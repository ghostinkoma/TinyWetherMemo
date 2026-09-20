-- ============================================================================
--  migrate_2026-09-14_v1_security.sql  -  v1.0 セキュリティ/ゲスト/アクティベート
--  適用: mysql -h <host> -u <user> -p '<dbname>' < db/migrate_2026-09-14_v1_security.sql
--  冪等ではない(ADD COLUMN)。適用済み環境で再実行するとエラーになる列はスキップ可。
-- ============================================================================
SET NAMES utf8mb4;

-- ---- devices: ゲスト公開 + 端末アクティベート ----
ALTER TABLE `devices`
  ADD COLUMN `guest_public`        TINYINT(1)   NOT NULL DEFAULT 0,
  ADD COLUMN `activation_key_hash` VARCHAR(255) NULL,
  ADD COLUMN `activated`           TINYINT(1)   NOT NULL DEFAULT 1,   -- 既存端末は有効扱い
  ADD COLUMN `activation_expires`  DATETIME     NULL;
ALTER TABLE `devices` ADD KEY `idx_devices_guest` (`guest_public`);

ALTER TABLE `devices_history`
  ADD COLUMN `guest_public`        TINYINT(1)   NOT NULL DEFAULT 0,
  ADD COLUMN `activation_key_hash` VARCHAR(255) NULL,
  ADD COLUMN `activated`           TINYINT(1)   NOT NULL DEFAULT 1,
  ADD COLUMN `activation_expires`  DATETIME     NULL;

-- ---- users: アクティベート ----
ALTER TABLE `users`
  ADD COLUMN `activated`            TINYINT(1)   NOT NULL DEFAULT 1,   -- 既存ユーザは有効扱い
  ADD COLUMN `activation_code_hash` VARCHAR(255) NULL,
  ADD COLUMN `activation_expires`   DATETIME     NULL;
ALTER TABLE `users_history`
  ADD COLUMN `activated`            TINYINT(1)   NOT NULL DEFAULT 1,
  ADD COLUMN `activation_code_hash` VARCHAR(255) NULL,
  ADD COLUMN `activation_expires`   DATETIME     NULL;

-- ---- アクセスログ (追記専用。履歴管理なし) ----
CREATE TABLE IF NOT EXISTS `access_log` (
  `log_id`     BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `user_id`    BIGINT UNSIGNED NULL,
  `mac`        CHAR(17)     NULL,
  `ip`         VARCHAR(45)  NOT NULL,          -- クライアントIP(推定)
  `fwd_ip`     VARCHAR(45)  NULL,              -- X-Forwarded-For 生
  `os`         VARCHAR(32)  NULL,              -- UA から簡易判定
  `browser`    VARCHAR(32)  NULL,
  `user_agent` VARCHAR(255) NULL,
  `method`     VARCHAR(8)   NULL,
  `path`       VARCHAR(128) NULL,
  `event`      VARCHAR(24)  NULL,              -- login_ok/login_ng/page/enroll/blocked/guest/admin
  `status`     SMALLINT     NULL,
  `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`log_id`),
  KEY `idx_al_ip` (`ip`),
  KEY `idx_al_time` (`created_at`),
  KEY `idx_al_user` (`user_id`),
  KEY `idx_al_event` (`event`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---- 禁止IP ----
CREATE TABLE IF NOT EXISTS `banned_ip` (
  `ip`         VARCHAR(45)  NOT NULL,
  `reason`     VARCHAR(128) NULL,
  `created_by` BIGINT UNSIGNED NULL,
  `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---- 禁止端末(MAC) ----
CREATE TABLE IF NOT EXISTS `banned_device` (
  `mac`        CHAR(17)     NOT NULL,
  `reason`     VARCHAR(128) NULL,
  `created_by` BIGINT UNSIGNED NULL,
  `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`mac`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---- WHOIS/GeoIP キャッシュ (ip-api.com 結果を保持し外部呼を減らす) ----
CREATE TABLE IF NOT EXISTS `ip_geo_cache` (
  `ip`         VARCHAR(45)  NOT NULL,
  `country`    VARCHAR(64)  NULL,
  `isp`        VARCHAR(128) NULL,
  `org`        VARCHAR(128) NULL,
  `asn`        VARCHAR(64)  NULL,
  `updated_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
