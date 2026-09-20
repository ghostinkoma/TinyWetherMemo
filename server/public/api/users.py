#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/users.py  -  ユーザー登録/一覧 (admin)  [CGI]
#  GET  → {users:[...], devices:[...]}   (登録フォーム用: 親候補=users, 割当端末=devices)
#  POST → 作成 {username, email, role, parent_user_id?, macs[]} → 認証コードをメール送付
#         応答 {ok:1, mailed, code?}  (メール不可時は code を admin に返す)
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo, mailer          # noqa: E402
from logic.config import cfg                     # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    s = webio.require_admin_or_die()
    conn = get_conn()
    try:
        webio.gate(conn, event="admin", user_id=s["uid"])
        if webio.method() == "GET":
            out = {"users": repo.users_list(conn), "devices": repo.devices_all(conn)}
            conn.close()
            webio.send_json(out)

        b = webio.read_body()
        username = str(b.get("username", "")).strip()
        email = str(b.get("email", "")).strip()
        role = int(b.get("role", 0) or 0)
        parent = b.get("parent_user_id")
        parent = int(parent) if parent not in (None, "", "0", 0) else None
        macs = b.get("macs") if isinstance(b.get("macs"), list) else []
        if not username or "@" not in email:
            webio.send_json({"error": "bad_input"}, 400)
        try:
            code = repo.create_user_pending(conn, username, email, role, parent, macs)
            conn.commit()
        except ValueError:
            conn.rollback()
            webio.send_json({"error": "email_exists"}, 409)
    except SystemExit:
        raise
    except Exception:
        try:
            conn.rollback(); conn.close()
        except Exception:
            pass
        webio.send_error("server")

    # メール送付 (失敗しても作成は成立。code を admin に返してフォールバック)
    base = cfg().get("base_url", "")
    mailed = False
    try:
        mailer.send_mail(email, "WetherLoggerBox アカウント有効化",
                         "WetherLoggerBox へようこそ。\n\n以下でアカウントを有効化してください(48時間有効)。\n"
                         "  メール: %s\n  コード: %s\n\n有効化: %s/ を開き『アクティベート』から\n"
                         "メールアドレス・コード・新しいパスワードを入力してください。\n" % (email, code, base))
        mailed = True
    except Exception:
        mailed = False
    try:
        conn.close()
    except Exception:
        pass
    webio.send_json({"ok": 1, "mailed": mailed, "code": (None if mailed else code)})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
