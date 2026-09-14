#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/ingest.py  -  気象データ投入 (端末→サーバ PUSH)  [CGI]
#  認証: Authorization: Bearer <token>  (token = HMAC-SHA256(hmac_secret, MAC))
#  POST {mac, ts?, temp[], humidity[], lightning[]} → 200 {ok:1, inserted:N}
#       各要素 {key?, value, ts?}。ts は unix秒(UTC)。
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, tokens, repo          # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    webio.require_post_or_die()
    body = webio.read_body()

    mac = tokens.normalize_mac(body.get("mac", ""))
    token = tokens.bearer_token(os.environ)
    if not mac:
        webio.send_json({"error": "bad_mac"}, 400)
    if not tokens.verify_token(mac, token):
        webio.send_json({"error": "unauthorized"}, 401)

    conn = get_conn()
    try:
        if not repo.device_is_active(conn, mac):
            webio.send_json({"error": "device_disabled"}, 403)
        n = repo.ingest_readings(conn, mac, body)
        conn.commit()
    except SystemExit:
        conn.rollback()
        raise
    except Exception:
        conn.rollback()
        webio.send_error("db")
    finally:
        conn.close()

    webio.send_json({"ok": 1, "inserted": n})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
