#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/csv.py  -  サーバ蓄積データの CSV ダウンロード (要ログイン)  [CGI]
#  GET ?mac=... → text/csv (attachment)。列: daytime_utc,metric,sensor_key,value
#  ※ daytime は UTC。metric = temp/humidity/pressure/lightning。
# ============================================================================
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo, tokens         # noqa: E402
from logic.db import get_conn                    # noqa: E402


def _csv_field(v):
    s = "" if v is None else str(v)
    if any(c in s for c in [',', '"', '\n', '\r']):
        s = '"' + s.replace('"', '""') + '"'
    return s


def main():
    webio.require_https_or_die()
    session = webio.require_session_or_die()   # 未ログインは 401(JSON) で弾く
    mac = tokens.normalize_mac(webio.query().get("mac", ""))
    if not mac:
        webio.send_json({"error": "bad_mac"}, 400)

    conn = get_conn()
    try:
        if not repo.can_view(conn, session, mac):
            webio.send_json({"error": "forbidden"}, 403)
        rows = repo.csv_rows(conn, mac)
    finally:
        conn.close()

    fname = "wether_%s.csv" % mac.replace(":", "")
    out = sys.stdout
    out.write("Status: 200 OK\r\n")
    out.write("Content-Type: text/csv; charset=utf-8\r\n")
    out.write('Content-Disposition: attachment; filename="%s"\r\n' % fname)
    out.write("\r\n")
    out.write("daytime_utc,metric,sensor_key,value\r\n")
    for r in rows:
        out.write(",".join([
            _csv_field(r["daytime"]), _csv_field(r["metric"]),
            _csv_field(r["sensor_key"]), _csv_field(r["value"]),
        ]) + "\r\n")
    out.flush()
    sys.exit(0)


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
