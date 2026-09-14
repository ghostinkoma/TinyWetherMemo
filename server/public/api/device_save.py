#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/device_save.py  -  端末 名称/ゲスト公開 の更新 (要ログイン)
#  POST {mac, name?, guest_public?} → {ok:1}
#  ※ アクティベートキーのメール発行はメール(SMTP)設定後の別フェーズ。
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo, tokens          # noqa: E402
from logic.db import get_conn                    # noqa: E402


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
    gp = None
    if "guest_public" in b:
        gp = 1 if str(b.get("guest_public")) in ("1", "true", "True", "on") else 0

    conn = get_conn()
    try:
        webio.gate(conn, event="admin", user_id=s["uid"])
        if not repo.can_edit(conn, s, mac):
            webio.send_json({"error": "forbidden"}, 403)
        repo.device_set_meta(conn, mac, name, gp, repo.actor_user(s["uid"]))
        conn.commit()
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
    webio.send_json({"ok": 1})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
