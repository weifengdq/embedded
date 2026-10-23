#!/bin/bash
# 交叉编译外设例程（peripherals/），目标 arm32 (RK3506G2)。
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"

PERI="$REPO_DIR/peripherals"
[ -d "$PERI" ] || die "找不到 $PERI"

# 确保工具链在 PATH
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

rc=0
for d in gpio_demo pwm_demo rpmsg_demo mqtt_demo; do
  [ -d "$PERI/$d" ] || continue
  log "编译 $d ..."
  if [ "$d" = "mqtt_demo" ]; then
    ( cd "$PERI/$d" && make clean >/dev/null 2>&1 || true; make BR_OUTPUT="$SDK_DIR/buildroot/output/rockchip_rk3506" ) || rc=1
  else
    ( cd "$PERI/$d" && make clean >/dev/null 2>&1 || true; make ) || rc=1
  fi
done

log "产物："
find "$PERI" -maxdepth 2 -type f -perm -u+x ! -name Makefile ! -name "*.sh" -exec file {} \; 2>/dev/null | grep -i "ELF" || true
exit $rc
