# -*- coding: utf-8 -*-
# ============================================================================
#  logic/tokens.py  -  端末⇄サーバ トークン (HMAC-SHA256(hmac_secret, MAC))
# ----------------------------------------------------------------------------
#  ・ステートレス検証: MAC からトークンを都度再計算し hmac.compare_digest で
#    定数時間比較 (DB照合不要)。「MACをキーに生成」の実装。
#  ・失効: devices.deleted=1 で拒否 (ingest 側で現行 deleted を確認)。
# ============================================================================
import re
import hmac
import hashlib
from .config import cfg

_HEX12 = re.compile(r"[0-9a-fA-F]")


def normalize_mac(raw):
    """小文字・コロン区切り "aa:bb:cc:dd:ee:ff"。不正なら空文字。"""
    if not raw:
        return ""
    hexs = "".join(_HEX12.findall(str(raw))).lower()
    if len(hexs) != 12:
        return ""
    return ":".join(hexs[i:i + 2] for i in range(0, 12, 2))


def make_token(mac):
    """正規化済み MAC を渡すこと。hex 64桁を返す。"""
    secret = str(cfg()["hmac_secret"]).encode("utf-8")
    return hmac.new(secret, mac.encode("utf-8"), hashlib.sha256).hexdigest()


def verify_token(mac, token):
    if not mac or not token:
        return False
    return hmac.compare_digest(make_token(mac), str(token).lower())


def verify_enroll_code(code):
    expect = str(cfg().get("enroll_code", ""))
    if not expect or not code:
        return False
    return hmac.compare_digest(expect, str(code))


def bearer_token(environ):
    """Authorization: Bearer <hex64> をCGI環境から取り出す。"""
    h = environ.get("HTTP_AUTHORIZATION") or environ.get("REDIRECT_HTTP_AUTHORIZATION") or ""
    m = re.match(r"^Bearer\s+([0-9a-fA-F]{64})$", h)
    return m.group(1).lower() if m else ""
