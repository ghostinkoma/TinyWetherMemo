# WetherLoggerBox — サーバ連携 (ロリポップ ビジネス / MySQL)

ESP32-C3 ロガー（WetherLoggerBox）の測定を、ロリポップ上の MySQL へ送って蓄積・閲覧するための **サーバ側一式**。デバイスはスタンドアロン（LittleFS ローカル記録）と **サーバ対応** を切替可能（デバイス側UIは別フェーズ）。

## アーキテクチャ（役割分担）
- **PHP = UI に専念**：`public/index.php` がログイン画面とダッシュボードの **HTML/CSS/JS を配信するだけ**。DB も秘密情報も持たない。
- **Python = ロジック層**：`logic/` ＋ CGI エンドポイント `public/api/*.py` が **DB アクセス・認証・バージョン管理・トークン** を担う。
- ブラウザの JS が Python API（`api/session.py` `api/login.py` `api/devices.py` `api/enroll.py` `api/ingest.py`）を fetch で呼ぶ。
- **DB**: `mysqlXXX.phy.lolipop.lan` / `LAAxxxxxxx-wether`（内部ホスト＝ロリポップ内からのみ到達可）。

## バージョン管理（現行 + 履歴）
- **現行テーブル**（`users` `devices` `user_device_perm` `data_temperature/humidity/lightning`）= **最新1行**。UI/API はここだけ参照。PK/FK/UNIQUE を維持。
- 変更時、旧行を **`*_history` へ退避**してから現行を更新（`version+1`）。**旧データは消えない**。物理削除も退避してから DELETE。直列化は現行行の `SELECT … FOR UPDATE`（`logic/db.py: versioned_write / hard_delete`）。
- **`*_history` は DB に直接アクセスできる管理者のみ**参照（アプリは触れない）。
- `deleted` は論理削除フラグ（**index 付き**＝データ量が多くても絞れる）。
- **監査（いつ・誰が）**: 全テーブルに `updated_by_user`（ユーザーID）/ `updated_by_mac`（端末MAC）。データ投入(ingest/enroll)は端末MAC、UIからの編集はユーザーID、seed 等は両NULL(system)。現行行＝最新版を書いた人、履歴行＝その旧版を書いた人。`updated_at`（いつ）と対で追跡。物理削除は履歴に削除実行者を記録。

## セキュリティ設計
- **SQLi**: 値は必ず `%s` プレースホルダ（PyMySQL）。テーブル/列名は `logic/db.py` 内の**固定ホワイトリスト**のみ。外部入力を識別子に使わない。
- **パスワード**: `PBKDF2-HMAC-SHA256`（`hashlib` 標準＝**C拡張ビルド不要**、bcrypt の venv ビルド問題を回避）。
- **人間セッション**: Python 発行の**署名Cookie**（HMAC 改ざん検知 + 期限）。`HttpOnly; SameSite=Strict`（＋本番 `Secure`）で CSRF を実質遮断。
- **端末認証**: `token = HMAC-SHA256(hmac_secret, 正規化MAC)`。サーバは MAC から再計算し `hmac.compare_digest` で定数時間照合＝**DB照合不要のステートレス**。失効は `devices.deleted=1`（ingest が現行 deleted を確認）。
- **秘密の非公開**: `config.local.py`（実DBパス/各シークレット）は **gitignore 済**。`logic/ db/ tools/ config.local.py` は `.htaccess` で直アクセス拒否。docroot は `server/public` 推奨。GitHub は **public** リポジトリ＝秘密厳禁。

## ディレクトリ
```
server/
├─ db/schema.sql                現行 + 履歴 + 中間テーブル DDL
├─ config.example.py            設定テンプレ(公開OK)
├─ config.local.py              ★実値(.gitignore)。Webルート外へ
├─ requirements.txt             PyMySQL
├─ logic/                       Python ロジック層(config/db/tokens/auth/webio/repo) ※直アクセス拒否
├─ tools/seed_admin.py          初期管理ユーザー投入(CLI)                        ※直アクセス拒否
└─ public/                      ★ドメインのドキュメントルート
   ├─ index.php                 管理画面SPA (ログイン→端末選択→ダッシュボード)
   ├─ api/                      人間UI用 CGI: session/login/devices/latest/series.py
   └─ webapi/                   端末(ESP32)用 CGI: enroll/ingest.py  ※URLを分離
```

## URL 分離 (セキュリティ) と ルート公開
- **ログイン/管理画面** = `https://your-domain.example/`（ルート）。`server/.htaccess` のリライトで `/`→`public/index.php`、`/api/*`→`public/api/*`、`/webapi/*`→`public/webapi/*` に内部転送（コンパネのドキュメントルート変更をしなくても `/` で動く。★可能なら公開フォルダ自体を `wether/public` にすると本リライト不要で最も安全）。
- **人間の管理API** = `/api/`（session/login/devices/latest/series）。SPA からは相対 `api/...`。
- **端末(ESP32)が叩くAPI** = `/webapi/`（enroll/ingest）。デバイスの「WebAPIルートパス」= **`/webapi`**、接続文字列 = `https://your-domain.example`。
- 端末用と管理用が別ディレクトリ＝URL不一致。将来 `webapi` のみ IP 制限や別サブドメイン化も容易。

## デプロイ手順（あなたが SSH で実行）
> `mysqlXXX.phy.lolipop.lan` は**ロリポップ内部からのみ**到達可。以下はロリポップの SSH 上で行います。

### 1) 配置 & venv
`server/` を転送し、**ドキュメントルートを `server/public`** に設定。Python モジュールはルートに置けないため **venv** を作成：
```bash
cd server
python3 -m venv venv
./venv/bin/pip install -r requirements.txt
```
CGI が使う Python を venv にするため、`public/api/*.py`・`public/webapi/*.py`・`tools/seed_admin.py` の **shebang を venv の python 絶対パスに**変更（ロリポップCGIは PATH に venv を通さないため）。**アップロードの度に必要**なので同梱スクリプトを使うと楽：
```bash
sh tools/deploy_fix.sh          # shebang を venv に固定 + public/{api,webapi}/*.py に chmod 705
# 手動なら:
# VENV_PY="$(pwd)/venv/bin/python3"
# sed -i "1s|.*|#!$VENV_PY|" public/api/*.py public/webapi/*.py tools/seed_admin.py
# chmod 705 public/api/*.py public/webapi/*.py
```
> ※ ロリポップの Python CGI 有効化方法（配置場所・`.htaccess`・実行権限）は管理画面/マニュアルに従って調整してください。`public/api/.htaccess` と `public/webapi/.htaccess` に `AddHandler cgi-script .py` を用意済み。

### 2) 設定
```bash
cp config.example.py config.local.py        # まだ無ければ
python3 -c "import secrets; print(secrets.token_hex(32))"   # hmac_secret / session_secret 用に2回
# config.local.py を編集: db.user/db.pass, enroll_code, hmac_secret, session_secret, require_https
```

### 3) テーブル作成
```bash
mysql -h mysqlXXX.phy.lolipop.lan -u <DBUSER> -p 'LAAxxxxxxx-wether' < db/schema.sql
```

### 4) 初期管理ユーザー
```bash
./venv/bin/python3 tools/seed_admin.py      # admin / wether / role3
```

### 5) 動作確認
- ブラウザで `https://your-domain.example/` → **admin / wether** でログイン。
- API 疎通:
```bash
# enroll (端末用API = /webapi/。code = config.local.py の enroll_code)
curl -sS -X POST https://your-domain.example/webapi/enroll.py \
  -H 'Content-Type: application/json' \
  -d '{"mac":"aa:bb:cc:dd:ee:ff","code":"<enroll_code>","name":"WetherMemo"}'
# → {"token":"<hex64>","mac":"aa:bb:cc:dd:ee:ff"}

# ingest (上の token を Bearer に)
curl -sS -X POST https://your-domain.example/webapi/ingest.py \
  -H "Authorization: Bearer <token>" -H 'Content-Type: application/json' \
  -d '{"mac":"aa:bb:cc:dd:ee:ff","ts":'"$(date +%s)"',
       "temp":[{"key":"air","value":25.3},{"key":"die","value":41.2}],
       "humidity":[{"key":"main","value":55.1}],
       "lightning":[{"key":"danger","value":7}]}'
# → {"ok":1,"inserted":4}
```

## API 仕様
### 端末(ESP32)用 = `/public/webapi/`
#### `POST /webapi/enroll.py`
- 入力 `{mac, code, name?, start?}` → `200 {token, mac}` / `401 bad_code` / `400 bad_mac`
#### `POST /webapi/ingest.py`
- ヘッダ `Authorization: Bearer <token>`
- 入力 `{mac, ts?, temp[], humidity[], pressure[], lightning[]}`。各要素 `{key?, value, ts?}`（`key`=チャンネル: 温度 air/die/water、雷 danger/nearest 等。`ts`=unix秒UTC）
- 出力 `200 {ok:1, inserted:N}` / `401 unauthorized` / `403 device_disabled`
- 保存: `(mac, sensor_key, daytime)` 現行 UPSERT。**同一点の再送は旧値を履歴へ退避**して更新（version+1）。`daytime` は UTC。

### 人間UI用（Cookie セッション） = `/public/api/`
- `POST /api/login.py {login,pass}` → セッションCookie発行 / `POST /api/session.py`=ログアウト / `GET /api/session.py`=whoami
- `GET /api/devices.py` = 端末一覧（admin=全端末、他=`user_device_perm` 許可分）＋件数/最終時刻
- `GET /api/latest.py?mac=` = 端末の最新値（チャンネル毎。温度/湿度/気圧/雷）
- `GET /api/series.py?mac=&metric=temp|humidity|pressure|lightning&key=air&hours=24` = 時系列（チャート用）
- いずれも要ログイン。非adminは `user_device_perm` で許可された端末のみ閲覧可。

## トラブルシュート
- **`'cryptography' package is required ...`**: ロリポップの MySQL は 8.0 系（caching_sha2_password）。venv に `cryptography` を入れる → `./venv/bin/pip install cryptography`（requirements.txt に記載済）。
- **`/` や `/api/...` が 404**: ドメインの公開(ドキュメント)フォルダが `wether/` 直下になっている可能性。**公開フォルダを `wether/public` に変更**する（推奨）。暫定確認は `/public/` 経由で到達可否をみる。
- **`.py` のソースがそのまま返る**: Python CGI が未有効。ロリポップの Python 有効化（配置場所・`AddHandler cgi-script .py`・実行権限 `chmod 705`・shebang=venv絶対パス）を確認。
- **再アップロード後に全API/ログインが壊れた**: アップロードで `public/api/*.py` の shebang(1行目)が `#!/usr/bin/env python3` に戻り実行権限も外れる → CGI が venv 外 python(依存なし)で起動して失敗。**アップロードの度に `sh tools/deploy_fix.sh` を実行**（shebang を venv に固定＋`chmod 705`）。手動なら:
  `VENV_PY="$(pwd)/venv/bin/python3"; sed -i "1s|.*|#!$VENV_PY|" public/api/*.py tools/seed_admin.py; chmod 705 public/api/*.py`
- **`config.local.py` が HTTP で見える(200)**: 公開フォルダが `wether/` 直下＝危険。`wether/public` へ docroot を移す（秘密が公開フォルダ外になる）。

## 既知のトレードオフ / 運用メモ
- 履歴方式は書込時に2テーブル触る（退避→更新）。收集データは通常 version=1（新規点は退避なし）で、履歴が増えるのは**再送/訂正時のみ**。
- 端末トークンは決定論値（MAC 起点）。個別ローテートは不可（＝失効は `devices.deleted`）。全体更新は `hmac_secret` 変更。
- `enroll_code` は共有シークレット。漏洩時は再生成し端末を再登録。必要ならレート制限を前段(Web設定)で。
- **本番前**: `hmac_secret`/`session_secret`/`enroll_code` を本番用に再生成、`require_https=true`、開発用 DBパスワードを変更、admin パスワード変更。
