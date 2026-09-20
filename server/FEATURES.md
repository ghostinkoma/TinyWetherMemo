# WetherLoggerBox サーバ — 機能仕様 (v1.0 正式版に向けて)

現状 **v1.0b**（実データE2E開通済／認証・履歴・監査あり）に対し、v1.0 では **メニュー再構成・不正アクセス防御・ユーザ/端末の管理UI・アクセスログ** を追加する。本書はその設計と **過不足レビュー／要決定事項** をまとめる。実装は「要決定事項」を確定後、フェーズ順で行う。

---
## 1. ロールと権限

| ロール | 値 | できること |
|---|---|---|
| guest | -1(新) | `guest_public=1` の端末を **閲覧のみ**。管理系は不可 |
| 参照のみ | 0 | 紐づく端末(`user_device_perm`)の閲覧のみ |
| 登録のみ | 1 | 上＋自分の端末の登録・アクティベート |
| 編集削除可 | 2 | 上＋端末情報の編集・(物理)削除 |
| アドミン | 3 | 全端末・**ユーザ登録**・**アクセスログ/BAN管理** |

- 端末ごとの権限は既存 `user_device_perm`（user_id, mac, role）を継続。
- guest は専用の組み込みアカウント（seed）1つ。パスワードは admin が設定/ローテート。

---
## 2. メニュー遷移 (情報設計)

```
[ログイン画面]
  ├─ ログイン (email/username + pass)
  └─ ゲストとして閲覧            ← guest アカウントでログイン

[ログイン後] ヘッダ ☰ メニュー
  ├─ 端末選択 (初期表示: 既定端末のダッシュボード)
  │    ダッシュボード内 ☰:
  │      ├─ ホーム        現在の最新値 + センサ異常表示 + 最終更新Datetime
  │      ├─ チャート      温度/湿度/気圧/雷を1グラフ重ね描き+表示選択(実装済)
  │      ├─ CSVダウンロード  サーバDBデータ(実装済)
  │      └─ 端末一覧へ戻る
  ├─ 端末一覧 (端末名のみ表示。MAC非表示。クリックで端末選択へ)
  ├─ 端末登録・変更 (role>=1 / 自分に紐づく端末のみ)
  │      ├─ 端末MACアドレス入力
  │      ├─ アクティベートキーをユーザのメールへ送信
  │      └─ ゲスト公開 チェックボックス (devices.guest_public)
  ├─ ユーザー登録 (role=3 のみ)
  │      ├─ 利用可能端末 (複数選択 → user_device_perm)
  │      ├─ メールアドレス / ユーザ名 / 親ユーザ選択
  │      └─ 認証コードをメール送付 → アクティベートで有効化
  ├─ アクセスログ (role=3 のみ)
  │      ├─ 禁止IPの設定 (banned_ip)
  │      └─ 禁止端末の設定 (banned_device)
  └─ ログアウト
```

> **過不足メモ**: 「端末選択(初期値表示)」と「端末一覧」は役割が重複気味。本設計では **端末一覧=一覧から選ぶ画面／端末選択=選択済み端末のダッシュボード** と定義して両立させる（要確認）。

---
## 3. DB 追加・変更

### 3.1 既存テーブルへの列追加
- `devices` / `devices_history`: **`guest_public TINYINT(1) NOT NULL DEFAULT 0`**（ゲスト公開）。
- `devices` / `devices_history`: **`activation_key_hash VARCHAR(255) NULL`**, **`activated TINYINT(1) NOT NULL DEFAULT 0`**, **`activation_expires DATETIME NULL`**（端末アクティベート。※下記「端末プロビジョニング方式」の決定次第）。
- `users` / `users_history`: **`activated TINYINT(1) NOT NULL DEFAULT 0`**, **`activation_code_hash VARCHAR(255) NULL`**, **`activation_expires DATETIME NULL`**（ユーザ有効化）。

### 3.2 新規テーブル
```
access_log        -- 全アクセス記録 (§5.1)
  log_id BIGINT PK AI, user_id BIGINT NULL, mac CHAR(17) NULL,
  ip VARCHAR(45), fwd_ip VARCHAR(45) NULL,       -- REMOTE_ADDR / X-Forwarded-For
  os VARCHAR(32) NULL, browser VARCHAR(32) NULL, -- UA から簡易パース
  user_agent VARCHAR(255) NULL, method VARCHAR(8), path VARCHAR(128),
  event VARCHAR(24),                              -- login_ok/login_ng/page/enroll/blocked 等
  status SMALLINT, created_at DATETIME,
  KEY(ip), KEY(created_at), KEY(user_id)

banned_ip         -- 禁止IP
  ip VARCHAR(45) PK, reason VARCHAR(128) NULL, created_by BIGINT NULL, created_at DATETIME

banned_device     -- 禁止端末(MAC)
  mac CHAR(17) PK, reason VARCHAR(128) NULL, created_by BIGINT NULL, created_at DATETIME
```
> access_log は履歴管理(_history)対象外（追記専用ログ）。保持は日数/件数でローテート運用（§5.1）。

---
## 4. API エンドポイント (追加)

人間UI `/api/`（要ログイン・権限チェック・**アクセスログ記録**・**BANチェック**を全経路で適用）:
- `guest_login.py` (POST) … ゲストログイン
- `users.py` (GET一覧/POST作成/PUT更新) role=3。作成時に認証コード生成→メール送信
- `user_activate.py` (POST) … 認証コードでユーザ有効化＋初回パスワード設定
- `mydevices.py` (GET) … 自分に紐づく端末（登録・変更用）
- `device_save.py` (POST) … 端末MAC登録/変更・guest_public・アクティベートキー発行→メール
- `bans.py` (GET/POST/DELETE) role=3 … 禁止IP/禁止端末の CRUD
- `accesslog.py` (GET) role=3 … アクセスログ閲覧（ページング/フィルタ）
- 既存: session/login/logout/devices/latest/series/csv（devices は guest 用に guest_public フィルタ対応）

端末 `/webapi/`:
- `enroll.py` … **端末ごとのアクティベートキー**で登録（§6.2 の決定次第）。ingest は現状維持＋**banned_device チェック**追加。

---
## 5. セキュリティ (不正アクセス対策)

### 5.1 アクセスログ
- 全 `/api/`・`/webapi/` リクエストで記録: IP(REMOTE_ADDR)、X-Forwarded-For、UAから **OS区分/ブラウザ名** を簡易パース、時刻、method/path、event、status。
- **記録スコープ（要決定）**: `latest.py` は30秒ポーリングのため全記録すると肥大。→ **認証イベント(login_ok/ng)・ページ遷移・enroll・blocked・管理操作のみ記録**を推奨（高頻度pollは除外 or サンプリング）。
- 保持: 例）90日 or 100万件でローテート（古いものから削除）。

### 5.2 IP / 端末 BAN
- 全 `/api/`・`/webapi/` の**入口で** `banned_ip`(クライアントIP) と `banned_device`(mac) を照合し、該当なら 403＋`event=blocked` 記録。
- **クライアントIPの決定（要決定）**: 共有ホスティングはプロキシ経由のことがあり `REMOTE_ADDR` がプロキシになる場合あり。→ `X-Forwarded-For` 先頭を優先、無ければ `REMOTE_ADDR`（信頼するプロキシ前提）。両方を access_log に保存。
- （推奨追加）ログイン失敗レート制限: 同一IPで N回/分 失敗→一時ブロック（access_log 集計 or メモリ）。

### 5.3 その他（既存＋強化）
- セッション署名Cookie(HttpOnly/SameSite=Strict、本番Secure)、PBKDF2、SQLi遮断(プレースホルダ)。
- 本番: `require_https=true`、`debug=false`、admin/enroll/各シークレット再生成。

---
## 6. メール / アクティベーション

### 6.1 メール送信（要決定＝最大の依存）
- 案A: **Python smtplib + ロリポップSMTP**（config.local.py に smtp_host/user/pass）。ロジック層(Python)から送信＝一貫。
- 案B: **PHP `mail()`** の薄い内部エンドポイントを Python から呼ぶ（Lolipop で mail() は概ね可）。
- 案C: **当面メール無し**＝認証コードを admin 画面に表示し口頭/手動連絡（v1.0後にメール化）。
- 推奨: まず **案C で骨組み**→ 動作確認後に **案A** を追加（SMTP情報が要る）。

### 6.2 端末プロビジョニング方式（要決定＝現行を変える）
- 現行: **共有 enroll_code**（全端末共通）→ token=HMAC(secret,mac)。
- 新案(推奨): **端末ごとのアクティベートキー**。ユーザが端末MACを登録→サーバが per-device キー生成→**オーナーのメールへ送信**→端末UIにキー入力→`enroll` が per-device キー(ハッシュ照合)＋期限で検証→token発行。共有シークレット漏洩リスクを排除。
  - 影響: `enroll.py` 改修、`devices.activation_key_hash/activated/expires` 追加、デバイス側 `srvCode` の意味が「per-deviceキー」に。移行時は既存端末を再アクティベート。

### 6.3 ユーザ アクティベーション
- admin がユーザ作成→認証コード生成→メール→ユーザが `user_activate` でコード入力＋パスワード設定→`activated=1`。未有効化はログイン不可。

---
## 7. センサ異常判定 (ホーム表示)
- **stale（最終更新が古い）**: `now - last_data_at > 閾値`（例 10分＝分足PUSHの数回分）→「更新途絶」。
- **欠測/異常値**: 最新 value が NULL、または各センサのレンジ外（温度 -40..85℃ 等）→「異常」。
- ホームに「最終更新: <datetime> / 状態: 正常|更新途絶|異常」を表示。閾値・レンジは要決定（既定案あり）。

---
## 8. 過不足レビュー（現時点の指摘）
- **重複**: メニューの「端末選択」と「端末一覧」→ §2 の定義で整理（要確認）。
- **不足→追加**: ユーザ/端末の **論理削除UI**、パスワード変更UI（現状seedのみ）、ログイン失敗レート制限、access_log ローテート運用、`guest_public` の history 追随。
- **依存**: メール送信はホスティング依存が大きく、v1.0の律速。案C→案A段階導入を推奨。
- **整合**: 端末プロビジョニングを per-device 化すると現行の共有 enroll_code は廃止。デバイス側UI/設定の意味変更とドキュメント更新が必要。
- **プライバシ**: access_log に IP/UA を保存＝個人情報。保持期間・閲覧権限(admin限定)・削除方針を明記（本書に記載済）。

---
## 9. 実装フェーズ計画（決定後）
1. **DBマイグレーション**: guest_public / activation列 / access_log / banned_ip / banned_device。
2. **セキュリティ土台**: webio に「BANチェック＋アクセスログ記録」を全経路適用。guest seed。
3. **メニュー再構成(UI)**: §2 の遷移。ホームのセンサ異常表示。端末一覧(名称のみ)。
4. **管理UI**: ユーザ登録(admin)・端末登録/変更・BAN管理・アクセスログ閲覧。
5. **アクティベーション/メール**: 案C(表示)→案A(SMTP)。端末 per-device キー化。
6. 仕上げ: パスワード変更UI・レート制限・保持ローテート・本番シークレット。

---
## 10. 要決定事項（実装前に確定したい）
1. **メール送信**: 案A(Python SMTP) / 案B(PHP mail) / 案C(当面表示のみ)。
2. **端末プロビジョニング**: per-deviceアクティベートキー(推奨) / 現行の共有enroll_code維持。
3. **アクセスログ範囲**: 認証+ページ+管理のみ(推奨) / 全リクエスト。
4. **ゲスト**: 組み込みguestアカウントで閲覧(推奨) / ログイン無しの公開ビュー。
5. （補助）クライアントIPは X-Forwarded-For 優先でよいか（共有ホスト構成）。
6. （補助）センサ異常の閾値（stale=10分／温度レンジ 等）。

---
## 11. 決定事項（確定 2026-09-14）と実装状況
- メール = **Python smtplib + ロリポップSMTP**（要SMTP資格情報→未設定のため関連機能は保留）
- 端末認証 = **端末ごとアクティベートキー**（enroll改修＝メールフェーズで実施）
- アクセスログ範囲 = **認証/ページ/管理のみ**（高頻度pollは除外）＋ **IP集計⇄時系列⇄明細** 切替、**IPクリックでWHOIS(ip-api.com, ip_geo_cache 30日)**
- ゲスト = **パス無し**・`guest_public` 端末のみ閲覧・遷移は一般ユーザと同じ（**ログアウト/CSV除く**）
- クライアントIP = X-Forwarded-For 優先→REMOTE_ADDR。センサ異常 = 最終更新>10分で「更新途絶」/欠測で「異常」。

### 実装済み（本コミット）
- DB: `migrate_2026-09-14_v1_security.sql`（guest_public/activation列・access_log・banned_ip・banned_device・ip_geo_cache）。schema.sqlにも反映。
- ロジック: webio(gate/client_ip/parse_ua/require_admin) / repo(log_access・ban CRUD・accesslog集計・geoキャッシュ・guest権限・device_set_meta)。
- API: guest.py / accesslog.py / bans.py / whois.py / mydevices.py / device_save.py。既存は全経路にBANゲート＋選択ログを付与。
- UI: index.php を v1.0メニュー(権限別ナビ/ゲスト/端末一覧[名称のみ]/端末登録・変更[ゲスト公開]/アクセスログ[3ビュー+WHOIS+ワンクリックIP禁止]/BAN管理/ホームのセンサ異常表示)。

### 保留（SMTP設定後）
- ユーザー登録(admin, 認証コードメール→アクティベート)、端末アクティベートキーのメール発行＆enroll改修、パスワード変更UI、ログイン失敗レート制限、access_log保持ローテート。

---
## 12. メールフェーズ実装 (2026-09-14, SMTP設定後)
- SMTP=`smtp.lolipop.jp:465(SSL)` / from=`wether@happykoma.tech`。config.local.py の `smtp.password` にメールアカウントのパスワードを記入して有効化。
- `logic/mailer.py`(smtplib SMTP_SSL)。
- **ユーザー登録(admin)**: `api/users.py`(GET一覧+端末候補/POST作成→認証コード48hをメール, メール不可時はcodeをadminへ返す)。`api/user_activate.py`(email+code+新パスワードで有効化, 未有効化はログイン不可)。
- **端末アクティベートキー**: `api/device_register.py`(MAC登録→per-deviceキー7日をオーナーへメール, owner権限付与)。`webapi/enroll.py`改修→`repo.enroll_verify`(per-deviceキー優先, 無ければ共有enroll_code=移行fallback)。既存の稼働端末はtoken保持のため無影響。
- **パスワード変更**: `api/passwd.py`(自分の旧→新, 8文字以上)。
- UI(index.php): ログインに『アカウント有効化』フォーム、ユーザー登録ページ(役割/親/端末複数選択)、端末新規登録(キーメール)、パスワード変更ページ。
- デプロイ: server一式アップ→`sh tools/deploy_fix.sh`(新.py多数)→config.local.pyにSMTPパス。アクティベート列は v1_security マイグレ済で追加不要。
