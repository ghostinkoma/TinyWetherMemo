# -*- coding: utf-8 -*-
# ============================================================================
#  logic/auth.py  -  パスワードハッシュ(PBKDF2) と 署名セッションCookie
# ----------------------------------------------------------------------------
#  ・パスワード: PBKDF2-HMAC-SHA256 (hashlib 標準。C拡張ビルド不要=共有ホスト向き)。
#    形式 "pbkdf2_sha256$<iter>$<salt_hex>$<hash_hex>"。検証は compare_digest。
#  ・セッション: 署名Cookie。値 = b64url(json{uid,role,name,exp}) + "." + HMAC。
#    サーバは session_secret で HMAC を検証(改ざん検知)し exp を確認 (ステートレス)。
# ============================================================================
import hmac
import json
import time
import base64
import hashlib
import secrets
from .config import cfg


# ---- パスワード ----
def hash_password(password):
    it = int(cfg().get("pbkdf2_iterations", 200000))
    salt = secrets.token_bytes(16)
    dk = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, it)
    return "pbkdf2_sha256${}${}${}".format(it, salt.hex(), dk.hex())


def verify_password(password, stored):
    try:
        algo, it, salt_hex, hash_hex = str(stored).split("$", 3)
        if algo != "pbkdf2_sha256":
            return False
        dk = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"),
                                 bytes.fromhex(salt_hex), int(it))
        return hmac.compare_digest(dk.hex(), hash_hex)
    except Exception:
        return False


# ---- 署名セッションCookie ----
def _b64e(b):
    return base64.urlsafe_b64encode(b).decode("ascii").rstrip("=")


def _b64d(s):
    pad = "=" * (-len(s) % 4)
    return base64.urlsafe_b64decode(s + pad)


def _sign(payload_b64):
    key = str(cfg()["session_secret"]).encode("utf-8")
    return hmac.new(key, payload_b64.encode("ascii"), hashlib.sha256).hexdigest()


def make_session(uid, role, name):
    ttl = int(cfg().get("session_ttl_sec", 86400))
    payload = {"uid": int(uid), "role": int(role), "name": name, "exp": int(time.time()) + ttl}
    p = _b64e(json.dumps(payload, separators=(",", ":")).encode("utf-8"))
    return p + "." + _sign(p)


def parse_session(cookie_val):
    if not cookie_val or "." not in cookie_val:
        return None
    p, sig = cookie_val.rsplit(".", 1)
    if not hmac.compare_digest(_sign(p), sig):
        return None
    try:
        data = json.loads(_b64d(p).decode("utf-8"))
    except Exception:
        return None
    if int(data.get("exp", 0)) < int(time.time()):
        return None
    return data
