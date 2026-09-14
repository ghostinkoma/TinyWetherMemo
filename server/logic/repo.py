# -*- coding: utf-8 -*-
# ============================================================================
#  logic/repo.py  -  業務ロジック (ユーザー/端末/権限/収集データ)
# ----------------------------------------------------------------------------
#  ・すべて db.py 経由 (%s プレースホルダ)。現行テーブルのみ参照 (=最新)。
#  ・履歴(_history)はアプリからは参照しない (DB直アクセス管理者専用)。
# ============================================================================
import time
from .db import fetchone, fetchall, versioned_write

# ロール: -1=ゲスト(guest_public端末のみ閲覧) 0=参照 1=登録 2=編集削除(物理可) 3=アドミン
ROLE_GUEST = -1
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


def can_edit(conn, session, mac):
    role = int(session.get("role", 0))
    if role >= ROLE_ADMIN:
        return True
    uid = int(session.get("uid", 0))
    if fetchone(conn, "SELECT 1 FROM `devices` WHERE mac=%s AND owner_user_id=%s AND deleted=0", [mac, uid]):
        return True
    return fetchone(conn,
        "SELECT 1 FROM `user_device_perm` WHERE user_id=%s AND mac=%s AND deleted=0 AND role>=%s",
        [uid, mac, ROLE_EDIT]) is not None


def my_devices(conn, session):
    """端末登録・変更で編集可能な端末一覧。"""
    role = int(session.get("role", 0))
    uid = int(session.get("uid", 0))
    if role >= ROLE_ADMIN:
        return fetchall(conn, "SELECT mac, device_name, guest_public FROM `devices` WHERE deleted=0 ORDER BY mac")
    return fetchall(conn,
        "SELECT d.mac, d.device_name, d.guest_public FROM `devices` d "
        "LEFT JOIN `user_device_perm` p ON p.mac=d.mac AND p.user_id=%s AND p.deleted=0 "
        "WHERE d.deleted=0 AND (d.owner_user_id=%s OR (p.role IS NOT NULL AND p.role>=%s)) "
        "ORDER BY d.mac", [uid, uid, ROLE_EDIT])


def device_set_meta(conn, mac, name, guest_public, actor):
    changes = {}
    if name is not None:
        changes["device_name"] = name[:64]
    if guest_public is not None:
        changes["guest_public"] = 1 if guest_public else 0
    if not changes:
        return "unchanged"
    changes["info_updated_at"] = _utcnow()
    versioned_write(conn, "devices", {"mac": mac}, changes, actor)
    return "updated"


def device_delete(conn, mac, actor):
    """論理削除(deleted=1)。履歴は残す。一覧/閲覧から除外される。"""
    if not fetchone(conn, "SELECT 1 FROM `devices` WHERE mac=%s AND deleted=0", [mac]):
        return False
    versioned_write(conn, "devices", {"mac": mac}, {"deleted": 1}, actor)
    return True


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
    if role == ROLE_GUEST:
        devs = fetchall(conn, "SELECT mac, device_name FROM `devices` WHERE deleted=0 AND guest_public=1 ORDER BY mac")
    elif role >= ROLE_ADMIN:
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
    role = int(session.get("role", 0))
    if role == ROLE_GUEST:
        return fetchone(conn, "SELECT 1 FROM `devices` WHERE mac=%s AND deleted=0 AND guest_public=1", [mac]) is not None
    if role >= ROLE_ADMIN:
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


# ============================================================================
#  セキュリティ: アクセスログ / BAN / WHOIS(GeoIP)キャッシュ
# ============================================================================
def log_access(conn, user_id, mac, ip, event, status):
    import os
    from .webio import parse_ua
    ua = (os.environ.get("HTTP_USER_AGENT", "") or "")[:255]
    o, b = parse_ua(ua)
    fwd = os.environ.get("HTTP_X_FORWARDED_FOR")
    method = os.environ.get("REQUEST_METHOD", "")[:8]
    path = (os.environ.get("SCRIPT_NAME", "") or "")[:128]
    with conn.cursor() as cur:
        cur.execute(
            "INSERT INTO `access_log` (user_id,mac,ip,fwd_ip,os,browser,user_agent,method,path,event,status) "
            "VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s)",
            [user_id, mac, ip, fwd, o, b, ua, method, path, event, status])


def ip_banned(conn, ip):
    if not ip:
        return False
    return fetchone(conn, "SELECT 1 FROM `banned_ip` WHERE ip=%s LIMIT 1", [ip]) is not None


def device_banned(conn, mac):
    if not mac:
        return False
    return fetchone(conn, "SELECT 1 FROM `banned_device` WHERE mac=%s LIMIT 1", [mac]) is not None


# ---- BAN CRUD (admin) ----
def ban_list(conn):
    return {
        "ip": fetchall(conn, "SELECT ip, reason, created_at FROM `banned_ip` ORDER BY created_at DESC LIMIT 500"),
        "device": fetchall(conn, "SELECT mac, reason, created_at FROM `banned_device` ORDER BY created_at DESC LIMIT 500"),
    }


def ban_add_ip(conn, ip, reason, by):
    conn.cursor().execute(
        "INSERT INTO `banned_ip` (ip,reason,created_by) VALUES (%s,%s,%s) "
        "ON DUPLICATE KEY UPDATE reason=VALUES(reason)", [ip[:45], (reason or "")[:128], by])


def ban_del_ip(conn, ip):
    conn.cursor().execute("DELETE FROM `banned_ip` WHERE ip=%s", [ip])


def ban_add_device(conn, mac, reason, by):
    conn.cursor().execute(
        "INSERT INTO `banned_device` (mac,reason,created_by) VALUES (%s,%s,%s) "
        "ON DUPLICATE KEY UPDATE reason=VALUES(reason)", [mac, (reason or "")[:128], by])


def ban_del_device(conn, mac):
    conn.cursor().execute("DELETE FROM `banned_device` WHERE mac=%s", [mac])


# ---- アクセスログ閲覧 (admin) ----
def accesslog_recent(conn, limit=200, event=None):
    if event:
        return fetchall(conn,
            "SELECT log_id,user_id,mac,ip,os,browser,method,path,event,status,created_at "
            "FROM `access_log` WHERE event=%s ORDER BY log_id DESC LIMIT %s", [event, int(limit)])
    return fetchall(conn,
        "SELECT log_id,user_id,mac,ip,os,browser,method,path,event,status,created_at "
        "FROM `access_log` ORDER BY log_id DESC LIMIT %s", [int(limit)])


def accesslog_by_ip(conn, limit=100):
    return fetchall(conn,
        "SELECT ip, COUNT(*) cnt, MAX(created_at) last, MIN(created_at) first, "
        "SUM(event='login_ng') ng, SUM(event='blocked') blocked "
        "FROM `access_log` GROUP BY ip ORDER BY cnt DESC LIMIT %s", [int(limit)])


def accesslog_by_hour(conn, hours=48):
    return fetchall(conn,
        "SELECT DATE_FORMAT(created_at,'%%Y-%%m-%%d %%H:00') slot, COUNT(*) cnt, "
        "COUNT(DISTINCT ip) ips, SUM(event='login_ng') ng "
        "FROM `access_log` WHERE created_at >= (NOW() - INTERVAL %s HOUR) "
        "GROUP BY slot ORDER BY slot DESC", [int(hours)])


# ---- GeoIP/WHOIS キャッシュ ----
def geo_get(conn, ip):
    return fetchone(conn, "SELECT ip,country,isp,org,asn,updated_at FROM `ip_geo_cache` WHERE ip=%s", [ip])


def geo_set(conn, ip, country, isp, org, asn):
    conn.cursor().execute(
        "INSERT INTO `ip_geo_cache` (ip,country,isp,org,asn) VALUES (%s,%s,%s,%s,%s) "
        "ON DUPLICATE KEY UPDATE country=VALUES(country),isp=VALUES(isp),org=VALUES(org),asn=VALUES(asn),updated_at=NOW()",
        [ip[:45], (country or "")[:64], (isp or "")[:128], (org or "")[:128], (asn or "")[:64]])


# ============================================================================
#  ユーザー管理 (admin) / アクティベーション / パスワード変更
# ============================================================================
import secrets                                          # noqa: E402
from . import auth, tokens                              # noqa: E402


def _exp(hours):
    return time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(int(time.time()) + hours * 3600))


def users_list(conn):
    return fetchall(conn,
        "SELECT user_id, username, email, role, parent_user_id, activated, created_at "
        "FROM `users` WHERE deleted=0 ORDER BY user_id")


def devices_all(conn):
    return fetchall(conn, "SELECT mac, device_name FROM `devices` WHERE deleted=0 ORDER BY mac")


def create_user_pending(conn, username, email, role, parent_user_id, macs):
    """未有効化ユーザを作成し、割当端末の閲覧権限を付与。戻り値=アクティベーションコード(平文)。"""
    if fetchone(conn, "SELECT 1 FROM `users` WHERE email=%s AND deleted=0", [email]):
        raise ValueError("email_exists")
    code = secrets.token_hex(4)                          # 8桁hex
    dummy = auth.hash_password(secrets.token_hex(8))     # 有効化まで使えないダミー
    with conn.cursor() as cur:
        cur.execute(
            "INSERT INTO `users` (username,email,password_hash,role,parent_user_id,"
            "activated,activation_code_hash,activation_expires) VALUES (%s,%s,%s,%s,%s,0,%s,%s)",
            [username[:64], email[:255], dummy, int(role), parent_user_id,
             auth.hash_password(code), _exp(48)])
        uid = cur.lastrowid
        for mac in (macs or []):
            m = tokens.normalize_mac(mac)
            if m:
                cur.execute(
                    "INSERT INTO `user_device_perm` (user_id,mac,role,updated_by_user) VALUES (%s,%s,%s,%s) "
                    "ON DUPLICATE KEY UPDATE deleted=0, role=VALUES(role)",
                    [uid, m, ROLE_VIEW, uid])
    return code


def activate_user(conn, email, code, new_password):
    u = fetchone(conn,
        "SELECT user_id, activation_code_hash, activation_expires, activated "
        "FROM `users` WHERE email=%s AND deleted=0", [email])
    if not u or int(u["activated"]) == 1:
        return "invalid"
    if not u["activation_code_hash"] or not auth.verify_password(code, u["activation_code_hash"]):
        return "invalid"
    if u["activation_expires"] and str(u["activation_expires"]) < time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime()):
        return "expired"
    if len(new_password) < 8:
        return "weak"
    versioned_write(conn, "users", {"user_id": u["user_id"]},
                    {"password_hash": auth.hash_password(new_password), "activated": 1,
                     "activation_code_hash": None, "activation_expires": None},
                    actor_user(u["user_id"]))
    return "ok"


def change_password(conn, uid, old_pw, new_pw):
    u = fetchone(conn, "SELECT password_hash FROM `users` WHERE user_id=%s AND deleted=0", [uid])
    if not u or not auth.verify_password(old_pw, u["password_hash"]):
        return "bad_old"
    if len(new_pw) < 8:
        return "weak"
    versioned_write(conn, "users", {"user_id": uid},
                    {"password_hash": auth.hash_password(new_pw)}, actor_user(uid))
    return "ok"


# ---- 端末アクティベートキー ----
def device_register(conn, mac, owner_uid, name, guest_public, actor):
    """端末を登録/更新し per-device アクティベートキーを発行。戻り値=キー(平文, オーナーへメール)。"""
    key = secrets.token_hex(8)                           # 16桁hex
    changes = {"activation_key_hash": auth.hash_password(key), "activated": 0,
               "activation_expires": _exp(24 * 7), "info_updated_at": _utcnow(),
               "owner_user_id": int(owner_uid)}
    if name is not None:
        changes["device_name"] = name[:64]
    if guest_public is not None:
        changes["guest_public"] = 1 if guest_public else 0
    if not fetchone(conn, "SELECT 1 FROM `devices` WHERE mac=%s", [mac]):
        changes["deleted"] = 0
    versioned_write(conn, "devices", {"mac": mac}, changes, actor)
    # オーナーに編集権限を付与
    conn.cursor().execute(
        "INSERT INTO `user_device_perm` (user_id,mac,role,updated_by_user) VALUES (%s,%s,%s,%s) "
        "ON DUPLICATE KEY UPDATE deleted=0, role=VALUES(role)", [int(owner_uid), mac, ROLE_EDIT, int(owner_uid)])
    return key


def enroll_verify(conn, mac, code):
    """端末 enroll の認証。per-device アクティベートキー優先、無ければ共有 enroll_code(移行用)。
       成功なら device 行を用意/有効化して True。"""
    d = fetchone(conn, "SELECT activation_key_hash, activation_expires FROM `devices` WHERE mac=%s", [mac])
    if d and d.get("activation_key_hash"):
        exp_ok = (not d["activation_expires"]) or \
                 (str(d["activation_expires"]) >= time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime()))
        if exp_ok and auth.verify_password(code, d["activation_key_hash"]):
            versioned_write(conn, "devices", {"mac": mac},
                            {"activated": 1, "activation_key_hash": None, "activation_expires": None,
                             "deleted": 0}, actor_device(mac))
            return True
        return False
    # フォールバック: 共有 enroll_code (未登録端末の移行用。運用で無効化するなら config の enroll_code を空に)
    if tokens.verify_enroll_code(code):
        enroll_device(conn, mac, None, None)
        return True
    return False
