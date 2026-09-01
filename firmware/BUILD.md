# ThunderSense firmware - Build

Target: **CH32V003J4M6** (SOP-8), framework: **ch32fun**, toolchain: `riscv-none-elf-gcc`.

Two verified build paths. Both produce the same firmware.

Measured (2026-09): **FLASH 7896 B / 16 KB (48%)**, **RAM 932 B / 2 KB (46%)**.

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
