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
        webio.gate(conn)                         # BAN照合(該当IPは403)
        u = repo.get_user_by_login(conn, login)
        # ユーザー不在でもダミーハッシュで検証しタイミング差を抑える。
        stored = u["password_hash"] if u else "pbkdf2_sha256$200000$00$00"
        ok = bool(u) and auth.verify_password(pw, stored)
        # 未有効化ユーザはログイン不可 (activated 列が無い旧DBは get 相当で1扱い)
        if ok and u.get("activated", 1) == 0:
            ok = False
        try:
            repo.log_access(conn, (u["user_id"] if u else None), None, webio.client_ip(),
                            "login_ok" if ok else "login_ng", 200 if ok else 401)
            conn.commit()
        except Exception:
            conn.rollback()
        if not ok:
            webio.send_json({"ok": 0}, 401)
        uid, role, uname = u["user_id"], int(u["role"]), u["username"]
    finally:
        conn.close()

    hdr = webio.set_session_cookie(uid, role, uname)
    webio.send_json({"ok": 1, "user": {"name": uname, "role": role}}, 200, [hdr])


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
