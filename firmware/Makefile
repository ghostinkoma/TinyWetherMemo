# ThunderSense firmware Makefile (ch32fun) - VERIFIED build path.
#
# Requires: ch32fun (https://github.com/cnlohr/ch32fun) + RISC-V GCC
#           (riscv-none-elf-gcc) and, for flashing, a WCH-LinkE (minichlink).
#
# Build only (prints RAM/Flash usage):
#   make main.bin CH32FUN=/path/to/ch32fun/ch32fun
# Build + flash:
#   make          CH32FUN=/path/to/ch32fun/ch32fun
#
# Example (this machine):
#   make main.bin CH32FUN=D:/hobby/AquaWithWaterboader/work/ch32fun/ch32fun
#
# Measured 2026-09: FLASH ~7.9 KB / 16 KB (48%), RAM 932 B / 2 KB (46%).

TARGET     := main
TARGET_MCU ?= CH32V003
CH32FUN    ?= ../ch32fun/ch32fun

# All modules except main.c (which is $(TARGET).c).
ADDITIONAL_C_FILES := crc16.c softclock.c swi2c.c as3935.c bundle.c \
                      capture.c i2c_slave.c health.c debug.c adc.c nvcal.c

all : flash

include $(CH32FUN)/ch32fun.mk

flash : cv_flash        # WCH-LinkE via minichlink
clean : cv_clean
