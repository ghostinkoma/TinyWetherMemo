# -*- coding: utf-8 -*-
# ============================================================================
#  logic/webio.py  -  CGI 入出力ヘルパ (JSON / HTTPS / セッションCookie)
# ============================================================================
import os
import sys
import json
import traceback
from http.cookies import SimpleCookie
from .config import cfg
from .auth import parse_session, make_session

COOKIE_NAME = "wlbsid"


def method():
    return os.environ.get("REQUEST_METHOD", "GET").upper()


def is_https():
    return (
        os.environ.get("HTTPS", "").lower() in ("on", "1")
        or os.environ.get("HTTP_X_FORWARDED_PROTO", "") == "https"
        or os.environ.get("SERVER_PORT", "") == "443"
    )


def query():
    """GET クエリ文字列を dict で返す。"""
    from urllib.parse import parse_qs
    d = parse_qs(os.environ.get("QUERY_STRING", ""))
    return {k: v[0] for k, v in d.items()}


# ---- クライアント識別 / UA 判定 ----
def client_ip():
    xff = os.environ.get("HTTP_X_FORWARDED_FOR", "")
    if xff:
        return xff.split(",")[0].strip()[:45]
    return (os.environ.get("REMOTE_ADDR", "") or "")[:45]


def parse_ua(ua):
    u = (ua or "").lower()
    os_ = ("Android" if "android" in u else
           "iOS" if ("iphone" in u or "ipad" in u) else
           "Windows" if "windows" in u else
           "macOS" if ("macintosh" in u or "mac os" in u) else
           "Linux" if "linux" in u else "Other")
    br = ("Edge" if "edg/" in u else
          "Chrome" if ("chrome" in u or "crios" in u) else
          "Firefox" if "firefox" in u else
          "Safari" if "safari" in u else
          "curl" if "curl" in u else
          "python" if ("python" in u or "requests" in u) else "Other")
    return os_, br


def gate(conn, event=None, user_id=None, mac=None):
    """全 /api・/webapi 入口で呼ぶ: banned_ip / banned_device を照合し該当は 403。
       event を渡すとアクセスログに1行記録する(認証/ページ/管理/enroll 等)。
       conn は呼び出し側の接続を再利用。ban/log 失敗はリクエストを止めない(可用性優先)。"""
    from . import repo
    ip = client_ip()
    try:
        if repo.ip_banned(conn, ip) or (mac and repo.device_banned(conn, mac)):
            repo.log_access(conn, None, mac, ip, "blocked", 403)
            conn.commit()
            send_json({"error": "forbidden"}, 403)
        if event:
            repo.log_access(conn, user_id, mac, ip, event, 200)
            conn.commit()
    except SystemExit:
        raise
    except Exception:
        pass


def read_body():
    """JSON か form-urlencoded を dict で返す。"""
    try:
        n = int(os.environ.get("CONTENT_LENGTH", "0") or "0")
    except ValueError:
        n = 0
    if n <= 0 or n > 1_000_000:
        return {}
    raw = sys.stdin.buffer.read(n)
    ctype = os.environ.get("CONTENT_TYPE", "")
    if "application/json" in ctype:
        try:
            j = json.loads(raw.decode("utf-8") or "null")
            return j if isinstance(j, dict) else {}
        except Exception:
            return {}
    # form-urlencoded フォールバック
    from urllib.parse import parse_qs
    d = parse_qs(raw.decode("utf-8", "replace"))
    return {k: v[0] for k, v in d.items()}


def _emit(status, body_bytes, extra_headers=None):
    out = sys.stdout
    out.write("Status: {}\r\n".format(status))
    out.write("Content-Type: application/json; charset=utf-8\r\n")
    for h in (extra_headers or []):
        out.write(h + "\r\n")
    out.write("\r\n")
    out.flush()
    sys.stdout.buffer.write(body_bytes)
    sys.stdout.buffer.flush()


_STATUS = {200: "200 OK", 400: "400 Bad Request", 401: "401 Unauthorized",
           403: "403 Forbidden", 404: "404 Not Found", 405: "405 Method Not Allowed",
           500: "500 Internal Server Error"}


def send_json(obj, code=200, extra_headers=None):
    # default=str: datetime/Decimal 等を文字列化して安全にシリアライズ。
    body = json.dumps(obj, ensure_ascii=False, default=str).encode("utf-8")
    _emit(_STATUS.get(code, "200 OK"), body, extra_headers)
    sys.exit(0)


def send_error(name, code=500):
    """例外レスポンス。config.local.py の debug=True のときだけ traceback を含める。"""
    payload = {"error": name}
    try:
        if cfg().get("debug"):
            tb = traceback.format_exc()
            if tb and tb.strip() != "NoneType: None":
                payload["trace"] = tb
    except Exception:
        pass
    send_json(payload, code)


def require_https_or_die():
    if cfg().get("require_https") and not is_https():
        send_json({"error": "https_required"}, 400)


def require_post_or_die():
    if method() != "POST":
        send_json({"error": "method"}, 405)


# ---- セッション ----
def current_session():
    c = SimpleCookie(os.environ.get("HTTP_COOKIE", ""))
    if COOKIE_NAME in c:
        return parse_session(c[COOKIE_NAME].value)
    return None


def require_session_or_die():
    s = current_session()
    if not s:
        send_json({"error": "auth"}, 401)
    return s


def require_admin_or_die():
    s = require_session_or_die()
    if int(s.get("role", 0)) < 3:
        send_json({"error": "forbidden"}, 403)
    return s


def set_session_cookie(uid, role, name, persistent=True):
    # persistent=False は Max-Age を付けない=セッションCookie(ブラウザ終了で消滅)。ゲスト用。
    val = make_session(uid, role, name)
    secure = "; Secure" if cfg().get("require_https") else ""
    maxage = ("; Max-Age=%d" % int(cfg().get("session_ttl_sec", 86400))) if persistent else ""
    return "Set-Cookie: {}={}; Path=/; HttpOnly; SameSite=Strict{}{}".format(
        COOKIE_NAME, val, maxage, secure)


def clear_session_cookie():
    return "Set-Cookie: {}=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0".format(COOKIE_NAME)
