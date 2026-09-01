# ThunderSense — CH32V003 ⇔ ESP32 I2C リファレンス

上位バス（HW-I2C）における **CH32V003（スレーブ）** と **ESP32（マスタ）** の通信仕様。
データ構造は [SPEC.md](SPEC.md)、全体像は [README.md](README.md) 参照。

---

## 1. バス諸元

| 項目 | 値 |
|---|---|
| 役割 | CH32V003 = スレーブ / ESP32 = マスタ |
| スレーブアドレス | **`0x28`**（7bit。他センサと衝突しない値に。config.h `HWI2C_SLAVE_ADDR`） |
| 速度 | 400kHz 推奨（100kHz でも可） |
| クロックストレッチ | **使用**（短い待ちの吸収に必須。ESP32 側で有効化） |
| バイト順 | すべて **リトルエンディアン (LE)** |
| CRC | CRC-16/CCITT-FALSE（poly `0x1021`, init `0xFFFF`, no reflect, xorout `0x0000`） |

> ⚠️ **I2C スレーブはマスタを無視できない**。「応答しない」は**不可**（バスがハングする）。
> 「返せない」ときは **status バイトの BUSY を立てて返す**（＝黙るのではなくデータで Busy を返す）。

---

## 2. トランザクション様式

すべて「**マスタが cmd を Write → repeated START → Read**」または「**cmd＋引数を Write**」。

```
[Read 系]  S  ADDR+W  cmd  Sr  ADDR+R  data...  P
[Write系]  S  ADDR+W  cmd  arg...                P
```

---

## 3. コマンド一覧

| cmd | 種別 | 引数 / 返却 | 手段 | 用途 |
|---|---|---|---|---|
| `0x10` | R | 返: **Bundle 389B**（固定長） | DMA-TX | 雷イベント列の回収（メイン経路） |
| `0x01` | R | 返: **Status 12B**（固定） | DMA/割込 | 状態・単調カウンタの診断読取 |
| `0x02` | R | 返: **現在時刻 6B**（epoch4+ms2） | 固定 | ドリフト照合／offset 再計算 |
| `0x03` | R | 返: **INFO 8B** | 固定 | chipID/FWver/record_size/fifo_depth |
| `0x04` | R | 返: **HEALTH 8B**（vdd_mv/vrefint_raw/temp_c10/flags） | 固定 | 電源電圧監視。**ダイ温度は目安のみ**（CH32V003に温度センサ無し、temp_c10=0） |
| `0x05` | R | 返: **[reg, val] 2B** | 固定 | `0x44`で選択したAS3935レジスタの値 |
| `0x06` | R | 返: **CalibInfo 8B**（tuncap/ok/count/target/rco/div） | 固定 | **LCO校正結果の読取（チューニングポーリング）** |
| `0x20` | W | `gen(1)`,`crc16(2 LE)` | 割込→命令バッファ | **CLEAR**：一致時のみ shipping をクリア |
| `0x30` | W | `epoch_sec(4 LE)`,`ms(2 LE)` | 割込→命令バッファ | **SET_TIME**：ソフト時計シード |
| `0x40` | W | `afe_gb(1)`,`nf_lev(1)`,`wdth(1)`,`div(1)`,`mask_dist(1)` | 割込→命令バッファ | **CONFIG**：AS3935 設定（reg0x00/01/03） |
| `0x41` | W | `subcmd(1)` | 割込→命令バッファ | **CMD**：`0x01`=再校正+フラッシュ保存 / `0x02`=FIFO 全クリア / `0xA5`=softreset |
| `0x42` | W | `srej(1)`,`min_num_ligh(1)` | 割込→命令バッファ | **SENS**：感度設定 reg0x02（スパイク除去/最小雷数） |
| `0x43` | W | `reg(1)`,`val(1)` | 割込→命令バッファ | **REG_WRITE**：任意レジスタ書込（CL_STAT/TUN_CAP/0x3C/0x3D 等すべて） |
| `0x44` | W | `reg(1)` | 割込→命令バッファ | **REG_SELECT**：任意レジスタを読取→`0x05`で取得 |

---

## 4. `0x10` READ_BUNDLE（メイン経路）

### 返却レイアウト（固定 389B）

| off | フィールド | 幅 |
|---|---|---|
| 0 | `status` | 1B（CRC 対象外） |
| 1 | `length`（有効ビン数 0..32） | 1B |
| 2 | `gen`（世代 ID） | 1B |
| 3.. | `bins[32]`（未使用 0x00） | 384B |
| 387–388 | `crc16`（over `[length,gen,bins]`） | 2B |

### ESP32 側手順

```text
1) S ADDR+W 0x10 Sr ADDR+R  → 389B 読む
2) status(先頭バイト) を確認（bit 定義は SPEC §4.2, STATE は §4.1）:
     BUSY(bit7)=1     → 破棄して数 ms 後に再ポーリング
     DATA_OK(bit6)=1  → 続行（STATE==READY）
     DATA_OK=0 のとき STATE で分岐:
         0,1,4(BOOT/CALIBRATING/RECOVERING) → 待つ
         ≥5(FAULT_*)                        → 空バンドル。系全体でフェイルセーフ
3) CRC16 を [length,gen,bins] で検証
     NG → CLEAR せず次ポーリングで再取得（非破壊なので同じ shipping が返る）
     OK → length 件のビンを取り込み、絶対時刻へ変換（§7）
4) CLEAR を発行: 0x20 gen crc_lo crc_hi
     → CH32 が (gen,crc) 一致時のみ shipping を全ゼロ化＝削除確定
```

> **削除は必ずこのフロー**（読取＝非破壊、CLEAR＝ack）。通信エラーでも 1 件も失わない。

---

## 5. `0x01` READ_STATUS（診断・12B 固定）

| off | フィールド | 幅 | 説明 |
|---|---|---|---|
| 0 | `status` | 1B | §SPEC の status ビット |
| 1 | `active_gen` | 1B | 現 active の gen |
| 2 | `ship_len` | 1B | shipping の length（未回収件数の目安） |
| 3 | `boot_id` | 1B | 起動毎に変化（再起動検知） |
| 4–5 | `lightning_total` | u16 LE | 単調 |
| 6–7 | `disturber_total` | u16 LE | 単調 |
| 8–9 | `noise_total` | u16 LE | 単調 |
| 10–11 | `lost_total` | u16 LE | 捨てた詳細ビン数 |

`lightning_total` 等は**オーバフローしても正確**。詳細ビンを落としても総数はここで把握できる。

---

## 6. `0x20` CLEAR / `0x30` SET_TIME / `0x40` CONFIG / `0x41` CMD

### 6.1 `0x20` CLEAR（gen, crc16）
- `S ADDR+W 0x20 gen crc_lo crc_hi P`
- CH32：命令バッファへ格納 → メインループで `shipping.gen==gen && shipping.crc==crc` の時のみゼロ化。
- 不一致（stale/誤 ack）は**無視**。ESP32 は再 read で最新を取り直す。

### 6.2 `0x30` SET_TIME（epoch_sec, ms）
- `S ADDR+W 0x30 e0 e1 e2 e3 m0 m1 P`（すべて LE）
- CH32：ソフト時計を `epoch_sec.ms` にシード → `TIME_VALID=1`。
- ESP32 は起動直後・数分ごとに実施（ドリフト補正）。

### 6.3 `0x40` CONFIG（AS3935 設定）
- `afe_gb`（AFE Gain Boost, 屋内=0x12/屋外=0x0E 目安）
- `nf_lev`（Noise Floor 0–7）/ `wdth`（Watchdog Threshold 0–15）
- `div`（reg0x03[7:6] 分周比 0=÷16..3=÷128）/ `mask_dist`（disturber マスク 0/1）
- CH32：命令バッファ経由で AS3935 へ SW-I2C 反映。

### 6.4 `0x41` CMD
| subcmd | 動作 |
|---|---|
| `0x01` | AS3935 再校正（LCO）＋**結果をフラッシュ(option Data0)へ保存** |
| `0x02` | 全バンドル FIFO クリア |
| `0xA5` | ソフトリセット |

### 6.5 感度設定（`0x42` SENS）
- `srej`（reg0x02[3:0], 既定2）：**スパイク除去。大きいほど妨害波に強いが検出効率低下**（データシート）。
- `min_num_ligh`（reg0x02[5:4]）：INT_L 発火までの最小雷数（0=1,1=5,2=9,3=16）。
- 例：ライター等の誤検出が多い環境では `srej` を上げる。

### 6.6 チューニング（LCO校正）の起動とポーリング
```
① W[0x41, 0x01]          → 再校正を起動（~1.6s, 内部でIWDG kick）
② status.BUSY/STATE を監視 → 校正中は NOT_READY/RECOVERING、完了で READY
③ R[0x06] CalibInfo      → tuncap / ok / count / target(3125) / div を確認
   （ok=1 かつ |count-target| 小 なら同調良好。結果はフラッシュにも保存済）
```

### 6.7 任意レジスタ アクセス（パススルー）
AS3935 の全レジスタに到達可能（上表でカバーしない CL_STAT / TUN_CAP 手動 / PRESET_DEFAULT(0x3C) / CALIB_RCO(0x3D) / SRCO・TRCO校正状態(0x3A/0x3B) / energy/distance/INT 直読 等）。
```
書込: W[0x43, reg, val]
読出: W[0x44, reg]  →（BUSY解除待ち）→ R[0x05] = [reg, val]
```

---

## 7. タイムスタンプの絶対時刻化（ESP32 側）

ビンは `epoch_sec(4)+ms(2)` を直接持つ（`TIME_VALID=1` の場合）。

```text
event_time = epoch_sec + ms/1000.0   [秒]
```

`TIME_VALID=0`（未同期）のビンは起動相対。ESP32 は SET_TIME 後の再取得で絶対時刻を得る。
ドリフト照合が要る場合は `0x02`（現在時刻読取）で offset を再計算。

---

## 8. Busy／衝突時の作法（重要）

| 状況 | CH32 の応答 | ESP32 の作法 |
|---|---|---|
| キャプチャ中(~1–3ms) | `BUSY=1`(STATE=BUSY_CAPTURE) or 短時間ストレッチ | 破棄→数 ms 後に再ポーリング |
| 起動/校正中 | `DATA_OK=0`, STATE=BOOT/CALIBRATING | 待つ（SET_TIME も投げる） |
| センサ異常 | `DATA_OK=0`, STATE≥5(FAULT_*) | 空バンドル受領＋系全体フェイルセーフ |
| shipping 未 ack | 同じ shipping を再送 | CRC OK なら CLEAR、NG なら再 read |
| 命令実行中 | `BUSY=1` | 完了まで再ポーリング |

- **推奨ポリシー**：既定 Busy。空き（capture 非実行）時のみ bundle を返す（雷ファースト）。
- 1 イベント占有 ≒ 3ms、5 件/秒でも Busy 約 1.5% → ESP32 可用性 98% 超。

---

## 9. 代表シーケンス（1 秒ポーリング・5 件到着時）

```text
t=0.0s  ESP32: 0x10 read → status.BUSY=1（ちょうど捕捉中）→ 破棄
t=0.005s ESP32: 再 read → status OK, length=5, gen=7 の Bundle 受信
        ESP32: CRC16 検証 OK
        ESP32: 0x20 07 <crc_lo> <crc_hi> → CH32 が shipping[gen7] をクリア
t=1.0s  次周期へ（新 gen で active→shipping swap）
```

---

## 10. エラー処理まとめ

| エラー | 検知 | 回復 |
|---|---|---|
| 伝送化け | CRC16 不一致 | CLEAR せず再 read（非破壊） |
| CH32 再起動 | boot_id 変化 / gen・seq リセット / ms 逆行 | SET_TIME で再シード |
| オーバフロー | `status.OVERFLOW` / `lost_total` 増加 | 総数はカウンタで保全、詳細は諦める |
| gen 衝突 | (gen,crc) 二重一致（極稀） | gen 循環＋crc 併用で実質排除 |

---

## 11. 実装時に確定するパラメータ

- スレーブアドレス（他デバイスと非衝突）
- DMA チャネル（TX=CH6/RX=CH7 想定 → RM 確認）
- CONFIG 既定値（AFE_GB/NF_LEV/WDTH/DIV/MASK_DIST）
- Busy 粒度（capture 全体 / ビットバングのみ）
- CRC16 実装（テーブル or ビット演算。ESP32 と多項式一致必須）
