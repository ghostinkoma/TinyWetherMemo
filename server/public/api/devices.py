#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/devices.py  -  端末一覧 + 直近件数 (ダッシュボード)  [CGI]
#  GET (要ログイン) → {devices:[{mac,name,counts:{temp,humidity,lightning},last}]}
#  ・admin(role3)=全端末 / それ以外=user_device_perm で許可された端末のみ。
#  ・現行テーブルのみ参照 (=最新)。履歴は含めない。
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo                   # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    session = webio.require_session_or_die()
    conn = get_conn()
    try:
        devs = repo.dashboard_devices(conn, session)
    finally:
        conn.close()
    webio.send_json({"devices": devs})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
