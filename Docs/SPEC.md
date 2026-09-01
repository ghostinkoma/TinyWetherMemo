# ThunderSense — 詳細仕様 (SPEC)

対象: CH32V003J4M6 (RISC-V, 48MHz, **SRAM 2KB**, Flash 16KB) を AS3935 ⇔ 上位 I2C の中継に用いる。
本書はデータ構造・状態機械・割込優先度・DMA・校正・メモリ収支を規定する。
上位バスのコマンド詳細は [I2C_REFERENCE.md](I2C_REFERENCE.md)。

---

## 1. システム構成

```
        SW-I2C(master)                 HW-I2C(slave)
 AS3935 ───────────────► CH32V003J4M6 ───────────────► ESP32 (master, NTP)
   │ IRQ(PC4)                 │ SysTick soft-clock
   └───────────────────────► │ event capture → bins → bundle(ping-pong)
```

- **データ経路**：AS3935 → ビン → バンドル → DMA → ESP32
- **制御経路**：ESP32 → 命令バッファ → メインループ実行

---

## 2. データ構造

### 2.1 イベントビン（12 バイト固定）

| off | フィールド | 型 | 説明 |
|---|---|---|---|
| 0 | `type` | u8 | reason（**0x01=noise / 0x04=disturber / 0x08=lightning**, AS3935 reg0x03 下位ニブル実値）。b7=`time_valid` |
| 1 | `distance` | u8 | km。`0x3F`=範囲外, `0x01`=頭上, `0x00`=不明 |
| 2–4 | `energy` | u24 LE | 20bit 有効（`reg6[3:0]:reg5:reg4`）、上位 4bit は 0 |
| 5–8 | `epoch_sec` | u32 LE | Unix 秒（絶対）。`time_valid`=0 のときは起動相対 |
| 9–10 | `ms` | u16 LE | 0–999（ミリ秒小数） |
| 11 | `seq` | u8 | イベント連番（0–255 循環）。欠落検知用 |

```c
typedef struct __attribute__((packed)) {
    uint8_t  type;        // [7]=time_valid, [3:0]=reason
    uint8_t  distance;    // km
    uint8_t  energy[3];   // 20-bit LE
    uint32_t epoch_sec;   // LE
    uint16_t ms;          // LE
    uint8_t  seq;
} EventBin;               // = 12 bytes
```

### 2.2 バンドル（ping-pong 2 段）

DMA が上位へ送出する単位。**固定長転送**（未使用ビンは 0x00）。

| off | フィールド | 幅 | CRC 対象 |
|---|---|---|---|
| 0 | `status` | 1B | ✗（ライブ状態、CRC 外） |
| 1 | `length` | 1B | ✓ 有効ビン数 0..32 |
| 2 | `gen` | 1B | ✓ 世代 ID（swap 毎に ++、0–255 循環） |
| 3 .. | `bins[32]` | 32×12=384B | ✓ 未使用は 0x00 |
| 387–388 | `crc16` | 2B | — CRC16 の格納先 |

- **転送総長 = 1 + 1 + 1 + 384 + 2 = 389 バイト**（固定, `sizeof(Bundle)`）。
- **CRC16 対象 = `[length, gen, bins[32]]`（= 386 バイト）**。多項式は CRC-16/CCITT-FALSE（`0x1021`, init `0xFFFF`）を推奨。
- `status` は毎回のライブ値なので CRC 外（Busy 等が変動するため）。

```c
typedef struct __attribute__((packed)) {
    uint8_t  status;      // live; not covered by CRC
    uint8_t  length;      // 0..32
    uint8_t  gen;         // generation id
    EventBin bins[32];    // 384B, unused = 0x00
    uint16_t crc16;       // over [length, gen, bins]
} Bundle;                 // wire = 389B (sizeof)
```

> **CRC8 ではなく CRC16 を採用する理由**：一括 CRC の対象が ~386B と大きく、CRC8 では未検出誤り率が高い。
> CRC16 でハミング距離を確保する。

### 2.3 静的確保（ヒープ不使用）

```c
static Bundle          g_bundle[2];   // ping-pong: ~782B
static volatile uint8_t g_active;     // 0/1: 充填中インデックス
static volatile uint8_t g_shipping;   // 0/1: 回収対象インデックス（空き時は none）
static CmdBuf          g_cmd;         // 命令バッファ（§5）
static SoftClock       g_clock;       // SysTick 時計（§6）
static Totals          g_totals;      // 単調カウンタ（§7）
```

---

## 3. 割込優先度（PFIC ネスト）

| 優先 | 割込 | 役割 | 所要 |
|---|---|---|---|
| **最高** | EXTI (PC4 = AS3935 IRQ) | **絶対時刻を即確定**（SysTick 読取のみ）＋2ms タイマ起動 | 数 µs |
| 高 | 2ms ワンショット完了 | SW-I2C で AS3935 レジスタ読取 → ビン化 → active へ push | ~1ms |
| 中 | I2C1 スレーブ | 上位バスのコマンド受信／DMA アーム | 短 |
| 中低 | DMA TC | 送出後始末 | 短 |
| 背景 | メインループ | 命令実行・クリア（ゼロ埋め）・校正 | — |

**鉄則**
- EXTI が最上位。**キャプチャの ISR / クリアの memset では割込禁止にしない**（EXTI が必ず割り込めること）。
- 時刻確定は EXTI 内で SysTick を読むだけ（µs）。レジスタ読取は 2ms 後（AS3935 の settle 必須）。

---

## 3.5 多重防御（イベント取りこぼし冗長化）

「エッジ取りこぼし＝イベント消失」を避けるための多層設計。
**前提**：AS3935 の INT ピンは**イベントで H になり、reg0x03 を読むまで H を保持**する
＝**センサ自身がハードウェアでレベルラッチ済**。したがって「後からレベルを見れば必ず拾える」。

### 3.5.1 防御レイヤ

| 層 | 手段 | 役割 | コスト |
|---|---|---|---|
| **L1** | **EXTI エッジ**(PC4) | 主経路。即時に絶対時刻確定＋2ms タイマ起動 | 既存 |
| **L2** | **レベルポーリング**（メインループ / SysTick tick） | INT が H なのに未処理なら捕捉を起動。**エッジ取りこぼしの保険**（INT がレベル保持なので確実に効く） | ほぼ 0 |
| **L3**（任意） | **TIM1_CH4 インプットキャプチャ**(PC4) | エッジ時刻を **CCR に HW で自動ラッチ**＋CC フラグ。CPU が遅れても正確な発生時刻が残る＝“正しいハードウェアラッチ” | TIM1（元々空き） |
| **L4** | **防御的定期 read**（例 100ms ごとに reg0x03 を無条件確認） | ISR 全滅・スタック時の最後の砦 | ほぼ 0 |

- **本命は L2**：`if (IRQ_pin==H && !servicing) → service_as3935()` を tick で回すだけ。**追加ピン・追加ペリフェラル不要**。
- **L3 が「ラッチ」の正解**：ADC ではなく**タイマのインプットキャプチャ**。EXTI と IC 入力は同一ピンでも両立し得る（EXTI は入力データを見るので AF モードでも動く見込み。**※実機で要検証**）。LCO 校正で PC4=TIM1_CH4 を使う場合と排他運用。
- L1＋L2（＋L4）で実用上の取りこぼしはほぼ根絶。L3 は「時刻精度＋独立 HW 経路」の上積み。

### 3.5.2 サービス入口の一本化（二重カウント防止）

多重検出でもビンを 1 件に収束させるため、**捕捉の入口を 1 本化**する。

```c
static volatile uint8_t g_service_pending;  // 1=要サービス
static volatile uint8_t g_servicing;        // 1=サービス実行中(排他)

// L1(EXTI) / L2(level poll) / L3(TIM IC) / L4(defensive) はすべて↓を叩くだけ
static inline void request_service(uint32_t t_capture) {
    // 絶対時刻は最初に確定したものを採用（EXTI が最速）
    if (!g_service_pending) { g_capture_ms = t_capture; }
    g_service_pending = 1;
}

// 実処理は 1 回だけ:  2ms 待ち→reg0x03 読取→種別/energy/distance→ビン化→push
// reg0x03 読取で INT クリア＝全レイヤの検出が同時に解消
void service_as3935(void) {
    if (g_servicing) return;         // 排他
    g_servicing = 1;
    ... /* read reg0x03, build EventBin, push to active */
    g_service_pending = 0;
    g_servicing = 0;
}
```

- どの経路から来ても **「INT=H を 1 回サービス → reg0x03 読取でクリア」に収束**。経路が増えてもビンは 1 件。
- 絶対時刻は**最速で確定した値（通常 L1）**を採用し、後発レイヤは上書きしない。

### 3.5.3 ADC をラッチに使わない理由（設計判断の記録）

将来の再検討を防ぐため、不採用理由を明記する。

1. **ADC は持続ラッチしない**：S/H の保持は 1 変換(µs)のみ、結果は次変換で上書き。**サンプル時間(SMP)を延ばしても変換が長くなるだけでラッチにならない**。
2. **対象がデジタルのレベル信号**：AS3935 IRQ は H 固定の論理信号で、電圧を測る意味がない。
3. **SOP8 のピン/モード競合**：J4M6 は 5GPIO 全使用で ADC 用空きピンが無い。IRQ の PC4 を**アナログにするとデジタル入力バッファが切れ EXTI が死ぬ**（同一ピンで EXTI と ADC 入力は同時不可）。

→ 冗長化は L1〜L4 で達成。**ADC は雷ラッチに使わず**、将来はアンテナ電源監視／基板温度など別用途に温存する。

---

## 4. 状態機械

```
 BOOT ─► CALIBRATING ─► (ESP32 が SET_TIME) ─► READY ◄─► BUSY_CAPTURE
                                                  ▲
                                                  └─ RECOVERING ◄─ FAULT_*（異常検知）
```

### 4.1 STATE 列挙（status 下位 4bit）

| STATE | 値 | 意味 | ESP32 への含意 |
|---|---|---|---|
| `BOOT` | 0 | 起動直後 | NOT_READY（待つ） |
| `CALIBRATING` | 1 | AS3935 LCO 校正中 | NOT_READY |
| `READY` | 2 | 通常稼働 | データ有効（DATA_OK=1） |
| `BUSY_CAPTURE` | 3 | AS3935 SW-I2C 中 | BUSY（再試行） |
| `RECOVERING` | 4 | 異常回復シーケンス中（§14） | 空バンドル。待つ |
| `FAULT_NACK` | 5 | AS3935 無応答 | FAULT。空バンドルだが CRC 正当 |
| `FAULT_STUCK` | 6 | SDA/IRQ 固着 | FAULT |
| `FAULT_CALIB` | 7 | 校正失敗 | FAULT |
| （予約） | 8–15 | 将来拡張 | — |

- ESP32 判定：`STATE==2`→採用 / `STATE==3`→再試行 / `STATE∈{0,1,4}`→待つ / `STATE≥5`→FAULT。
- `BUSY_CAPTURE` は `capture_in_progress` で表現。EXTI で立ち、ビン commit で下ろす。
  最小実装は capture 全体(~3ms)を Busy、可用性重視なら SW-I2C ビットバング中(~1ms)だけ Busy。

### 4.2 status バイト（全 read の先頭 = `bundle.status`）＝**1 バイトで確定**

| bit | 名称 | 意味 |
|---|---|---|
| 7 | `BUSY` | STATE==BUSY_CAPTURE のミラー（先頭 bit で即再試行判定）。以降のバイト無効 |
| 6 | `DATA_OK` | 1=バンドル有効（STATE==READY）。ESP32 はこの 1bit で採否判断 |
| 5 | `TIME_VALID` | ソフト時計が NTP 同期済 |
| 4 | `OVERFLOW` | 前回回収以降にビンを捨てた |
| 3:0 | `STATE` | §4.1 の列挙 |

> **8bit で収まる根拠**：`NOT_READY`/`FAULT`/`RECOVERING`/`CALIB_DONE` は
> **相互排他のライフサイクル**なので STATE に内包し、直交条件（TIME_VALID/OVERFLOW）だけをフラグに残した。
> `BUSY`/`DATA_OK` は STATE から導出可能な**利便ミラー**（ESP32 の高速判定用）。
> → **status/管理メモリの拡張は不要**。将来フラグが増える場合のみ、診断レジスタ 0x01（§5, コールドパス）へ追加する。

---

## 5. 命令バッファ（制御経路）

- ESP32 の書込コマンドは **I2C ISR で高速に本バッファへ格納**（実行しない）。
- **実行はメインループ**（雷が空いてから）。→ 命令を取りこぼさず雷を最優先。
- 単一スロットで十分（ESP32 は「命令→Busy 消えるまで待つ」運用）。保険で 2–4 段リングも可。

```c
typedef struct {
    volatile uint8_t pending;   // 未実行フラグ
    uint8_t  opcode;
    uint8_t  arg[6];            // 例: CLEAR(gen, crc_lo, crc_hi) / SET_TIME(...)
} CmdBuf;
```

コマンド一覧は [I2C_REFERENCE.md](I2C_REFERENCE.md)。

---

## 6. 時刻（SysTick ソフト時計＋NTP 対応付け）

CH32V003 に **RTC ペリフェラルは無い**（バックアップドメインも無し）。よって：

1. SysTick 周期割込で **64bit ms 積算器**（`g_clock.ms`）をフリーラン。
2. 起動後、ESP32 が `SET_TIME`（epoch_sec + ms）を書き **積算器をシード**。以後ビンは絶対時刻を刻む。
3. `time_valid` を status/ビンに反映。シード前は起動相対＋`time_valid=0`。
4. **再起動検知**：`gen`/`seq` リセット・`ms` 逆行で ESP32 が検知 → 再シード。
5. **ドリフト**：内蔵 HSI ±1%（1 秒で ±10ms）。ESP32 が数分ごとに再シードして累積抑制。厳密なら外部 XTAL（雷用途では不要）。

> **SW-I2C の µs 遅延も SysTick の VAL レジスタ**で生成 → TIM を消費しない。

---

## 7. 単調カウンタ（オーバフロー時も総数保全）

```c
typedef struct {
    uint16_t lightning_total;   // 検出総数（FIFO 溢れても加算）
    uint16_t disturber_total;
    uint16_t noise_total;
    uint16_t lost_total;        // 詳細ビンを捨てた数
} Totals;
```

- **ビンに格納するのは雷(INT_L)のみ**。disturber/noise は**カウンタのみ**（妨害波連発が本物の雷を押し出すのを防ぐ）。
- オーバフロー方針：**「エネルギー上位 N(=32) を保持」**。満杯時は**最弱ビンを O(N) スキャンで探し、新規の方が強ければ置換**（ソート無し。約1µs）。
  - active バッファのみ書換（shipping は不可侵）。ホストは**読取周期を可変**にでき、遅い周期＝その窓の上位32、速い周期＝取りこぼし減。
  - 置換/破棄のたび `lost_total++`＋`overflow`。**単調カウンタ（総数）は常に正確**。
- ランキング指標は energy(20bit, 相対値)。配列は未整列（必要なら ESP32 側でソート）。

---

## 8. DMA

- CH32V003 I2C1 は F0 系実装。**スキャッタギャザ非対応＝1 転送=連続 1 ブロック**。
  → バンドルは連続確保（§2.3）で満たす。ping-pong なのでリング wrap 問題は発生しない。
- **I2C1_TX = DMA1_CH6 / I2C1_RX = DMA1_CH7**（F0 系マップ準拠。**RM とヘッダで最終確認**）。
- **コマンド（書込）は DMA で受けない**：コマンドを解釈しないと DMA 長/ソースを決められないため、
  書込は RXNE 割込で受け、**DMA は bundle 送出（読出）専用**。
- 送出フロー：
  ```
  読出コマンド受信(ISR) →
    capture_in_progress なら status=BUSY のみ返す（本体送らない）
    さもなくば: shipping を確定（必要なら active→shipping へ swap）
                DMA1_CH6 src=&g_bundle[shipping], cnt=389 を設定
                I2C1 CTLR2.DMAEN=1（アーム完了まで SCL ストレッチで待たせる）
  TXE 毎に DMA が DATAR へ供給 → TC 割込で後始末
  ```
- **オーバリード保護**：cnt=389 固定。余分にクロックされたら 0xFF を返すガードを 1 つ。

---

## 9. トリプルバッファとクリア（非破壊 read ＋ ESP32 主導削除）

3 スロットが役割を回転（`bundle.c`）：**FILLING（充填）/ SERVING（凍結・回収対象）/ DIRTY（背景クリア待ち）/ CLEAN（予備プール）**。
3 枚目により**遅いゼロクリアを fill/serve から完全に分離**できる。

```
[読出コマンド]
  if SERVING あり:              同じ SERVING を再送（retry, 非破壊）
  elif FILLING に件数>0:        FILLING→SERVING(凍結, gen++)、CLEAN 1枚→新 FILLING
                               （CLEAN 無ければ DIRTY を即クリアして捻出）
  else:                        FILLING を length=0 の空応答として返す
  DMA 送出

[ESP32]
  CRC16 検証 OK → CLEAR(gen,crc) → 一致時のみ SERVING を DIRTY 化（データは履歴として残置）
  CRC16 NG     → CLEAR 送らず → 次ポーリングで同じ SERVING を再取得

[背景] bundle_tick_clear()（メインループ）が DIRTY を1枚ずつクリア:
   ① crc16=0（無効化）→ ② bins ゼロ → ③ length/gen=0 → CLEAN 化
```

- **クリア＝(gen, crc) 一致時のみ**（コンテンツアドレス指定 ack）。stale ack・誤クリア防止。`gen` 併記で CRC16 衝突排除。
- **CRC 先消去**：クリアが中断されても不正 CRC で「未完成」と判別でき、半端なバッファが有効読取されない。
- クリアは**メインループ・割込可能**。DIRTY は FILLING/SERVING と別スロットなので雷・読取と非競合。
- 「履歴」= ack 済 SERVING がクリアされるまで DIRTY として残る（現状ホストからの再アドレス指定は無し。再取得は ack 前の SERVING 再読で担保）。

---

## 10. AS3935 取り扱い（一次資料: 秋月デモ）

- 初期化（デモ準拠）：`0x3C=0x96, 0x3D=0x96`（校正 RCO 系）、AFE_GB（屋内=0x12 等）、NF_LEV、WDTH、
  分周比（reg0x03[7:6]）。
- **LCO 校正必須**（アンテナ共振 500kHz ±3.5%）。TUN_CAP を reg0x08[3:0] で 0–120pF/8pF ステップ調整。
  実装 = `as3935_calibrate()`（**FreqCounter(AVR) の CH32V003 移植**）：
  - DISP_LCO(reg0x08 bit7)=1 で IRQ に LCO/16 を出力、TUN_CAP 0..15 を掃引。
  - **HW カウンタの代わりに SysTick ゲート(100ms)で PC4 をポーリングし立上りエッジを計数**
    （PC4=T1CH4 は TIM 外部クロック源にできないため。48MHz ポーリングは 31.25kHz を余裕で捕捉）。
  - 目標 = 500000/16×0.1 = **3125 カウント**。`|count-3125|` 最小の cap を採用、DISP_LCO=0 に戻す。
  - `|diff| > ±3.5%(≒110)` なら **AS_CALIB_FAIL**（STATE=FAULT_CALIB）。
  - 出典：AE_AS3935DEMO / FreqCounter.cpp（KHM LAB3, Martin Nawrath）。AVR レジスタコードは非移植、手順と目標値のみ流用。
  - 代替（方式 A）：ベンチで一度校正した `TUN_CAP` を `config.h` に定数ハードコードし校正省略も可。
- **IRQ 後シーケンス**：IRQ(High) → **2ms 待ち** → reg0x03 下位ニブルで種別判定 → 種別=雷なら
  energy(reg4–6)/distance(reg7) 読取 → ビン化。reg0x03 読取で IRQ クリア＆再アーム。
- **アドレス**：デモは `0x00`。実機で実測（ADD0/ADD1 依存。`0x03` の個体もある）。

---

## 11. メモリ収支（SRAM 2KB）※実測（ch32fun/riscv-none-elf-gcc 14.2, -flto）

**現行（triple, N=32 ＋ 全機能：ADC/健康度/フラッシュ校正/レジスタパススルー/感度, DEBUG=0）実測：
FLASH 8988 B / 16 KB (54.9%)、RAM(static) 1344 B / 2 KB (65.6%)。**
→ スタック余裕 = 2048 − 1344 = **704 B**（十分）。Flash はビンでは増えない（ビンは RAM のみ）。
（機能を外した double/32 の素の値は FLASH 7896 B / RAM 932 B）

### バッファ構成別の実測比較（`--print-memory-usage`）

| 構成 | RAM static | %2KB | スタック余裕 | 判定 |
|---|---|---|---|---|
| **double / 32（現行）** | 932 B | 45.5% | 1116 B | ✅ 安全・既定 |
| triple / 32 | 1320 B | 64.5% | **728 B** | ✅ 移行可・余裕あり |
| double / 64 | 1700 B | 83.0% | **348 B** | ⚠️ 動くがスタック薄（要watermark検証） |
| triple / 64 | >2048 B | — | 溢れ | ❌ 不可（リンク不能） |

- `mem.h` の `_Static_assert`（`SRAM_STACK_MIN=640`）が **64ビン構成をビルド時に拒否**（実確認済）。
  64ビンを試すには `SRAM_STACK_MIN` を下げる必要があり、それ自体が「スタック危険」の証左。
- **結論**：容量を増やすなら **triple/32 が安全な選択**（728Bスタック）。**64ビンはスタック348Bで非推奨**
  （上位32保持＋単調カウンタで実用上32で十分。triple は容量増より“パイプライン余裕”の意味合い）。
- N=64 をどうしても使うなら bin 縮小（seq削除→11B 等）＋スタック実測が前提。

---

## 13. モジュール構成（ファーム設計）

責務を分離し、**ISR で動く層**と**メインループで動く層**を明確化する。

```
firmware/
├─ config.h        ピン/スレーブアドレス/バッファ寸法/しきい値/ビルド時定数
├─ main.c          起動シーケンス, メインループ(dispatch/命令実行/クリア), IWDG kick
├─ crc16.[ch]      CRC-16/CCITT-FALSE（テーブル or ビット演算。ESP32 と多項式一致必須）
├─ softclock.[ch]  SysTick ms 時計, 絶対時刻取得, SET_TIME シード, µs 遅延
├─ swi2c.[ch]      ビットバング I2C マスタ（★per-bit タイムアウト＋バス回復付）
├─ as3935.[ch]     AS3935 ドライバ: init / LCO 校正 / read_event / config / health   ← swi2c
├─ capture.[ch]    L1 EXTI / L2 levelpoll / L3 TIM-IC / L4 defensive, service 一本化   ← as3935, softclock
├─ bundle.[ch]     EventBin/Bundle, ping-pong, push/swap/clear, CRC 付与              ← crc16, softclock
├─ i2c_slave.[ch]  HW I2C1 スレーブ＋DMA, コマンド解釈, 命令バッファ, bundle 送出      ← bundle
└─ health.[ch]     センサ/バス異常の検知・回復 状態機械, FAULT 報告                    ← as3935, swi2c
```

### 13.1 依存方向（上位→下位、循環なし）

```
main → {capture, i2c_slave, health, softclock}
capture → as3935 → swi2c → softclock(µs遅延)
i2c_slave → bundle → {crc16, softclock}
health → {as3935, swi2c}
```

### 13.2 実行コンテキスト（競合設計の要）

| モジュール | 主な実行文脈 | 備考 |
|---|---|---|
| `capture`(L1) | **EXTI ISR（最高優先）** | 絶対時刻確定のみ。数 µs |
| `capture`(2ms読取) | タイマ ISR（高） | `as3935.read_event()`→`bundle.push()` |
| `capture`(L2/L4) | メインループ / SysTick | レベル監視・防御 read |
| `i2c_slave` | I2C ISR（中）＋DMA | コマンド受信は命令バッファへ格納のみ |
| 命令実行/クリア | **メインループ（背景）** | 雷が空いてから。割禁にしない |
| `health` | メインループ | 回復シーケンス（時間がかかる処理） |

### 13.3 主要 API（抜粋）

| 関数 | モジュール | 役割 |
|---|---|---|
| `swi2c_start/stop/wr/rd(...)` | swi2c | ★戻り値で `SWI2C_OK/TIMEOUT/NACK` を返す |
| `swi2c_bus_recover()` | swi2c | SCL を最大 9 発叩き SDA 解放→STOP |
| `as3935_init() / as3935_calibrate()` | as3935 | 初期化・LCO 校正（方式A/B, §10） |
| `as3935_read_event(EventBin*)` | as3935 | reg0x03→種別/energy/distance→ビン化。失敗時 status 返す |
| `as3935_health()` | as3935 | `OK/NACK/BUS/STUCK_IRQ/CALIB_FAIL` |
| `capture_request(t_ms)` | capture | 全レイヤ共通の入口（§3.5.2） |
| `capture_service()` | capture | 排他 1 回実行。メイン/タイマから駆動 |
| `bundle_push(EventBin*)` | bundle | active へ追記。満杯は drop-newest＋lost++ |
| `bundle_freeze_for_read()` | bundle | swap→shipping 確定→CRC 付与→DMA 長返す |
| `bundle_clear(gen,crc)` | bundle | (gen,crc) 一致時のみ全ゼロ化 |
| `i2c_slave_on_cmd()` | i2c_slave | 受信コマンドを命令バッファへ |
| `health_tick()` | health | 異常検知＋回復状態機械（§14） |

---

## 14. センサ異常・ハング処理（必須）

**大原則：ハングした AS3935 が上位バス（HW-I2C スレーブ）を道連れにしてはならない。**
ブリッジの存在意義（隔離）そのもの。以下を必ず実装する。

### 14.1 SW-I2C は全ループにタイムアウト（最優先の防波堤）

- `swi2c_*` の **クロックストレッチ待ち・SDA 解放待ちは必ず有限ループ**（SysTick で上限管理、例 各 1ms）。
- **無限待ちを 1 箇所でも作らない**。ここが抜けると、ハング AS3935 で CPU が固まり、
  HW-I2C スレーブが応答不能→**上位バス全体がハング**する（最悪シナリオ）。

### 14.2 異常シナリオ別の回復

| # | 症状 | 検知 | 回復 |
|---|---|---|---|
| A | SDA 固着（スレーブが SDA を Low 保持） | START/読取でタイムアウト | `swi2c_bus_recover()`：SCL を最大 9 発→STOP。復旧しなければ B へ |
| B | 無応答（NACK 連発） | `as3935_read_event` が NACK | リトライ N=3 → 失敗で `as3935_init()`+再校正 → なお失敗で **SENSOR_FAULT** |
| C | IRQ 固着（reg0x03 読取後も INT=H） | L2 レベル監視が「H 継続」を検出 | reg0x03 再読取→ noise/disturber マスク見直し→continuous なら再校正。閾値超過で **SENSOR_FAULT** |
| D | 校正失敗（LCO 周波数が範囲外） | `as3935_calibrate` の測定値外れ | TUN_CAP 再掃引→失敗で **CALIB_FAIL**（FAULT 扱い） |
| E | CH32 ファーム暴走 | **IWDG（独立ウォッチドッグ）** | リセット→再 init。ESP32 は `boot_id` 変化で検知→`SET_TIME` 再シード |
| F | HW-I2C ペリフェラル固着（BUSY 張付き） | I2C ISR/送出のタイムアウト監視 | I2C1 ソフトリセット（CTLR1.SWRST）→再 init。バス回復シーケンスも実施 |

### 14.3 FAULT 時の振る舞い（フェイルセーフ）

- **SENSOR_FAULT でも HW-I2C スレーブは生かす**。ESP32 には
  **有効な空バンドル（length=0, 正しい CRC）＋ status.FAULT=1** を返し続ける。
  → 上位バスと他センサは無事。ESP32 は「雷センサ死亡」を認識して系全体でフェイルセーフ判断（[[safety-philosophy]] に整合）。
- `health` が **背景で周期的に回復を再試行**（例 5 秒毎に A→B→D）。復旧したら FAULT 解除。
- **単調カウンタ**は保持（消さない）。FAULT 前の総数は維持。

### 14.4 status への反映（ESP32 が判別可能に）

異常は**独立フラグではなく STATE 値**で表す（§4.1）。→ status は 1 バイトのまま。

| 事象 | STATE 値 | ESP32 の扱い |
|---|---|---|
| 回復シーケンス中 | `RECOVERING`(4) | 空バンドル。待って再ポーリング |
| AS3935 無応答 | `FAULT_NACK`(5) | FAULT。空バンドル（CRC 正当）＋系全体でフェイルセーフ |
| SDA/IRQ 固着 | `FAULT_STUCK`(6) | FAULT |
| 校正失敗 | `FAULT_CALIB`(7) | FAULT |

- ESP32 は `STATE≥5` を FAULT と判定。詳細理由は STATE 値そのもので識別（追加バイト不要）。
- 復旧回数や LCO 実測値など**冷経路の詳細**が要る場合のみ、診断レジスタ 0x01（§5）に載せる。

> ⚠️ **SOP-8 の制約**：予備 GPIO ゼロのため **AS3935 のハード電源断（power-cycle）で回復する手段が無い**。
> シナリオ B/C が SW 回復で復旧しない個体不良では FAULT のまま。
> ハード電源断が必要なら **TSSOP20(F4P6) に移行**し、`SENSOR_PWR_EN`（MOSFET 制御）と
> 余裕あれば `DATA_READY` 出力を確保するのが望ましい（[README.md](README.md) §4.1）。

### 14.5 IWDG 運用

- メインループで `IWDG_ReloadCounter()` を kick。
- **SW-I2C 等の長処理はタイムアウト付**なので、正常時はループが回り続け kick される。
- 真に暴走した場合のみタイムアウトでリセット→シナリオ E の回復に合流。

---

## 16. 健康度モニタ（VDD / ダイ温度の“目安”）

- **CH32V003 に校正済みダイ温度センサは無い**（内部 ADC は `Vrefint(ch8)` / `Vcalint(ch9)` のみ）。
- 実装（`adc.c`）：`ADC_TSVREFE` を有効化し **Vrefint を測定 → 電源電圧 VDD を推定**
  （`VDD = 1200mV × 4095 / adc(ch8)`）。ブラウンアウト監視に有用（[[aqua-device-ops-quirks]] の 100% ファン電圧降下対策に整合）。
- ホストは `0x04 READ_HEALTH`（8B）で取得：`vdd_mv / vrefint_raw / temp_c10(=0, 目安未提供) / flags(bit0=proxy)`。
- メインループで約 1Hz サンプリング→キャッシュ。**ADC 変換は ISR で行わない**（サンプル時間が長く I2C を止めるため）。
- 真の℃が必要なら外部温度センサ、または Vrefint ドリフトの基板別校正が前提（本チップ単体では非推奨）。

## 17. LCO 校正値のフラッシュ保存（`nvcal.c`）

- **オプションバイトのユーザデータ `Data0/Data1`** に保存（コードページ消去不要・低リスク）。
  `Data0=TUN_CAP(4bit)`, `Data1=magic(0xA5)`。
- 起動時：`as3935_setup()`→ `nvcal_load()` 成功なら **保存値を `as3935_apply_tuncap()` で適用し校正スキップ（高速起動）**。
  無ければ `as3935_calibrate()` 実行→ `nvcal_save()`。
- ホスト `0x41 CMD subcmd=0x01`（再校正）で **再測定＋フラッシュ更新**。

## 18. ウォッチドッグ（IWDG）

- `main.c` で IWDG 起動（LSI ~128kHz, /32, `IWDG_TIMEOUT_MS`=200ms）。メインループで `iwdg_kick()`。
- SW-I2C 等の長処理は全てタイムアウト付なので、正常時はループが回り続け kick される。暴走時のみリセット（§14 E に合流）。

## 15. 未確定事項（実装時に確定）

1. DMA1 チャネル番号（TX=CH6/RX=CH7 想定、RM 実確認）※上位HW-I2C側は実機未検証
2. ~~J4M6 物理ピン番号~~ → **確定**（実機動作、SOP8PinOut.txt）
3. ~~AS3935 I2C アドレス~~ → **0x00 確定**（実機応答）
4. LCO 校正方式 → **方式B（ポーリング計数）実装・実機成功**（TUN_CAP=4）。方式Aも config で選択可
5. Busy 粒度 → `BUSY_WHOLE_CAPTURE=1`（capture 全体）で運用
6. ~~上位スレーブアドレス~~ → **0x28 確定**（config.h）
7. ~~フラッシュ/RAM~~ → **確定**（triple/32 全機能込み FLASH 53% / RAM 65%、[../TESTLOG.md](../TESTLOG.md)）
