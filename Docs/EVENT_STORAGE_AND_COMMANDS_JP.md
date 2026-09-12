# イベントバッファ & コマンド経路 — アーキテクチャノート（CH32V003ブリッジ）

*[English → EVENT_STORAGE_AND_COMMANDS.md](EVENT_STORAGE_AND_COMMANDS.md) · 日本語（本ファイル）*

**このノートの発端となった問い：** *格納する落雷イベント数（N_BINS）は増やせるか？ また、雷イベントとホストコマンドを立て続けに受けたとき、バッファ増大はコマンド経路を圧迫しないか？*

**結論（要約）：** イベント格納（`N_BINS`）とコマンドバッファは**直交**（独立）している。`N_BINS` を増やしてもコマンド経路は縮まず遅くもならない。実際の律速は (a) 意図的な **640B のスタック予約**（`N_BINS` を約35で頭打ちにする）と、(b) それとは別の既存の **単一スロット・コマンドバッファ**（`CMDBUF_DEPTH = 1`）——同時負荷時にコマンドを無警告で取りこぼしうるが、これは `N_BINS` とは無関係。**判定：`N_BINS = 32` を維持。**

---

## 1. イベント格納モデル

- **トリプルバッファ**（`N_BUNDLES = 3`, [bundle.c](../firmware/bundle.c)）：各スロットが **FILLING → SERVING → DIRTY(クリア中) → CLEAN** の役割を巡回。第3面が、遅いゼロクリアを fill/serve から完全に切り離す（SPEC §9）。
- **バンドル配線レイアウト**（[protocol.h](../firmware/protocol.h)）：`status(1) + length(1) + gen(1) + bins[N_BINS]×12 + crc16(2)` = **5 + 12·N_BINS** バイト。`N_BINS = 32` で **389B**。
- **`EventBin` = 12B 固定**（`_Static_assert` で保証）。
- **bins に格納されるのは落雷イベントのみ。** 妨害波・ノイズは**カウントのみ**（単調カウンタ）で bin スロットを消費しない（[bundle.c](../firmware/bundle.c) `bundle_push`）。よって「N_BINS = 32」＝ACK 間に *落雷最大32件* を保持、の意味。
- **満杯時：** エネルギー上位N置換（最弱の落雷を追い出す）＋ `lost_total++` ＋オーバーフローフラグ。**単調カウンタは総数を絶対に失わず**、失うのは N を超えた最弱落雷の個別明細だけ。
- **ロスセーフ配送：** `[length, gen, bins]` の CRC16、非破壊 read、`(gen, crc)` 指定 CLEAR——ホストの読取が検証成功して初めてブリッジは破棄する。
- **config 知恵入れ：** `MAX_EVENT_COUNT`（[config.h](../firmware/config.h)）が件数変更の唯一の場所（`N_BINS` はそのエイリアス）。ホストの `TS_MAX_EVENTS` と**必ず一致**させること——両者は配線契約（`バンドル長 = 3 + 12·N + 2`）で、不一致は CRC エラーとして現れる。

---

## 2. RAM 収支と `N_BINS` の天井

CH32V003 は **2KB SRAM・ヒープ無し**（全バッファ静的、[mem.h](../firmware/mem.h)）。

- **静的フットプリント ≈ `204 + 36·N_BINS`** バイト。`N_BINS = 32` で **1356B**（リンカ `.map` と一致）。
- **1件増あたり = 36B**（12B × トリプル）。
- **ビルド時ガード（[mem.h](../firmware/mem.h)）：** `_Static_assert` が **`SRAM_STACK_MIN = 640B`** を **ISRネスト**（EXTI → 2ms タイマ → I2Cスレーブ → DMA）＋ SW-I2C フレーム用に必ず空けることを強制。

| N_BINS | 静的RAM | スタック残 | バンドル長 | 判定 |
|---|---|---|---|---|
| **32（現状）** | 1356B | 692B | 389B | ✅ |
| 33 | 1392B | 656B | 401B | ✅ 640B を満たす |
| 35 | 1464B | 584B | 425B | ⚠ アサートは通る（推定）が実予約 < 640B |
| **40** | 1644B | 404B | 485B | ❌ **ビルド拒否**（640B 侵犯） |
| 48 | — | 116B | 581B | ❌ |

- 静的アサート（推定値使用）が許すのは **N_BINS ≤ 35**、実 `.map`（misc ≈ 189B）整合なら真の天井は **N_BINS ≈ 33**。
- **`N_BINS = 40` はビルドが正しく拒否**——スタック残 404B で 640B の ISR 予約を 236B 割り込む。
- **35 を超える**には、まず実ピークスタックを *測定*（`TS_DEBUG_STACK` ＋ `mem_stack_paint()` / `mem_stack_used()`）し、根拠を得てから `SRAM_STACK_MIN` を下げること。

---

## 3. コマンド経路

- **制御/データ分離**（[i2c_slave.c](../firmware/i2c_slave.c) ヘッダ）：I2Cスレーブ ISR は**受信（コマンドバイトをバッファへ）と read 用 DMA 準備のみ**。**副作用を持つコマンドの実行はメインループ**（`i2c_slave_process_cmd`）で行う——ゆえにリアルタイム雷キャプチャ（EXTI, 高優先）は決して塞がれない。
- **単一コマンドスロット — `CMDBUF_DEPTH = 1`**（`g_cmd`）。書込トランザクションの STOP 時（[i2c_slave.c](../firmware/i2c_slave.c):202）：

  ```c
  if (g_rxn >= 1 && !g_cmd.pending) {   // 前コマンドが処理済みの時だけ受理
      g_cmd.opcode = ...; g_cmd.pending = 1;
  }                                     // それ以外は無警告で破棄（カウンタもNACKも無し）
  ```

- **並行性：** ~3ms の **BUSY キャプチャ**中はメインループが AS3935 読取で塞がり、コマンドをドレインできない。その窓に来た**2件目**のコマンドは**破棄**される。
- **コマンド別のロス安全性：**
  - `CMD_CLEAR`（ホストのACK）：破棄されても**安全**——バンドルは次ポーリングで再サーブ・再ACK（設計上リトライ安全）。
  - `CMD_SET_TIME`：周期送信のため、破棄されても次周期で再送。
  - **校正（`NF_LEV` / `WDTH` / `SREJ` / …）：破棄されると無警告で未適用。** ホストは HTTP 200 を受け取るのでレジスタ未設定に気づけない。**これが本当のギャップ。**

---

## 4. 重要な発見 — `N_BINS` ⟂ コマンド経路（直交）

- イベント bins（`g_bundle[]`）とコマンドバッファ（`g_cmd`, `g_rxbuf[8]`）は**別メモリ**。
- `N_BINS` を増やしても、read-serve ISR に **~1〜2µs の CRC** と、より長い**（ハードウェア DMA）**転送が加わるだけ——コマンド容量も取りこぼしの力学（メインループのドレイン速度 対 コマンド到来速度、~3ms BUSY 窓）も**変えない**。
- ゆえに *「N_BINS を増やすとコマンドバッファが枯渇する」は誤り*。増設はコマンド取りこぼしを**引き起こしも直しもしない**。
- 取りこぼしリスクの本体は **`CMDBUF_DEPTH = 1`** で、`N_BINS` とは無関係、32でも既に存在する。

---

## 5. 判定

- **`N_BINS = 32`（`MAX_EVENT_COUNT = 32`）を維持。** 増やす実効メリットは僅少：bins は落雷のみ・単調カウンタが総数保全・上位N保持で最強落雷を確保、頻繁ポーリングで bins は滅多に埋まらない。しかも増設はコマンド安全性と無関係。
- `MAX_EVENT_COUNT` 知恵入れ、ホスト `TS_MAX_EVENTS` の「MUST match」注記、`mem.h` ガードは**将来の布石**として残す——後日の増設は両側1行ずつ、かつビルドガードが不安全値を弾く。
- **同時多発コマンドへの頑健化が本当に必要になった場合は、これは *別ワークストリーム***（バッファサイズ変更ではない）：
  1. `g_cmd` を `CMDBUF_DEPTH` 深さのリングにしてバーストを吸収。
  2. 破棄時に `g_cmd_lost++` を数え、STATUS レジスタ経由でホストへ露出（気づけるように）。
  3. ホスト側で重要設定を**確認読み**（送信後に `/api/settings` と実レジスタを照合）。
- **bins に RAM を割く前に、実運用で数カ月 `lost_total` を観測**し、実際に溢れが見えたときだけ増やすのが合理的。

---

## 6. 参照
- [firmware/config.h](../firmware/config.h) — `MAX_EVENT_COUNT`, `N_BUNDLES`, `CMDBUF_DEPTH`
- [firmware/protocol.h](../firmware/protocol.h) — `EventBin`, `Bundle`
- [firmware/bundle.c](../firmware/bundle.c) — トリプルバッファ, `bundle_push`, freeze/clear
- [firmware/i2c_slave.c](../firmware/i2c_slave.c) — コマンド経路（ISR受信 / メインループ実行）
- [firmware/mem.h](../firmware/mem.h) — SRAM 収支ガード, スタック高水位ツール
- host lib `TS_MAX_EVENTS`（[arduino/ThunderSense/ThunderSense.h](../arduino/ThunderSense/ThunderSense.h)）— `MAX_EVENT_COUNT` との配線契約
