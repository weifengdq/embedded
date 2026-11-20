#!/bin/bash
# 用 J-Link (JLinkExe CommanderScript) 烧写 s32k312_s2500
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="${1:-Debug}"
HEX="$SCRIPT_DIR/build/$BUILD_TYPE/s32k312_s2500.hex"
JLINK="${JLINK_EXE:-/opt/SEGGER/JLink/JLinkExe}"
DEVICE="S32K312"
SPEED="${JLINK_SPEED:-4000}"

if [ ! -f "$HEX" ]; then
    echo "找不到 $HEX, 请先 ./build.sh build $BUILD_TYPE"
    exit 1
fi

TMP=$(mktemp /tmp/flash_s32k312_XXXX.jlink)
{
    echo "device $DEVICE"
    echo "si SWD"
    echo "speed $SPEED"
    echo "connect"
    echo "r"
    echo "h"
    echo "loadfile $HEX"
    echo "r"
    echo "g"
    echo "q"
} > "$TMP"

echo "J-Link 烧写: $HEX"
"$JLINK" -CommanderScript "$TMP" | tee /tmp/flash_s32k312.log
rm -f "$TMP"

if grep -q "O.K." /tmp/flash_s32k312.log; then
    echo "烧写完成, 已复位运行"
else
    echo "烧写失败, 见 /tmp/flash_s32k312.log"
    exit 1
fi

