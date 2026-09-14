# -*- coding: utf-8 -*-
# ============================================================================
#  logic/db.py  -  PyMySQL 接続 と バージョン管理書き込み (現行+履歴)
# ----------------------------------------------------------------------------
#  ★SQLインジェクション対策: 値は必ず %s プレースホルダ経由 (PyMySQL がエスケープ)。
#    テーブル/列名はこのモジュール内の固定リテラル/ホワイトリストのみ。外部入力は
#    識別子に使わない (pk/changes のキーは repo.py が固定文字列で与える)。
#
#  バージョン方針: 現行テーブル=最新1行。変更時は旧行を _history へ退避してから
#    現行を UPDATE(version+1)。物理削除も退避してから DELETE。直列化のため対象行を
#    SELECT ... FOR UPDATE でロックしてから行う (呼び出しは1トランザクション内)。
# ============================================================================
import pymysql
from .config import cfg

# 現行テーブル → {pk, cols(退避する全列), history} のメタ (列名は固定)。
#  updated_by_user / updated_by_mac = 監査(誰が更新したか)。履歴退避にも含める。
ACTOR_COLS = ["updated_by_user", "updated_by_mac"]
_COMMON_TAIL = ["version", "created_at", "updated_at", "deleted"] + ACTOR_COLS
TABLES = {
    "users": {
        "pk": ["user_id"],
        "cols": ["user_id", "username", "email", "password_hash", "role",
                 "parent_user_id", "billing_confirmed",
                 "activated", "activation_code_hash", "activation_expires"] + _COMMON_TAIL,
        "history": "users_history",
    },
    "devices": {
        "pk": ["mac"],
        "cols": ["mac", "device_name", "owner_user_id", "start_date",
                 "temp_sensor", "temp_offset", "humi_sensor", "humi_offset",
                 "ln_sensor", "ln_config", "info_updated_at", "last_data_at",
                 "guest_public", "activation_key_hash", "activated", "activation_expires"] + _COMMON_TAIL,
        "history": "devices_history",
    },
    "user_device_perm": {
        "pk": ["user_id", "mac"],
        "cols": ["user_id", "mac", "role"] + _COMMON_TAIL,
        "history": "user_device_perm_history",
    },
    "data_temperature": {
        "pk": ["mac", "sensor_key", "daytime"],
        "cols": ["mac", "sensor_key", "daytime", "value"] + _COMMON_TAIL,
        "history": "data_temperature_history",
    },
    "data_humidity": {
        "pk": ["mac", "sensor_key", "daytime"],
        "cols": ["mac", "sensor_key", "daytime", "value"] + _COMMON_TAIL,
        "history": "data_humidity_history",
    },
    "data_pressure": {
        "pk": ["mac", "sensor_key", "daytime"],
        "cols": ["mac", "sensor_key", "daytime", "value"] + _COMMON_TAIL,
        "history": "data_pressure_history",
    },
    "data_lightning": {
        "pk": ["mac", "sensor_key", "daytime"],
        "cols": ["mac", "sensor_key", "daytime", "value"] + _COMMON_TAIL,
        "history": "data_lightning_history",
    },
}


def get_conn():
    c = cfg()["db"]
    return pymysql.connect(
        host=c["host"], user=c["user"], password=c["pass"],
        database=c["name"], charset=c["charset"],
        cursorclass=pymysql.cursors.DictCursor,
        autocommit=False,
    )


def _bt(name):
    # 識別子は固定リテラル前提だが、念のためバッククォートをエスケープして囲む。
    return "`" + str(name).replace("`", "``") + "`"


def versioned_write(conn, table, pk: dict, changes: dict, actor: dict = None):
    """現行行が無ければ INSERT(version=1)、あれば旧行を履歴退避してから UPDATE(version+1)。
       actor = {'updated_by_user':.., 'updated_by_mac':..} (どちらか/両NULL)。監査列に反映。
       戻り値: 'inserted' | 'updated'。呼び出し側でトランザクション制御(commit)する。"""
    meta = TABLES[table]
    pk_cols = meta["pk"]
    assert set(pk.keys()) == set(pk_cols), "pk keys mismatch"
    changes = dict(changes)
    if actor:
        changes.update(actor)                     # 監査: 誰が更新したか
    where = " AND ".join(f"{_bt(c)}=%s" for c in pk_cols)
    where_vals = [pk[c] for c in pk_cols]

    with conn.cursor() as cur:
        cur.execute(f"SELECT * FROM {_bt(table)} WHERE {where} FOR UPDATE", where_vals)
        row = cur.fetchone()
        if row is None:
            cols = list(pk.keys()) + list(changes.keys())
            vals = [pk[c] for c in pk] + [changes[c] for c in changes]
            collist = ",".join(_bt(c) for c in cols)
            ph = ",".join(["%s"] * len(cols))
            cur.execute(f"INSERT INTO {_bt(table)} ({collist}) VALUES ({ph})", vals)
            return "inserted"
        # 旧行を履歴へ退避 (cols のみコピー)。
        hcols = meta["cols"]
        hlist = ",".join(_bt(c) for c in hcols)
        hph = ",".join(["%s"] * len(hcols))
        cur.execute(f"INSERT INTO {_bt(meta['history'])} ({hlist}) VALUES ({hph})",
                    [row[c] for c in hcols])
        # 現行を更新 (version+1)。
        setparts = [f"{_bt(c)}=%s" for c in changes] + ["`version`=`version`+1"]
        setvals = [changes[c] for c in changes]
        cur.execute(f"UPDATE {_bt(table)} SET {', '.join(setparts)} WHERE {where}",
                    setvals + where_vals)
        return "updated"


def hard_delete(conn, table, pk: dict, actor: dict = None):
    """物理削除: 旧行を履歴退避してから現行 DELETE。actor を渡すと履歴に「削除実行者」を記録。
       戻り値: 削除件数。"""
    meta = TABLES[table]
    pk_cols = meta["pk"]
    where = " AND ".join(f"{_bt(c)}=%s" for c in pk_cols)
    where_vals = [pk[c] for c in pk_cols]
    with conn.cursor() as cur:
        cur.execute(f"SELECT * FROM {_bt(table)} WHERE {where} FOR UPDATE", where_vals)
        row = cur.fetchone()
        if row is None:
            return 0
        if actor:
            row = {**row, **actor}                # 履歴に削除実行者を残す
        hcols = meta["cols"]
        hlist = ",".join(_bt(c) for c in hcols)
        hph = ",".join(["%s"] * len(hcols))
        cur.execute(f"INSERT INTO {_bt(meta['history'])} ({hlist}) VALUES ({hph})",
                    [row[c] for c in hcols])
        cur.execute(f"DELETE FROM {_bt(table)} WHERE {where}", where_vals)
        return 1


def fetchone(conn, sql, params=None):
    with conn.cursor() as cur:
        cur.execute(sql, params or [])
        return cur.fetchone()


def fetchall(conn, sql, params=None):
    with conn.cursor() as cur:
        cur.execute(sql, params or [])
        return cur.fetchall()
