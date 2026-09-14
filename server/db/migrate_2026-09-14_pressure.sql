-- ============================================================================
--  migrate_2026-09-14_pressure.sql  -  気圧テーブル追加 (既存DBへの差分)
--  適用: mysql -h mysqlXXX.phy.lolipop.lan -u LAAxxxxxxx -p 'LAAxxxxxxx-wether' \
--          < db/migrate_2026-09-14_pressure.sql
--  ※ 既存テーブルは変更しない (CREATE TABLE IF NOT EXISTS のみ)。冪等。
-- ============================================================================
SET NAMES utf8mb4;

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
