#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/guest.py  -  ゲスト閲覧開始 (パスワード無し)  [CGI]
#  POST → guest セッション(role=-1, uid=0)を発行。guest_public=1 端末のみ閲覧可。
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio                         # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    webio.require_post_or_die()
    conn = get_conn()
    try:
        webio.gate(conn, event="guest")          # BAN照合 + ログ
    finally:
        conn.close()
    hdr = webio.set_session_cookie(0, -1, "guest", persistent=False)  # セッションCookie(一時)
    webio.send_json({"ok": 1, "user": {"name": "guest", "role": -1}}, 200, [hdr])


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
