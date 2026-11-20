#!/usr/bin/env bash
# TC387 + T30HM1TS2500 烧写脚本 (TAS / DAP MiniWiggler)
#
# 流程: 启动 TAS server -> aurix_flasher 擦写校验 -> 复位并运行内核
#
# 注意: aurix_flasher 以 "reset & halt" 方式连接, 烧完写的 OCTRL 应用复位
#       在调试会话仍挂住内核时不会真正跑起来 (现象: 串口无任何输出, ping 不通)。
#       因此烧写后必须再发一次 "reset & run": 这里用 -read 走 TAS 的
#       device_connect(TAS_CLNT_DCO_RESET) 路径, 用完即弃。
set -e
cd "$(dirname "$0")"

HEX=build/tc387_s2500.hex
FLASHER=${AURIX_FLASHER:-${HOME}/tc387-tools/aurix_flasher}
TAS_SERVER=${TAS_SERVER:-/opt/Tools/DAS/8.3.0/bin/tas_server}
SERIAL=${SERIAL:-/dev/ttyUSB0}

if [ ! -f "$HEX" ]; then
    echo "缺少 $HEX, 请先执行 ./build.sh"
    exit 1
fi

if ! pgrep -x tas_server >/dev/null 2>&1; then
    echo "启动 TAS server ..."
    nohup "$TAS_SERVER" >/tmp/tas_server.log 2>&1 &
    sleep 3
fi

echo "烧写 $HEX ..."
"$FLASHER" -hex "$HEX" -erase on -start on -ver on

echo "复位并运行内核 (TAS reset & run) ..."
"$FLASHER" -read 0xF003AF00 >/tmp/aurix_run.log 2>&1 || true
sleep 1

# 串口自检: 能读到 tcqspi 输出即说明固件在跑
S25="$(cd "$(dirname "$0")/.." && pwd)/tools/s25.py"
if [ -e "$SERIAL" ] && [ -f "$S25" ]; then
    if timeout 15 python3 "$S25" -p "$SERIAL" -c "tcqspi" -t 10 --idle 3 2>/dev/null | grep -q "f_SPB"; then
        echo "串口 $SERIAL: 固件已运行 (tcqspi 有响应)"
    else
        echo "串口 $SERIAL: 未响应, 复查: tools/s25.py --auto tc387 -c help"
    fi
else
    echo "跳过串口自检 (缺 $SERIAL 或 tools/s25.py)"
fi
