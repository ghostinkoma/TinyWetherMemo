# -*- coding: utf-8 -*-
# ============================================================================
#  config.local.py  -  ★実値。.gitignore 済み(絶対にコミット/公開しない)。
#                       Webルート(public/)の外に置く。
#  ※開発用の値。開発終了後 DBパスワードを変更し、下のシークレット3つを本番用に再生成。
#     生成: python3 -c "import secrets; print(secrets.token_hex(32))"
# ============================================================================
CONFIG = {
    "db": {
        "host": "mysql404.phy.lolipop.lan",
        "name": "LAA1614669-wether",
        "user": "LAA1614669",            # ★DB名の接頭辞と一致 (末尾9)。実機で疎通確認済み。
        "pass": "lovelove",              # 開発用。終了後に変更する。
        "charset": "utf8mb4",
    },

    # ★本番前に必ず再生成 (下は開発用の仮値)。
    "enroll_code": "dev-enroll-2026",
    "hmac_secret": "dev0000000000000000000000000000000000000000000000000000000000dev",
    "session_secret": "devsess000000000000000000000000000000000000000000000000000000dev",
    "session_ttl_sec": 86400,

    "require_https": False,              # 開発中 False。本番(SSL有効化後) True。
    "pbkdf2_iterations": 200000,
    "debug": True,                       # ★開発中のみ。API 500 応答に traceback を含める。本番は False/削除。

    # メール内リンクの土台 (アクティベート案内などに使用)
    "base_url": "https://wether.happykoma.tech",

    # ---- SMTP (ロリポップ / wether@happykoma.tech) ----
    #  ★password にメールアカウントのパスワードを記入してください(未設定だと送信不可)。
    "smtp": {
        "host": "smtp.lolipop.jp",
        "port": 465,
        "ssl": True,                     # 465=SSL/TLS(implicit)
        "user": "wether@happykoma.tech",
        "password": "Love1972-activate",
        "from": "wether@happykoma.tech",
        "from_name": "WetherLoggerBox",
    },
}
