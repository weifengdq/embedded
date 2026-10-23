#!/bin/bash
# 把内核模块通过**网络**部署到已启动的板卡（用于“模块缺失”问题的临时/在线修复）
# 前提：板卡已能联网（若模块缺失导致没有 eth0，则需先按 docs/07 手工 insmod，
#       或改用串口传输）。
#
# 用法：
#   bash scripts/40-deploy-modules.sh [板卡IP]
# 默认板卡 IP 从环境 BOARD_IP 读取。
#
# 作用：在主机起一个 HTTP 服务，把 target 里的 /lib/modules/<ver> 打成 tar.gz，
#       然后在板卡上执行 wget+解包（下方会打印需要在板卡串口/ssh 执行的命令）。
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"

BOARD_IP="${1:-${BOARD_IP:-}}"
HOST_IP="${HOST_IP:-$(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)}"
MODROOT="$SDK_DIR/buildroot/output/rockchip_rk3506/target/lib/modules"
[ -d "$MODROOT" ] || die "找不到模块目录：$MODROOT（请先编译 rootfs）"

VER="$(ls "$MODROOT" | grep -E '^[0-9]' | head -1)"
[ -n "$VER" ] || die "未找到 /lib/modules/<版本>（内核模块未安装，见 docs/07）"

OUT="$WORK_DIR/tf_card/mods.tar.gz"
mkdir -p "$(dirname "$OUT")"
( cd "$MODROOT" && tar czf "$OUT" "$VER" )
log "模块包：$OUT ($(stat -c%s "$OUT") bytes, 版本 $VER)"

PORT="${PORT:-8000}"
log "启动 HTTP 服务： http://$HOST_IP:$PORT/"
log "请在板卡上执行："
cat <<EOF

  wget -q http://$HOST_IP:$PORT/mods.tar.gz -O /tmp/mods.tar.gz
  cd /lib/modules && tar xzf /tmp/mods.tar.gz
  sync
  reboot        # 或 /sbin/reboot

EOF
if [ -n "$BOARD_IP" ]; then
  log "板卡 IP=$BOARD_IP，确认可达： ping -c1 $BOARD_IP"
fi

cd "$(dirname "$OUT")"
exec python3 -m http.server "$PORT" --bind "$HOST_IP"
