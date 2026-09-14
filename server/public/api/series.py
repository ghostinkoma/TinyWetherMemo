#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/series.py  -  時系列データ (チャート用, 要ログイン)  [CGI]
#  GET ?mac=&metric=temp|humidity|pressure|lightning&key=air&hours=24
#    → {mac, metric, key, points:[{t,v}]}
# ============================================================================
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo, tokens         # noqa: E402
from logic.db import get_conn                    # noqa: E402


def main():
    webio.require_https_or_die()
    session = webio.require_session_or_die()
    q = webio.query()
    mac = tokens.normalize_mac(q.get("mac", ""))
    metric = q.get("metric", "temp")
    key = str(q.get("key", "main"))[:32]
    try:
        hours = max(1, min(24 * 90, int(q.get("hours", "24"))))   # 1h..90日
    except ValueError:
        hours = 24
    if not mac:
        webio.send_json({"error": "bad_mac"}, 400)
    if metric not in repo.DATA_TABLES:
        webio.send_json({"error": "bad_metric"}, 400)

    since_dt = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(int(time.time()) - hours * 3600))
    conn = get_conn()
    try:
        if not repo.can_view(conn, session, mac):
            webio.send_json({"error": "forbidden"}, 403)
        pts = repo.series(conn, mac, metric, key, since_dt)
    finally:
        conn.close()
    webio.send_json({"mac": mac, "metric": metric, "key": key, "points": pts or []})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
