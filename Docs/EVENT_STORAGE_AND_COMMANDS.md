# Event Buffer & Command Path — Architecture Note (CH32V003 bridge)

*English (this file) · [日本語 → EVENT_STORAGE_AND_COMMANDS_JP.md](EVENT_STORAGE_AND_COMMANDS_JP.md)*

**Question that prompted this note:** *Can we increase the number of stored lightning events (N_BINS)? And under a burst of simultaneous lightning + host commands, does a larger buffer starve the command path?*

**Short answer:** Event storage (`N_BINS`) and the command buffer are **orthogonal**. Raising `N_BINS` does **not** shrink or slow the command path. The real limits are (a) a deliberate **640 B stack reserve** that caps `N_BINS` at ~35, and (b) a separate, pre-existing **single-slot command buffer** (`CMDBUF_DEPTH = 1`) that can silently drop a command under concurrent load — independent of `N_BINS`. **Decision: keep `N_BINS = 32`.**

---

## 1. Event storage model

- **Triple buffer** (`N_BUNDLES = 3`, [bundle.c](../firmware/bundle.c)): each slot rotates through roles **FILLING → SERVING → DIRTY(clearing) → CLEAN**. The 3rd slot lets the slow zero-clear run fully decoupled from fill and serve (SPEC §9).
- **Bundle wire layout** ([protocol.h](../firmware/protocol.h)): `status(1) + length(1) + gen(1) + bins[N_BINS]×12 + crc16(2)` = **5 + 12·N_BINS** bytes. At `N_BINS = 32` → **389 B**.
- **`EventBin` = 12 B fixed** (`_Static_assert` enforced).
- **Only LIGHTNING events are stored in bins.** Disturbers and noise are **counted only** (monotonic totals), never consuming a bin slot ([bundle.c](../firmware/bundle.c) `bundle_push`). So "N_BINS = 32" means *up to 32 lightning strikes* held between ACKs.
- **On full:** top-N-by-energy replacement (weakest strike is evicted) + `lost_total++` + overflow flag. The **monotonic counters never lose the count** — only per-strike detail of the weakest strikes beyond N is dropped.
- **Loss-safe delivery:** CRC16 over `[length, gen, bins]`, non-destructive read, and a `(gen, crc)`-qualified CLEAR — the bridge drops a bundle only after the host's read verifies.
- **Config knob:** `MAX_EVENT_COUNT` ([config.h](../firmware/config.h)) is the single place to change the count (`N_BINS` is its alias). It **must equal** the host's `TS_MAX_EVENTS` — the two form the wire contract (`bundle size = 3 + 12·N + 2`); a mismatch shows up as a CRC error.

---

## 2. RAM budget and the `N_BINS` ceiling

CH32V003 has **2 KB SRAM and no heap** (all buffers static, [mem.h](../firmware/mem.h)).

- **Static footprint ≈ `204 + 36·N_BINS`** bytes. At `N_BINS = 32` → **1356 B** (matches the linker `.map`).
- **Cost per +1 event = 36 B** (12 B × the triple buffer).
- **Build-time guard ([mem.h](../firmware/mem.h)):** a `_Static_assert` insists on keeping **`SRAM_STACK_MIN = 640 B`** free for **ISR nesting** (EXTI → 2 ms timer → I2C slave → DMA) plus SW-I2C frames.

| N_BINS | static RAM | free stack | bundle wire | verdict |
|---|---|---|---|---|
| **32 (current)** | 1356 B | 692 B | 389 B | ✅ |
| 33 | 1392 B | 656 B | 401 B | ✅ honours 640 B |
| 35 | 1464 B | 584 B | 425 B | ⚠ passes the assert (est.), real reserve < 640 B |
| **40** | 1644 B | 404 B | 485 B | ❌ **build blocked** (violates 640 B) |
| 48 | — | 116 B | 581 B | ❌ |

- The static assert (using an estimate) permits up to **N_BINS ≤ 35**; honouring the real `.map` (misc ≈ 189 B) the true ceiling is **N_BINS ≈ 33**.
- **`N_BINS = 40` is correctly rejected by the build** — it leaves only 404 B of stack, 236 B below the 640 B ISR reserve.
- **To exceed 35** you must first *measure* the real peak stack (`TS_DEBUG_STACK` + `mem_stack_paint()` / `mem_stack_used()`), and only then lower `SRAM_STACK_MIN` with evidence.

---

## 3. Command path

- **Control/data split** ([i2c_slave.c](../firmware/i2c_slave.c) header): the I2C-slave ISR **only** receives command bytes into a buffer and arms DMA for reads. **Side-effecting commands execute later, in the main loop** (`i2c_slave_process_cmd`), so real-time lightning capture (EXTI, higher priority) is never blocked.
- **Single command slot — `CMDBUF_DEPTH = 1`** (`g_cmd`). At STOP of a write transaction ([i2c_slave.c](../firmware/i2c_slave.c):202):

  ```c
  if (g_rxn >= 1 && !g_cmd.pending) {   // accept ONLY if the previous cmd is drained
      g_cmd.opcode = ...; g_cmd.pending = 1;
  }                                     // else: silently dropped (no counter, no NACK)
  ```

- **Concurrency:** during a ~3 ms **BUSY capture** the main loop is busy reading the AS3935, so it does not drain commands. A **second** command that arrives in that window is **dropped**.
- **Loss-safety, per command:**
  - `CMD_CLEAR` (the host's ACK): a dropped CLEAR is **safe** — the bundle is simply re-served and re-ACKed next poll (retry-safe by design).
  - `CMD_SET_TIME`: periodic; a dropped one is retried on the next cycle.
  - **Calibration (`NF_LEV` / `WDTH` / `SREJ` / …): a dropped command is silently NOT applied.** The host still gets HTTP 200, so it cannot tell the register was not set. **This is the real gap.**

---

## 4. Key finding — `N_BINS` ⟂ command path (orthogonal)

- Event bins (`g_bundle[]`) and the command buffer (`g_cmd`, `g_rxbuf[8]`) are **separate memory**.
- Raising `N_BINS` adds only **~1–2 µs of CRC** in the read-serve ISR and a longer **(hardware DMA)** transfer — it does **not** reduce command capacity or change the drop dynamics (which are governed by the main-loop drain rate vs. command arrival rate and the ~3 ms BUSY windows).
- Therefore *"a larger `N_BINS` starves the command buffer"* is **false**, and increasing `N_BINS` neither causes nor fixes the command drop.
- The command-drop risk is **`CMDBUF_DEPTH = 1`**, independent of `N_BINS`, already present at 32.

---

## 5. Decision

- **Keep `N_BINS = 32` (`MAX_EVENT_COUNT = 32`).** The real benefit of increasing is marginal: bins hold lightning only, the monotonic counters never lose the count, and top-N-by-energy already preserves the strongest strikes; the bins rarely fill given frequent host polling. And any increase is orthogonal to command safety.
- The `MAX_EVENT_COUNT` knob, the host `TS_MAX_EVENTS` "MUST match" note, and the `mem.h` guard are kept as **future groundwork** — a later increase is a one-line change on each side, and the build guard blocks an unsafe value.
- **If heavy concurrent-command robustness is ever required, it is a *separate* workstream** (not a buffer-size change):
  1. Deepen `g_cmd` into a ring of `CMDBUF_DEPTH` to absorb bursts.
  2. Count dropped commands (`g_cmd_lost++`) and expose it via the STATUS register so the host can notice.
  3. Have the host **read back** critical settings (e.g. compare `/api/settings` against the applied registers) after sending them.
- **Before spending any RAM on more bins, observe `lost_total` over months of real operation** — increase only if real overflow is actually seen.

---

## 6. References
- [firmware/config.h](../firmware/config.h) — `MAX_EVENT_COUNT`, `N_BUNDLES`, `CMDBUF_DEPTH`
- [firmware/protocol.h](../firmware/protocol.h) — `EventBin`, `Bundle`
- [firmware/bundle.c](../firmware/bundle.c) — triple buffer, `bundle_push`, freeze/clear
- [firmware/i2c_slave.c](../firmware/i2c_slave.c) — command path (ISR receive / main-loop execute)
- [firmware/mem.h](../firmware/mem.h) — SRAM budget guard, stack high-water tooling
- host lib `TS_MAX_EVENTS` ([arduino/ThunderSense/ThunderSense.h](../arduino/ThunderSense/ThunderSense.h)) — the wire contract with `MAX_EVENT_COUNT`
