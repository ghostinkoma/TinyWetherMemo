#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/device_register.py  -  端末MAC登録 + アクティベートキー発行(メール)
#  POST {mac, name?, guest_public?, owner_user_id?(admin)} → {ok:1, mailed, key?}
#  ・オーナー(既定=自分, adminは他ユーザ指定可)へ per-device キーをメール。
#  ・端末側「サーバ連携」の認証コード欄にこのキーを入力→enrollで有効化。
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo, tokens, mailer   # noqa: E402
from logic.config import cfg                     # noqa: E402
from logic.db import get_conn, fetchone          # noqa: E402


def main():
    webio.require_https_or_die()
    webio.require_post_or_die()
    s = webio.require_session_or_die()
    if int(s.get("role", 0)) < repo.ROLE_ADD:
        webio.send_json({"error": "forbidden"}, 403)
    b = webio.read_body()
    mac = tokens.normalize_mac(str(b.get("mac", "")))
    if not mac:
        webio.send_json({"error": "bad_mac"}, 400)
    name = str(b["name"]) if b.get("name") is not None else None
    gp = 1 if str(b.get("guest_public")) in ("1", "true", "True", "on") else (0 if "guest_public" in b else None)
    owner = int(s["uid"])
    if int(s.get("role", 0)) >= repo.ROLE_ADMIN and b.get("owner_user_id"):
        owner = int(b["owner_user_id"])

    conn = get_conn()
    try:
        webio.gate(conn, event="admin", user_id=s["uid"])
        ownrow = fetchone(conn, "SELECT email FROM `users` WHERE user_id=%s AND deleted=0", [owner])
        if not ownrow:
            webio.send_json({"error": "bad_owner"}, 400)
        key = repo.device_register(conn, mac, owner, name, gp, repo.actor_user(s["uid"]))
        conn.commit()
        owner_email = ownrow["email"]
    except SystemExit:
        raise
    except Exception:
        try:
            conn.rollback(); conn.close()
        except Exception:
            pass
        webio.send_error("server")

    mailed = False
    try:
        mailer.send_mail(owner_email, "WetherLoggerBox 端末アクティベートキー",
                         "端末 %s のアクティベートキーです(7日間有効)。\n\n  キー: %s\n\n"
                         "端末の『サーバ連携』設定で、接続文字列=%s / ルートパス=/webapi /\n"
                         "認証コード欄に上記キーを入力し[接続]してください。\n"
                         % (mac, key, cfg().get("base_url", "")))
        mailed = True
    except Exception:
        mailed = False
    try:
        conn.close()
    except Exception:
        pass
    webio.send_json({"ok": 1, "mailed": mailed, "key": (None if mailed else key)})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
