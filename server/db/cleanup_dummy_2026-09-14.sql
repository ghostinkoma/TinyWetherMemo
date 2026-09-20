-- ============================================================================
--  cleanup_dummy_2026-09-14.sql  -  検証で投入したダミーデータの削除
-- ----------------------------------------------------------------------------
--  適用: mysql -h mysqlXXX.phy.lolipop.lan -u LAAxxxxxxx -p 'LAAxxxxxxx-wether' \
--          < db/cleanup_dummy_2026-09-14.sql
--
--  削除対象:
--   (1) テスト端末 02:00:00:00:be:ef (PIPELINE-TEST) を端末ごと完全削除。
--   (2) 実機 aa:bb:cc:dd:ee:ff に紛れ込んだ検証用ダミー(2026-09-14 05:00 UTC より前)。
--       ※実機の本物データは新しい時刻(05:00 UTC 以降)なので残る。
--       実測が 05:00 UTC 以降に入っていることを確認してから実行すること。
-- ============================================================================
SET FOREIGN_KEY_CHECKS = 1;

-- (1) テスト端末を完全削除 -------------------------------------------------
DELETE FROM `data_temperature`         WHERE mac='02:00:00:00:be:ef';
DELETE FROM `data_humidity`            WHERE mac='02:00:00:00:be:ef';
DELETE FROM `data_pressure`            WHERE mac='02:00:00:00:be:ef';
DELETE FROM `data_lightning`           WHERE mac='02:00:00:00:be:ef';
DELETE FROM `data_temperature_history` WHERE mac='02:00:00:00:be:ef';
DELETE FROM `data_humidity_history`    WHERE mac='02:00:00:00:be:ef';
DELETE FROM `data_pressure_history`    WHERE mac='02:00:00:00:be:ef';
DELETE FROM `data_lightning_history`   WHERE mac='02:00:00:00:be:ef';
DELETE FROM `devices_history`          WHERE mac='02:00:00:00:be:ef';
DELETE FROM `devices`                  WHERE mac='02:00:00:00:be:ef';

-- (2) 実機に紛れ込んだダミー(05:00 UTC より前)を削除 -----------------------
DELETE FROM `data_temperature`         WHERE mac='aa:bb:cc:dd:ee:ff' AND daytime < '2026-09-14 05:00:00';
DELETE FROM `data_humidity`            WHERE mac='aa:bb:cc:dd:ee:ff' AND daytime < '2026-09-14 05:00:00';
DELETE FROM `data_pressure`            WHERE mac='aa:bb:cc:dd:ee:ff' AND daytime < '2026-09-14 05:00:00';
DELETE FROM `data_lightning`           WHERE mac='aa:bb:cc:dd:ee:ff' AND daytime < '2026-09-14 05:00:00';
DELETE FROM `data_temperature_history` WHERE mac='aa:bb:cc:dd:ee:ff' AND daytime < '2026-09-14 05:00:00';
DELETE FROM `data_humidity_history`    WHERE mac='aa:bb:cc:dd:ee:ff' AND daytime < '2026-09-14 05:00:00';
DELETE FROM `data_pressure_history`    WHERE mac='aa:bb:cc:dd:ee:ff' AND daytime < '2026-09-14 05:00:00';
DELETE FROM `data_lightning_history`   WHERE mac='aa:bb:cc:dd:ee:ff' AND daytime < '2026-09-14 05:00:00';

-- 確認用:
-- SELECT mac, COUNT(*) FROM data_temperature GROUP BY mac;
