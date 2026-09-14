# -*- coding: utf-8 -*-
# ============================================================================
#  config.example.py  -  設定テンプレート (公開OK。実値は入れない)
# ----------------------------------------------------------------------------
#  使い方: このファイルを config.local.py にコピーし実値を記入。
#          config.local.py は .gitignore 済み(絶対にコミットしない)。
#          Webルート(public/)の外に置くこと。
#
#  シークレット生成 (ロリポップSSH):
#    python3 -c "import secrets; print(secrets.token_hex(32))"
# ============================================================================
CONFIG = {
    # ---- MySQL 接続 (ロリポップ ビジネス) ----
    "db": {
        "host": "mysqlXXX.phy.lolipop.lan",
        "name": "LAAxxxxxxx-wether",     # ハイフン込み。PyMySQL の db= に渡す
        "user": "YOUR_DB_USER",
        "pass": "YOUR_DB_PASSWORD",
        "charset": "utf8mb4",
    },

    # ---- 端末⇄サーバ 認証 ----
    # enroll_code : 端末の「認証コード」欄に入れる共有シークレット (登録ゲート)。
    # hmac_secret : トークン導出鍵。token = HMAC-SHA256(hmac_secret, 正規化MAC)。
    "enroll_code": "CHANGE_ME_enroll_code",
    "hmac_secret": "CHANGE_ME_64hex_random",

    # ---- 人間ログインのセッション署名鍵 (Cookie 改ざん検知) ----
    "session_secret": "CHANGE_ME_64hex_random_2",
    "session_ttl_sec": 86400,            # 24h

    # ---- 動作 ----
    "require_https": True,               # 本番 True 推奨 (API/ログインを HTTPS 必須)
    "pbkdf2_iterations": 200000,

    # メール内リンクの土台
    "base_url": "https://YOUR_DOMAIN",

    # ---- SMTP (ロリポップ) ----
    "smtp": {
        "host": "smtp.lolipop.jp",
        "port": 465,
        "ssl": True,
        "user": "YOUR_MAIL@YOUR_DOMAIN",
        "password": "YOUR_MAIL_PASSWORD",
        "from": "YOUR_MAIL@YOUR_DOMAIN",
        "from_name": "WetherLoggerBox",
    },
}
