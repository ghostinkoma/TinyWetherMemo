#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/accesslog.py  -  アクセスログ閲覧 (admin)  [CGI]
#  GET ?view=byip|byhour|recent[&event=login_ng]
#    byip   : IP集計(アクセス回数・失敗・blocked)。悪質IP発見用
#    byhour : 時間帯別集計(直近48h)
#    recent : 直近明細(event で絞込可)
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo                   # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    s = webio.require_admin_or_die()
    q = webio.query()
    view = q.get("view", "byip")
    conn = get_conn()
    try:
        webio.gate(conn, event="admin", user_id=s["uid"])
        if view == "recent":
            rows = repo.accesslog_recent(conn, 300, q.get("event"))
        elif view == "byhour":
            rows = repo.accesslog_by_hour(conn, 48)
        else:
            view = "byip"
            rows = repo.accesslog_by_ip(conn, 200)
    finally:
        conn.close()
    webio.send_json({"view": view, "rows": rows})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
