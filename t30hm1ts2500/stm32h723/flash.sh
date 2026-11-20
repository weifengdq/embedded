#!/usr/bin/env bash
# STM32H723ZGT6 + T30HM1TS2500 一键烧写 (STLINK-V3, SWD)
#
# 用法:
#   ./flash.sh                      SWD 烧 build/stm32h723_s2500.elf (默认)
#   ./flash.sh app.elf              SWD 烧指定 elf
#   ./flash.sh -e                   SWD 全片擦除后再烧
#   ./flash.sh -s 4000              指定 SWD 速率 kHz (默认 4000)
#   ./flash.sh -m                   SWD attach 后只打印目标信息 (芯片/电压)
#
# 依赖: openocd (>=0.11) / stlink-tools(st-flash)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ELF="$HERE/build/stm32h723_s2500.elf"
BIN="$HERE/build/stm32h723_s2500.bin"
MODE="flash"
SPEED="4000"
FILE=""

while [ $# -gt 0 ]; do
  case "$1" in
    -e|--erase) MODE="flash_erase"; shift ;;
    -m|--monitor) MODE="monitor"; shift ;;
    -s|--speed) SPEED="$2"; shift 2 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) FILE="$1"; shift ;;
  esac
done

if [ -n "$FILE" ]; then
  case "$FILE" in
    *.elf) ELF="$FILE" ;;
    *.bin) BIN="$FILE" ;;
    *) echo "只支持 .elf / .bin"; exit 1 ;;
  esac
fi

if [ "$MODE" = "monitor" ]; then
  echo "== SWD 目标信息 =="
  openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "adapter speed $SPEED" \
          -c "init; targets; flash probe 0; reset init; mdw 0xE000ED00 1; shutdown"
  exit 0
fi

[ -f "$ELF" ] || { echo "找不到 $ELF, 先 ./build.sh"; exit 1; }
[ -f "$BIN" ] || { echo "找不到 $BIN, 先 ./build.sh"; exit 1; }

if command -v openocd >/dev/null; then
  echo "== SWD (openocd, STLINK-V3, $SPEED kHz): $ELF =="
  if [ "$MODE" = "flash_erase" ]; then
    openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
            -c "adapter speed $SPEED" -c "init; reset init; stm32h7x mass_erase 0" \
            -c "program $ELF verify reset exit" && exit 0
  else
    openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
            -c "adapter speed $SPEED" \
            -c "program $ELF verify reset exit" && exit 0
  fi
  echo "!! openocd 失败, 回退 st-flash"
fi

command -v st-flash >/dev/null || { echo "缺 st-flash: sudo apt install stlink-tools"; exit 1; }
echo "== SWD (st-flash --reset): $BIN =="
st-flash --reset write "$BIN" 0x08000000
