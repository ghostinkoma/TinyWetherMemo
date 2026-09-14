#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/enroll.py  -  端末登録 (認証コード → トークン発行)  [CGI]
#  POST {mac, code, name?, start?} → 200 {token, mac} / 401 bad_code / 400 bad_mac
# ============================================================================
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, tokens, repo          # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    webio.require_post_or_die()
    body = webio.read_body()

    mac = tokens.normalize_mac(body.get("mac", ""))
    code = str(body.get("code", ""))
    if not mac:
        webio.send_json({"error": "bad_mac"}, 400)
    if not tokens.verify_enroll_code(code):
        webio.send_json({"error": "bad_code"}, 401)

    name = str(body["name"])[:64] if body.get("name") is not None else None
    start = None
    if body.get("start") and re.match(r"^\d{4}-\d{2}-\d{2}$", str(body["start"])):
        start = str(body["start"])

    conn = get_conn()
    try:
        repo.enroll_device(conn, mac, name, start)
        conn.commit()
    except Exception:
        conn.rollback()
        webio.send_error("db")
    finally:
        conn.close()

    webio.send_json({"token": tokens.make_token(mac), "mac": mac})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
