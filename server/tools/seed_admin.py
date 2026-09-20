#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  tools/seed_admin.py  -  初期管理ユーザー投入 (CLI)
#  実行 (ロリポップSSH, server/ 直下):  python3 tools/seed_admin.py
#  既定: username=admin / email=admin@localhost / password=wether / role=3
#  既に admin があれば何もしない。パスワードは PBKDF2 で保存 (平文非保持)。
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from logic.db import get_conn        # noqa: E402
from logic import auth, repo         # noqa: E402

USERNAME, EMAIL, PASSWORD, ROLE = "admin", "admin@localhost", "wether", 3


def main():
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT user_id FROM `users` WHERE username=%s OR email=%s LIMIT 1",
                        [USERNAME, EMAIL])
            if cur.fetchone():
                print("admin は既に存在します。変更しません。")
                return
        uid = repo.create_user(conn, USERNAME, EMAIL, auth.hash_password(PASSWORD), ROLE)
        conn.commit()
        print("作成しました: username=admin / password=wether / role=3 (user_id=%s)" % uid)
        print("ログイン後に必ずパスワードを変更してください。")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
