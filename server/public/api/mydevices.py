#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/mydevices.py  -  端末登録・変更 用の一覧 (要ログイン, ゲスト不可)
#  GET → {devices:[{mac, name, guest_public}]}
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo                   # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    s = webio.require_session_or_die()
    if int(s.get("role", 0)) < repo.ROLE_ADD:    # guest(-1)/参照(0) は登録変更不可
        webio.send_json({"error": "forbidden"}, 403)
    conn = get_conn()
    try:
        webio.gate(conn, event="page", user_id=s["uid"])
        rows = repo.my_devices(conn, s)
    finally:
        conn.close()
    webio.send_json({"devices": [{"mac": r["mac"], "name": r["device_name"],
                                  "guest_public": int(r["guest_public"])} for r in rows]})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
