-- ============================================================================
--  schema.sql  -  WetherLoggerBox サーバ連携 スキーマ (MySQL / InnoDB / utf8mb4)
-- ----------------------------------------------------------------------------
--  対象: ロリポップ ビジネス MySQL (mysqlXXX.phy.lolipop.lan / DB=LAAxxxxxxx-wether)
--  適用: mysql -h mysqlXXX.phy.lolipop.lan -u <DBUSER> -p 'LAAxxxxxxx-wether' < schema.sql
--    ※ DB名にハイフンを含むため CREATE DATABASE/USE は書かず、-D 選択済DBへ CREATE TABLE のみ。
--
--  ★バージョン管理: 「現行テーブル + 履歴テーブル(_history)」。
--   - 現行=最新1行/論理キー。通常のPK/FK/UNIQUE維持。UI/APIはここだけ参照。
--   - 変更時: 旧行を _history へ退避してから現行を UPDATE(version+1) → 旧データは残る。
--   - 物理削除: 旧行を _history へ退避してから現行 DELETE。論理削除: deleted=1(退避あり)。
--   - _history は「DB直アクセス管理者のみ」参照。直列化は SELECT..FOR UPDATE (logic/db.py)。
--
--  ★監査(いつ・誰が): 全テーブルに更新者2カラム。
--   - updated_by_user : 更新者ユーザーID (NULL可)
--   - updated_by_mac  : 更新者端末MAC  (NULL可)   ※両方NULL = system(seed等)
--   - 現行行=最新版を書いた人 / 履歴行=その旧版を書いた人。updated_at(いつ)と対で追跡。
--
--  収集データ: 温度/湿度/雷の3テーブル。各 sensor_key で1端末マルチCH
--  (温度=air/die/water、雷=danger/nearest)。PK=(mac,sensor_key,daytime)。
-- ============================================================================
SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 1;

-- ===========================================================================
--  ① users (現行)
-- ===========================================================================
CREATE TABLE IF NOT EXISTS `users` (
  `user_id`           BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
  `username`          VARCHAR(64)      NOT NULL,
  `email`             VARCHAR(255)     NOT NULL,
  `password_hash`     VARCHAR(255)     NOT NULL,               -- PBKDF2-HMAC-SHA256 (logic/auth.py)
  `role`              TINYINT UNSIGNED NOT NULL DEFAULT 0,     -- 0=参照 1=登録 2=編集削除(物理可) 3=アドミン
  `parent_user_id`    BIGINT UNSIGNED  NULL,
  `billing_confirmed` TINYINT(1)       NOT NULL DEFAULT 0,
  `version`           INT UNSIGNED     NOT NULL DEFAULT 1,
  `created_at`        DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`        DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `deleted`           TINYINT(1)       NOT NULL DEFAULT 0,
  `updated_by_user`   BIGINT UNSIGNED  NULL,                   -- 更新者(ユーザーID)
  `updated_by_mac`    CHAR(17)         NULL,                   -- 更新者(端末MAC) ※両NULL=system
  PRIMARY KEY (`user_id`),
  UNIQUE KEY `uq_users_email` (`email`),
  KEY `idx_users_deleted` (`deleted`),
  KEY `idx_users_parent`  (`parent_user_id`),
  CONSTRAINT `fk_users_parent` FOREIGN KEY (`parent_user_id`)
    REFERENCES `users` (`user_id`) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `users_history` (
  `h_id`              BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
  `user_id`           BIGINT UNSIGNED  NOT NULL,
  `username`          VARCHAR(64)      NOT NULL,
  `email`             VARCHAR(255)     NOT NULL,
  `password_hash`     VARCHAR(255)     NOT NULL,
  `role`              TINYINT UNSIGNED NOT NULL,
  `parent_user_id`    BIGINT UNSIGNED  NULL,
  `billing_confirmed` TINYINT(1)       NOT NULL,
  `version`           INT UNSIGNED     NOT NULL,
  `created_at`        DATETIME         NOT NULL,
  `updated_at`        DATETIME         NOT NULL,
  `deleted`           TINYINT(1)       NOT NULL,
  `updated_by_user`   BIGINT UNSIGNED  NULL,
  `updated_by_mac`    CHAR(17)         NULL,
  `archived_at`       DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`h_id`),
  KEY `idx_uh_key` (`user_id`, `version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ===========================================================================
--  ② devices (現行)
-- ===========================================================================
CREATE TABLE IF NOT EXISTS `devices` (
  `mac`             CHAR(17)         NOT NULL,                 -- 正規化 "aa:bb:cc:dd:ee:ff"
  `device_name`     VARCHAR(64)      NULL,
  `owner_user_id`   BIGINT UNSIGNED  NULL,
  `start_date`      DATE             NULL,
  `temp_sensor`     VARCHAR(32)      NULL,
  `temp_offset`     DECIMAL(6,3)     NOT NULL DEFAULT 0.000,
  `humi_sensor`     VARCHAR(32)      NULL,
  `humi_offset`     DECIMAL(6,3)     NOT NULL DEFAULT 0.000,
  `ln_sensor`       VARCHAR(32)      NULL,
  `ln_config`       TEXT             NULL,                     -- JSON文字列 (アプリで json 検証)
  `info_updated_at` DATETIME         NULL,
  `last_data_at`    DATETIME         NULL,
  `version`         INT UNSIGNED     NOT NULL DEFAULT 1,
  `created_at`      DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`      DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `deleted`         TINYINT(1)       NOT NULL DEFAULT 0,
  `updated_by_user` BIGINT UNSIGNED  NULL,
  `updated_by_mac`  CHAR(17)         NULL,
  PRIMARY KEY (`mac`),
  KEY `idx_devices_deleted` (`deleted`),
  KEY `idx_devices_owner`   (`owner_user_id`),
  CONSTRAINT `fk_devices_owner` FOREIGN KEY (`owner_user_id`)
    REFERENCES `users` (`user_id`) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `devices_history` (
  `h_id`            BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
  `mac`             CHAR(17)         NOT NULL,
  `device_name`     VARCHAR(64)      NULL,
  `owner_user_id`   BIGINT UNSIGNED  NULL,
  `start_date`      DATE             NULL,
  `temp_sensor`     VARCHAR(32)      NULL,
  `temp_offset`     DECIMAL(6,3)     NOT NULL,
  `humi_sensor`     VARCHAR(32)      NULL,
  `humi_offset`     DECIMAL(6,3)     NOT NULL,
  `ln_sensor`       VARCHAR(32)      NULL,
  `ln_config`       TEXT             NULL,
  `info_updated_at` DATETIME         NULL,
  `last_data_at`    DATETIME         NULL,
  `version`         INT UNSIGNED     NOT NULL,
  `created_at`      DATETIME         NOT NULL,
  `updated_at`      DATETIME         NOT NULL,
  `deleted`         TINYINT(1)       NOT NULL,
  `updated_by_user` BIGINT UNSIGNED  NULL,
  `updated_by_mac`  CHAR(17)         NULL,
  `archived_at`     DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`h_id`),
  KEY `idx_dh_key` (`mac`, `version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ===========================================================================
--  ③ user_device_perm (中間: ユーザー端末権限) 現行
-- ===========================================================================
CREATE TABLE IF NOT EXISTS `user_device_perm` (
  `user_id`         BIGINT UNSIGNED  NOT NULL,
  `mac`             CHAR(17)         NOT NULL,
  `role`            TINYINT UNSIGNED NOT NULL DEFAULT 0,       -- 端末ごとのロール(0..3)
  `version`         INT UNSIGNED     NOT NULL DEFAULT 1,
  `created_at`      DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,  -- 登録年月日
  `updated_at`      DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP, -- 最終変更年月日
  `deleted`         TINYINT(1)       NOT NULL DEFAULT 0,
  `updated_by_user` BIGINT UNSIGNED  NULL,
  `updated_by_mac`  CHAR(17)         NULL,
  PRIMARY KEY (`user_id`, `mac`),
  KEY `idx_perm_deleted` (`deleted`),
  KEY `idx_perm_mac` (`mac`),
  CONSTRAINT `fk_perm_user` FOREIGN KEY (`user_id`)
    REFERENCES `users` (`user_id`) ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT `fk_perm_dev`  FOREIGN KEY (`mac`)
    REFERENCES `devices` (`mac`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `user_device_perm_history` (
  `h_id`            BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
  `user_id`         BIGINT UNSIGNED  NOT NULL,
  `mac`             CHAR(17)         NOT NULL,
  `role`            TINYINT UNSIGNED NOT NULL,
  `version`         INT UNSIGNED     NOT NULL,
  `created_at`      DATETIME         NOT NULL,
  `updated_at`      DATETIME         NOT NULL,
  `deleted`         TINYINT(1)       NOT NULL,
  `updated_by_user` BIGINT UNSIGNED  NULL,
  `updated_by_mac`  CHAR(17)         NULL,
  `archived_at`     DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`h_id`),
  KEY `idx_ph_key` (`user_id`, `mac`, `version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ===========================================================================
--  ④ 収集データ (温度/湿度/雷) 現行 + 履歴  ※value以外は共通形
-- ===========================================================================
-- ---- 温度 ----
CREATE TABLE IF NOT EXISTS `data_temperature` (
  `mac`             CHAR(17)      NOT NULL,
  `sensor_key`      VARCHAR(32)   NOT NULL DEFAULT 'main',
  `daytime`         DATETIME      NOT NULL,                     -- UTC 保存
  `value`           DECIMAL(7,3)  NULL,
  `version`         INT UNSIGNED  NOT NULL DEFAULT 1,
  `created_at`      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `deleted`         TINYINT(1)    NOT NULL DEFAULT 0,
  `updated_by_user` BIGINT UNSIGNED NULL,
  `updated_by_mac`  CHAR(17)      NULL,
  PRIMARY KEY (`mac`, `sensor_key`, `daytime`),
  KEY `idx_temp_deleted`  (`deleted`),
  KEY `idx_temp_mac_time` (`mac`, `daytime`),
  CONSTRAINT `fk_temp_dev` FOREIGN KEY (`mac`)
    REFERENCES `devices` (`mac`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `data_temperature_history` (
  `h_id`            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `mac`             CHAR(17)      NOT NULL,
  `sensor_key`      VARCHAR(32)   NOT NULL,
  `daytime`         DATETIME      NOT NULL,
  `value`           DECIMAL(7,3)  NULL,
  `version`         INT UNSIGNED  NOT NULL,
  `created_at`      DATETIME      NOT NULL,
  `updated_at`      DATETIME      NOT NULL,
  `deleted`         TINYINT(1)    NOT NULL,
  `updated_by_user` BIGINT UNSIGNED NULL,
  `updated_by_mac`  CHAR(17)      NULL,
  `archived_at`     DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`h_id`),
  KEY `idx_th_key` (`mac`, `sensor_key`, `daytime`, `version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---- 湿度 ----
CREATE TABLE IF NOT EXISTS `data_humidity` (
  `mac`             CHAR(17)      NOT NULL,
  `sensor_key`      VARCHAR(32)   NOT NULL DEFAULT 'main',
  `daytime`         DATETIME      NOT NULL,
  `value`           DECIMAL(7,3)  NULL,
  `version`         INT UNSIGNED  NOT NULL DEFAULT 1,
  `created_at`      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `deleted`         TINYINT(1)    NOT NULL DEFAULT 0,
  `updated_by_user` BIGINT UNSIGNED NULL,
  `updated_by_mac`  CHAR(17)      NULL,
  PRIMARY KEY (`mac`, `sensor_key`, `daytime`),
  KEY `idx_humi_deleted`  (`deleted`),
  KEY `idx_humi_mac_time` (`mac`, `daytime`),
  CONSTRAINT `fk_humi_dev` FOREIGN KEY (`mac`)
    REFERENCES `devices` (`mac`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `data_humidity_history` (
  `h_id`            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `mac`             CHAR(17)      NOT NULL,
  `sensor_key`      VARCHAR(32)   NOT NULL,
  `daytime`         DATETIME      NOT NULL,
  `value`           DECIMAL(7,3)  NULL,
  `version`         INT UNSIGNED  NOT NULL,
  `created_at`      DATETIME      NOT NULL,
  `updated_at`      DATETIME      NOT NULL,
  `deleted`         TINYINT(1)    NOT NULL,
  `updated_by_user` BIGINT UNSIGNED NULL,
  `updated_by_mac`  CHAR(17)      NULL,
  `archived_at`     DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`h_id`),
  KEY `idx_hh_key` (`mac`, `sensor_key`, `daytime`, `version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---- 気圧 ----
CREATE TABLE IF NOT EXISTS `data_pressure` (
  `mac`             CHAR(17)      NOT NULL,
  `sensor_key`      VARCHAR(32)   NOT NULL DEFAULT 'main',
  `daytime`         DATETIME      NOT NULL,
  `value`           DECIMAL(9,3)  NULL,                          -- 取得気圧値 [hPa]
  `version`         INT UNSIGNED  NOT NULL DEFAULT 1,
  `created_at`      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `deleted`         TINYINT(1)    NOT NULL DEFAULT 0,
  `updated_by_user` BIGINT UNSIGNED NULL,
  `updated_by_mac`  CHAR(17)      NULL,
  PRIMARY KEY (`mac`, `sensor_key`, `daytime`),
  KEY `idx_pres_deleted`  (`deleted`),
  KEY `idx_pres_mac_time` (`mac`, `daytime`),
  CONSTRAINT `fk_pres_dev` FOREIGN KEY (`mac`)
    REFERENCES `devices` (`mac`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `data_pressure_history` (
  `h_id`            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `mac`             CHAR(17)      NOT NULL,
  `sensor_key`      VARCHAR(32)   NOT NULL,
  `daytime`         DATETIME      NOT NULL,
  `value`           DECIMAL(9,3)  NULL,
  `version`         INT UNSIGNED  NOT NULL,
  `created_at`      DATETIME      NOT NULL,
  `updated_at`      DATETIME      NOT NULL,
  `deleted`         TINYINT(1)    NOT NULL,
  `updated_by_user` BIGINT UNSIGNED NULL,
  `updated_by_mac`  CHAR(17)      NULL,
  `archived_at`     DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`h_id`),
  KEY `idx_presh_key` (`mac`, `sensor_key`, `daytime`, `version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---- 雷 ----
CREATE TABLE IF NOT EXISTS `data_lightning` (
  `mac`             CHAR(17)      NOT NULL,
  `sensor_key`      VARCHAR(32)   NOT NULL DEFAULT 'danger',
  `daytime`         DATETIME      NOT NULL,
  `value`           DECIMAL(10,3) NULL,
  `version`         INT UNSIGNED  NOT NULL DEFAULT 1,
  `created_at`      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `deleted`         TINYINT(1)    NOT NULL DEFAULT 0,
  `updated_by_user` BIGINT UNSIGNED NULL,
  `updated_by_mac`  CHAR(17)      NULL,
  PRIMARY KEY (`mac`, `sensor_key`, `daytime`),
  KEY `idx_ln_deleted`  (`deleted`),
  KEY `idx_ln_mac_time` (`mac`, `daytime`),
  CONSTRAINT `fk_ln_dev` FOREIGN KEY (`mac`)
    REFERENCES `devices` (`mac`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `data_lightning_history` (
  `h_id`            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `mac`             CHAR(17)      NOT NULL,
  `sensor_key`      VARCHAR(32)   NOT NULL,
  `daytime`         DATETIME      NOT NULL,
  `value`           DECIMAL(10,3) NULL,
  `version`         INT UNSIGNED  NOT NULL,
  `created_at`      DATETIME      NOT NULL,
  `updated_at`      DATETIME      NOT NULL,
  `deleted`         TINYINT(1)    NOT NULL,
  `updated_by_user` BIGINT UNSIGNED NULL,
  `updated_by_mac`  CHAR(17)      NULL,
  `archived_at`     DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`h_id`),
  KEY `idx_lh_key` (`mac`, `sensor_key`, `daytime`, `version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
