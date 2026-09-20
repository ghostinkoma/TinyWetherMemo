#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/device_delete.py  -  端末の論理削除 (要ログイン, owner/admin)
#  POST {mac} → {ok:1}
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
    mac = tokens.normalize_mac(str(webio.read_body().get("mac", "")))
    if not mac:
        webio.send_json({"error": "bad_mac"}, 400)
    conn = get_conn()
    try:
        webio.gate(conn, event="admin", user_id=s["uid"])
        if not repo.can_edit(conn, s, mac):
            webio.send_json({"error": "forbidden"}, 403)
        repo.device_delete(conn, mac, repo.actor_user(s["uid"]))
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
