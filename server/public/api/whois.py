#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  public/api/whois.py  -  IP の国/プロバイダ情報 (admin)  [CGI]
#  GET ?ip=1.2.3.4 → {ip, country, isp, org, asn, cached}
#  ・ip_geo_cache に30日キャッシュ。無ければ ip-api.com へ問合せ(無料/HTTP)。
#  ・外部呼び出しの失敗はキャッシュ or 空で返す(画面を止めない)。
# ============================================================================
import os
import sys
import re
import json
import time
import urllib.request
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from logic import webio, repo                   # noqa: E402
from logic.db import get_conn                    # noqa: E402

_IP = re.compile(r"^[0-9a-fA-F:.]{3,45}$")


def main():
    webio.require_https_or_die()
    s = webio.require_admin_or_die()
    ip = webio.query().get("ip", "").strip()
    if not _IP.match(ip):
        webio.send_json({"error": "bad_ip"}, 400)

    conn = get_conn()
    try:
        webio.gate(conn, event="admin", user_id=s["uid"])
        row = repo.geo_get(conn, ip)
        fresh = False
        if row and row.get("updated_at"):
            try:
                age = time.time() - time.mktime(time.strptime(str(row["updated_at"]), "%Y-%m-%d %H:%M:%S"))
                fresh = age < 30 * 86400
            except Exception:
                fresh = False
        if row and fresh:
            webio.send_json({"ip": ip, "country": row["country"], "isp": row["isp"],
                             "org": row["org"], "asn": row["asn"], "cached": True})
        # 外部問い合わせ (ip-api.com 無料エンドポイント)
        country = isp = org = asn = None
        try:
            url = "http://ip-api.com/json/%s?fields=status,country,isp,org,as" % urllib.parse.quote(ip)
            with urllib.request.urlopen(url, timeout=6) as r:
                j = json.loads(r.read().decode("utf-8"))
            if j.get("status") == "success":
                country, isp, org, asn = j.get("country"), j.get("isp"), j.get("org"), j.get("as")
                repo.geo_set(conn, ip, country, isp, org, asn)
                conn.commit()
        except Exception:
            if row:  # 失効キャッシュでも無いよりは返す
                country, isp, org, asn = row["country"], row["isp"], row["org"], row["asn"]
    finally:
        conn.close()
    webio.send_json({"ip": ip, "country": country, "isp": isp, "org": org, "asn": asn, "cached": False})


try:
    main()
except SystemExit:
    raise
except Exception:
    webio.send_error("server")
