#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/login.py  -  人間ログイン (email/username + password)  [CGI]
#  POST {login, pass} → 200 {ok:1, user} + Set-Cookie(署名セッション) / 401 {ok:0}
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo, auth            # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    webio.require_post_or_die()
    body = webio.read_body()
    login = str(body.get("login") or body.get("email") or "").strip()
    pw = str(body.get("pass") or body.get("password") or "")

    conn = get_conn()
    try:
        u = repo.get_user_by_login(conn, login)
    finally:
        conn.close()

    # ユーザー不在でもダミーハッシュで検証しタイミング差を抑える。
    stored = u["password_hash"] if u else "pbkdf2_sha256$200000$00$00"
    if not auth.verify_password(pw, stored) or not u:
        webio.send_json({"ok": 0}, 401)

    hdr = webio.set_session_cookie(u["user_id"], u["role"], u["username"])
    webio.send_json({"ok": 1, "user": {"name": u["username"], "role": int(u["role"])}}, 200, [hdr])


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
