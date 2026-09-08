# Docs/TEST_LOG — 実機テストログ格納庫

WetherLoggerBox(ESP32-C3ホスト)+ ThunderSence(CH32V003ブリッジ)の**実機検証ログ**を日付別に格納する。

| ファイル | 内容 |
|---|---|
| [WetherLoggerBox_2026-09-08.md](WetherLoggerBox_2026-09-08.md) | 分足/時間足、int16圧縮、CSV(NTP/AHT20)、FS長期履歴、認証(SHA-256/RNG)、AES設定暗号、HTTPS(EC P-256)併設、Chart.jsローカル、省電力(モデムスリープ/CPU80MHz/CH32 24MHz)、雷poll(389B)修正+スパーク試験、CH32書込問題(WCH-LinkE v2.17) の実測ログ |

> 関連: CH32V003 ブリッジ単体の初期実機ログは [../../TESTLOG.md](../../TESTLOG.md)(リポジトリ直下, ビルド収支/LCO校正/スパーク81件等)。
> 仕様・設計は [../../host_esp32c3/Docs/SPEC.md](../../host_esp32c3/Docs/SPEC.md) / [../../host_esp32c3/Docs/ARCHITECTURE.md](../../host_esp32c3/Docs/ARCHITECTURE.md)。
