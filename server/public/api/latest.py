#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/latest.py  -  端末の最新値 (人間UI用, 要ログイン)  [CGI]
#  GET ?mac=... → {mac, latest:{temp:[{key,value,daytime}], humidity, pressure, lightning}}
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo, tokens         # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    session = webio.require_session_or_die()
    mac = tokens.normalize_mac(webio.query().get("mac", ""))
    if not mac:
        webio.send_json({"error": "bad_mac"}, 400)
    conn = get_conn()
    try:
        if not repo.can_view(conn, session, mac):
            webio.send_json({"error": "forbidden"}, 403)
        latest = repo.latest_all(conn, mac)
    finally:
        conn.close()
    webio.send_json({"mac": mac, "latest": latest})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
