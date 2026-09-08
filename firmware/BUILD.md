# ThunderSense firmware - Build

Target: **CH32V003J4M6** (SOP-8), framework: **ch32fun**, toolchain: `riscv-none-elf-gcc`.

Two verified build paths. Both produce the same firmware.

Measured (2026-09, 採用構成 triple/32 + 全機能, DEBUG=0): **FLASH 8964 B / 16 KB (55%)**,
**RAM 1344 B / 2 KB (66%)**。(最小構成 double/32 は 7896B/48% / 932B/46%)。

> クロックは既定 **48MHz**(HSI×PLL2)。省電力の **24MHz 化**(PLL停止)は [funconfig.h](funconfig.h) と
> [config.h](config.h) の `SYS_CLK_HZ` を 24000000 に合わせる(保留オプション。下記書込み注意も参照)。

---

## A. Makefile (ch32fun) - verified

Requires the ch32fun checkout and RISC-V GCC in PATH.

```bash
# build only (prints RAM/Flash usage)
make main.bin CH32FUN=/path/to/ch32fun/ch32fun
# build + flash (WCH-LinkE via minichlink)
make          CH32FUN=/path/to/ch32fun/ch32fun
```

This machine:
```bash
make main.bin CH32FUN=D:/hobby/AquaWithWaterboader/work/ch32fun/ch32fun
```

## B. PlatformIO (platform: ch32v) - verified

One-time: make ch32fun visible as `./ch32fun` (symlink or copy):

```bash
# POSIX
ln -s /path/to/ch32fun/ch32fun ch32fun
# Windows (admin cmd)
mklink /D ch32fun D:\hobby\AquaWithWaterboader\work\ch32fun\ch32fun
```

Then:
```bash
pio run                # build
pio run -t upload      # flash (WCH-LinkE)
```

`gen_ldscript.py` generates the linker script from the board's MCU; `platformio.ini`
selects `board = genericCH32V003J4M6`.

---

## 書込み注意 (WCH-LinkE ファーム v2.17)

WCH-LinkE **v2.17** と PIO/ch32fun 同梱 **minichlink** の組み合わせで、`Interface Setup` 後に
`Error sending WCH command (on recv): ...` が出て**書込みが停止**する事象を確認（**読取り/haltは成功・
バイナリ内容とは無関係**。24MHz/48MHz どちらのビルドでも同一症状で再現）。回避策:

1. **WCH-LinkUtility (WCH公式GUI)** で書込む: chip=CH32V003 / `.pio/build/genericCH32V003J4M6/firmware.bin`
   を開始アドレス **0x08000000** へ Download（最も確実）。
2. **minichlink を ch32fun 最新版でビルドし直す**（v2.17 対応の新シーケンス）。
3. WCH-LinkUtility で **LinkE ファームをダウングレード**してから現 minichlink で書く。

`pio run -t upload` の openocd(wch-link) は本環境で `WLink Open Error`（ドライバ/インターフェイス
要因）になるため、上記のいずれかを使う。

## Debug console

Set `DEBUG 1` in [config.h](config.h) to enable `DBG()` output over the SDI
debug channel (no extra pin). Leave `DEBUG 0` for production (all debug code and
`printf` are compiled out).

## Buffer sizing

`N_BINS` / `N_BUNDLES` live in [config.h](config.h). The `_Static_assert` in
[mem.h](mem.h) fails the build if the static footprint would leave less than
`SRAM_STACK_MIN` (640 B) of stack. See Docs/SPEC.md §11 for the measured
double/triple x 32/64 comparison (summary: double/32 safe, triple/32 OK,
64-bin not recommended).
