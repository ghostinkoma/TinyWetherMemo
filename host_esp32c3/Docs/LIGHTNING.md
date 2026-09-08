# 雷センサ実装 + 危険度(積和) — 設計メモ

WetherLoggerBox の第1段。ESP32-C3 が **ThunderSense ブリッジ**（AS3935→CH32V003,
I2C スレーブ `0x28`）をポーリングし、落雷イベントから「現在の危険度」を算出する。

## ハード / 配線

| ESP32-C3 | ThunderSense (CH32V003) |
|---|---|
| SDA = GPIO8 (`WLB_I2C_SDA`) | PC1 (I2C SDA) |
| SCL = GPIO9 (`WLB_I2C_SCL`) | PC2 (I2C SCL) |
| GND | GND 共通 |

- ブリッジは 3.3V 動作。I2C プルアップは AS3935 モジュール側にある。
- ピンは `src/config.h` で変更可。基板の実配線に合わせること。
- ブリッジ側の詳細プロトコルは `D:\hobby\ThunderSence\Docs\I2C_REFERENCE.md`。
  ホスト API は同梱の `lib/ThunderSense/ThunderSense.h`（自己文書化）。

## 危険度アルゴリズム（積和 / weighted sum-of-products）

要件: 「遠い落雷は危険度低、近い落雷は危険度高。統計を取る方式で、積和で算出」。

1. **距離ビン**（`WLB_LN_BINS = 8`）に落雷を仕分ける。
   `bin0:≤1km(真上) 1:≤5 2:≤10 3:≤15 4:≤20 5:≤30 6:≤40 7:圏外`
2. 各ビンに**減衰カウント** `acc[i]` を持つ。落雷1件で該当ビン `+1`、
   毎秒 半減期 `WLB_LN_HALFLIFE_S`（既定5分）で減衰 → 常に「今」の統計量。
3. 危険度（生値）= **積和** `danger = Σ acc[i] × weight[i]`
   重み `weight = {20,16,10,6,4,2,1,1}`（近いほど大）。これが `acc[]`（統計ベクトル）と
   `weight[]`（重みベクトル）の**内積**そのもの。
4. `%` 正規化: `danger / WLB_LN_FULLSCALE × 100`（0..100 でクランプ）。
5. レベル: `<5 安全 / <25 注意 / <50 警戒 / <75 危険 / それ以上 厳重警戒`。

パラメータは `src/config.h` で調整。重み表は `src/lightning.cpp` の `kWeight[]`。

## サンプリング周期

- 雷はイベント駆動。毎ループ `poll()` で drain（ブリッジは強い32件を保持）。
- 危険度の減衰更新は `WLB_POLL_MS = 1000`（1秒）毎。
- ダッシュボード/ログ集約は `WLB_DASHBOARD_MS = 10000`（10秒）毎。

## 使い方（本段: 単体シリアルテスト）

```
Lightning ln;
ln.begin();               // I2C + ブリッジ初期化
// (NTP後) ln.syncTime(epoch);
loop: ln.tick(millis());  // 内部で1秒に間引き
      LnSnapshot s = ln.snapshot();   // dangerPct / level / nearestKm / 各カウンタ
```

`platformio.ini` の環境で `pio run`（ビルド確認済: RAM 4.0% / Flash 24.7%）。
書込は `pio run -t upload`、シリアルは `pio device monitor`（115200）。

## 今後（未実装 / このロガーの全体像）

- **タブ1 ダッシュボード**: 温湿度気圧 + 雷危険度を HTTP + Chart.js 表示（10秒更新）。
  環境センサは 1秒×10サンプル→最小最大を捨て8個平均（`>>3` ビットシフト）。
  時/日/週/月/年 のスコープ切替。長期データは別途 MySQL へクエリ。
- **タブ2 ストレージ管理**: 10秒サンプルを FS 記録 → CSV ダウンロード。
  MySQL 送信先 URL / ID / パスワード入力欄（例: 3分毎に送信）。
- **タブ3 校正**: 雷センサ（AS3935 LCO/感度）キャリブレーション UI。
  下位 API は `ThunderSense::recalibrate() / setSensitivity() / configure()` 等。
