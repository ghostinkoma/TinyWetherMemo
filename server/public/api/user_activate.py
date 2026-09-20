#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/user_activate.py  -  ユーザー有効化 (未ログイン)  [CGI]
#  POST {email, code, password} → {ok:1} / {error: invalid|expired|weak}
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo                   # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    webio.require_post_or_die()
    b = webio.read_body()
    email = str(b.get("email", "")).strip()
    code = str(b.get("code", "")).strip()
    pw = str(b.get("password", ""))
    if not email or not code:
        webio.send_json({"error": "bad_input"}, 400)

    conn = get_conn()
    try:
        webio.gate(conn, event="page")
        r = repo.activate_user(conn, email, code, pw)
        if r == "ok":
            conn.commit()
        else:
            conn.rollback()
    except SystemExit:
        raise
    except Exception:
        conn.rollback(); conn.close()
        webio.send_error("server")
    finally:
        try:
            conn.close()
        except Exception:
            pass
    if r == "ok":
        webio.send_json({"ok": 1})
    webio.send_json({"ok": 0, "error": r}, 400)


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
