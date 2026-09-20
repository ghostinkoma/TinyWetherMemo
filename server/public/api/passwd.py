#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/passwd.py  -  自分のパスワード変更 (要ログイン, ゲスト不可)  [CGI]
#  POST {old, new} → {ok:1} / {error: bad_old|weak}
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo                   # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    webio.require_post_or_die()
    s = webio.require_session_or_die()
    if int(s.get("role", 0)) < 0:                # guest 不可
        webio.send_json({"error": "forbidden"}, 403)
    b = webio.read_body()
    conn = get_conn()
    try:
        webio.gate(conn, event="admin", user_id=s["uid"])
        r = repo.change_password(conn, int(s["uid"]), str(b.get("old", "")), str(b.get("new", "")))
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
