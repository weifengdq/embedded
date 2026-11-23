#!/usr/bin/env bash
# 通过 J-Link 烧写 ELF。若调用方已持有板锁 (HPM_HW_LOCK_HELD=1), 不再重复加锁。
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/env.sh"
[ $# -ge 1 ] || { echo "usage: $0 <elf>"; exit 1; }
ELF="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
if [ "${HPM_HW_LOCK_HELD:-0}" != "1" ]; then
  source "$here/hwlock.sh"
  HPM_HW_LOCK_NAME="flash:$ELF"
  hw_lock_acquire 900 || { echo "ERROR: 等待板子锁超时(900s)" >&2; exit 4; }
fi
JL="/tmp/hpm_flash_$$.jlink"
cat > "$JL" <<EOF
JTAGConf -1,-1
r
h
loadfile $ELF
r
g
qc
EOF
set +e
OUT="$(JLinkExe -NoGui 1 -AutoConnect 1 -Device "$JLINK_DEVICE" -If JTAG -Speed "$JLINK_SPEED" -ExitOnError 1 -CommandFile "$JL" 2>&1)"
RC=$?
set -e
echo "$OUT" | grep -E "Downloading|Programming|Verifying|Flash download|Error|FAILED|Cannot|O.K." | tail -12 || true
rm -f "$JL"
exit $RC
