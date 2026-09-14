# -*- coding: utf-8 -*-
# ============================================================================
#  logic/repo.py  -  業務ロジック (ユーザー/端末/権限/収集データ)
# ----------------------------------------------------------------------------
#  ・すべて db.py 経由 (%s プレースホルダ)。現行テーブルのみ参照 (=最新)。
#  ・履歴(_history)はアプリからは参照しない (DB直アクセス管理者専用)。
# ============================================================================
import time
from .db import fetchone, fetchall, versioned_write

# ロール: 0=参照のみ 1=登録のみ 2=編集削除可(物理削除可) 3=アドミン
ROLE_VIEW, ROLE_ADD, ROLE_EDIT, ROLE_ADMIN = 0, 1, 2, 3

DATA_TABLES = {
    "temp": "data_temperature",
    "humidity": "data_humidity",
    "pressure": "data_pressure",
    "lightning": "data_lightning",
}


# ---- 監査: 更新者(actor) ----
def actor_user(uid):
    return {"updated_by_user": int(uid), "updated_by_mac": None}


def actor_device(mac):
    return {"updated_by_user": None, "updated_by_mac": mac}


def actor_system():
    return {"updated_by_user": None, "updated_by_mac": None}


def _utcnow():
    return time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime())


def _epoch_to_utc(ts):
    return time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(int(ts)))


# ---- ユーザー ----
def get_user_by_login(conn, login):
    return fetchone(conn,
        "SELECT user_id, username, email, password_hash, role FROM `users` "
        "WHERE (email=%s OR username=%s) AND deleted=0 LIMIT 1", [login, login])


def create_user(conn, username, email, password_hash, role=ROLE_VIEW, parent_user_id=None):
    with conn.cursor() as cur:
        cur.execute(
            "INSERT INTO `users` (username, email, password_hash, role, parent_user_id) "
            "VALUES (%s, %s, %s, %s, %s)",
            [username, email, password_hash, role, parent_user_id])
        return cur.lastrowid


# ---- 端末登録 (enroll) ----
def enroll_device(conn, mac, name=None, start=None):
    row = fetchone(conn, "SELECT * FROM `devices` WHERE mac=%s", [mac])
    if row is None:
        changes = {"info_updated_at": _utcnow(), "deleted": 0}
        if name is not None:
            changes["device_name"] = name
        if start is not None:
            changes["start_date"] = start
        versioned_write(conn, "devices", {"mac": mac}, changes, actor_device(mac))
        return "created"
    # 既存: 実際に変わる項目だけ更新 (履歴の無駄な増殖を防ぐ)。
    changes = {}
    if name is not None and name != row.get("device_name"):
        changes["device_name"] = name
    if start is not None and str(start) != str(row.get("start_date")):
        changes["start_date"] = start
    if row.get("deleted") == 1:
        changes["deleted"] = 0
    if changes:
        changes["info_updated_at"] = _utcnow()
        versioned_write(conn, "devices", {"mac": mac}, changes, actor_device(mac))
        return "updated"
    return "unchanged"


def device_is_active(conn, mac):
    row = fetchone(conn, "SELECT deleted FROM `devices` WHERE mac=%s", [mac])
    return bool(row) and int(row["deleted"]) == 0


# ---- 収集データ投入 (ingest) ----
def ingest_readings(conn, mac, payload):
    default_ts = payload.get("ts")
    try:
        default_ts = int(default_ts)
    except (TypeError, ValueError):
        default_ts = int(time.time())

    inserted = 0
    for field, table in DATA_TABLES.items():
        rows = payload.get(field)
        if not isinstance(rows, list):
            continue
        for r in rows:
            if not isinstance(r, dict) or "value" not in r:
                continue
            key = str(r.get("key") or ("danger" if field == "lightning" else "main"))[:32]
            try:
                ts = int(r["ts"]) if "ts" in r else default_ts
            except (TypeError, ValueError):
                ts = default_ts
            v = r["value"]
            val = None if v is None else float(v)
            day = _epoch_to_utc(ts)
            versioned_write(conn, table,
                            {"mac": mac, "sensor_key": key, "daytime": day},
                            {"value": val}, actor_device(mac))   # 監査: この端末が投入
            inserted += 1
    return inserted


# ---- ダッシュボード ----
def dashboard_devices(conn, session):
    role = int(session.get("role", 0))
    uid = int(session.get("uid", 0))
    if role >= ROLE_ADMIN:
        devs = fetchall(conn, "SELECT mac, device_name FROM `devices` WHERE deleted=0 ORDER BY mac")
    else:
        devs = fetchall(conn,
            "SELECT d.mac, d.device_name FROM `devices` d "
            "JOIN `user_device_perm` p ON p.mac=d.mac AND p.user_id=%s AND p.deleted=0 "
            "WHERE d.deleted=0 ORDER BY d.mac", [uid])

    out = []
    for d in devs:
        mac = d["mac"]
        rec = {"mac": mac, "name": d.get("device_name"), "counts": {}, "last": None}
        last = None
        for field, table in DATA_TABLES.items():
            c = fetchone(conn, "SELECT COUNT(*) c, MAX(daytime) m FROM `{}` "
                               "WHERE mac=%s AND deleted=0".format(table), [mac])
            rec["counts"][field] = int(c["c"]) if c else 0
            if c and c["m"] and (last is None or str(c["m"]) > str(last)):
                last = c["m"]
        rec["last"] = str(last) if last else None
        out.append(rec)
    return out


# ---- 権限チェック (この人がこの端末を見てよいか) ----
def can_view(conn, session, mac):
    if int(session.get("role", 0)) >= ROLE_ADMIN:
        return fetchone(conn, "SELECT 1 FROM `devices` WHERE mac=%s AND deleted=0", [mac]) is not None
    r = fetchone(conn,
        "SELECT 1 FROM `user_device_perm` WHERE user_id=%s AND mac=%s AND deleted=0",
        [int(session.get("uid", 0)), mac])
    return r is not None


# ---- 最新値 (チャンネル毎の最新1点。MySQL8 window関数) ----
def latest_all(conn, mac):
    out = {}
    for field, table in DATA_TABLES.items():
        rows = fetchall(conn,
            "SELECT sensor_key, value, daytime FROM ("
            "  SELECT sensor_key, value, daytime,"
            "         ROW_NUMBER() OVER (PARTITION BY sensor_key ORDER BY daytime DESC) rn"
            "  FROM `{}` WHERE mac=%s AND deleted=0"
            ") x WHERE rn=1 ORDER BY sensor_key".format(table), [mac])
        out[field] = [{"key": r["sensor_key"],
                       "value": None if r["value"] is None else float(r["value"]),
                       "daytime": str(r["daytime"])} for r in rows]
    return out


# ---- 時系列 (チャート用) ----
def series(conn, mac, metric, sensor_key, since_dt, limit=2000):
    table = DATA_TABLES.get(metric)          # ホワイトリスト経由 (外部入力を識別子にしない)
    if not table:
        return None
    rows = fetchall(conn,
        "SELECT daytime, value FROM `{}` "
        "WHERE mac=%s AND sensor_key=%s AND deleted=0 AND daytime>=%s "
        "ORDER BY daytime ASC LIMIT %s".format(table),
        [mac, sensor_key, since_dt, int(limit)])
    return [{"t": str(r["daytime"]), "v": None if r["value"] is None else float(r["value"])} for r in rows]


# ---- CSV 出力用 (サーバに蓄積した全データを long 形式で) ----
def csv_rows(conn, mac):
    sql = (
        "SELECT daytime, 'temp' AS metric, sensor_key, value FROM `data_temperature` WHERE mac=%s AND deleted=0 "
        "UNION ALL SELECT daytime, 'humidity', sensor_key, value FROM `data_humidity` WHERE mac=%s AND deleted=0 "
        "UNION ALL SELECT daytime, 'pressure', sensor_key, value FROM `data_pressure` WHERE mac=%s AND deleted=0 "
        "UNION ALL SELECT daytime, 'lightning', sensor_key, value FROM `data_lightning` WHERE mac=%s AND deleted=0 "
        "ORDER BY daytime, metric, sensor_key"
    )
    return fetchall(conn, sql, [mac, mac, mac, mac])
