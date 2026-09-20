#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/bans.py  -  禁止IP / 禁止端末 の管理 (admin)  [CGI]
#  GET               → {ip:[...], device:[...]}
#  POST {type:'ip'|'device', value, reason?, action:'add'|'delete'}
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo, tokens          # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    s = webio.require_admin_or_die()
    conn = get_conn()
    try:
        webio.gate(conn, event="admin", user_id=s["uid"])
        if webio.method() == "GET":
            out = repo.ban_list(conn)
        else:
            b = webio.read_body()
            t = b.get("type")
            act = b.get("action", "add")
            reason = str(b.get("reason", ""))
            if t == "ip":
                v = str(b.get("value", "")).strip()[:45]
                if not v:
                    webio.send_json({"error": "bad_value"}, 400)
                repo.ban_del_ip(conn, v) if act == "delete" else repo.ban_add_ip(conn, v, reason, s["uid"])
            elif t == "device":
                v = tokens.normalize_mac(str(b.get("value", "")))
                if not v:
                    webio.send_json({"error": "bad_mac"}, 400)
                repo.ban_del_device(conn, v) if act == "delete" else repo.ban_add_device(conn, v, reason, s["uid"])
            else:
                webio.send_json({"error": "bad_type"}, 400)
            conn.commit()
            out = {"ok": 1}
    except SystemExit:
        raise
    except Exception:
        conn.rollback()
        conn.close()
        webio.send_error("server")
    finally:
        try:
            conn.close()
        except Exception:
            pass
    webio.send_json(out)


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
