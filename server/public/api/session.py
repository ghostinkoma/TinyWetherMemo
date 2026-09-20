#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/session.py  -  セッション確認 / ログアウト  [CGI]
#  GET  → {authed:0|1, user?}
#  POST → ログアウト (Cookie 失効) {ok:1}
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio                         # noqa: E402


def main():
    webio.require_https_or_die()
    if webio.method() == "POST":
        webio.send_json({"ok": 1}, 200, [webio.clear_session_cookie()])
    s = webio.current_session()
    if not s:
        webio.send_json({"authed": 0})
    webio.send_json({"authed": 1, "user": {"name": s.get("name"), "role": int(s.get("role", 0))}})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
