#!/usr/bin/env bash
# 耐久测试外部看门狗：每 120 s 扫一次 events.log 的新增行，
# 若出现 >=2 次 phase_recover / traffic_stalled / phase_no_traffic / recover_begin，
# 用 J-Link 复位板子（r;g，不重新烧写）——复位后下一轮 eth iserver 会重新建监听，
# 修掉"监听态卡死但 ping/串口都正常"这一类 runner 自己修不了的状态。
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
TAG="${1:?tag}"
LOG="$repo/logs/soak-$TAG"
EV="$LOG/events.log"
OFF="$LOG/.wd_offset"
[ -d "$LOG" ] || exit 1
touch "$OFF"
off=$(cat "$OFF" 2>/dev/null || echo 0)
size=$(stat -c%s "$EV" 2>/dev/null || echo 0)
if [ "$size" -lt "$off" ]; then off=0; fi
new=$(tail -c +$((off+1)) "$EV" 2>/dev/null || true)
echo "$size" > "$OFF"
n=$(printf '%s' "$new" | grep -c -E "phase_recover|traffic_stalled|phase_no_traffic|recover_begin" || true)
if [ "${n:-0}" -ge 2 ]; then
  echo "$(date -Is) watchdog: $n failure events in window -> JLink reset" >> "$LOG/watchdog.log"
  printf 'JTAGConf -1,-1\nr\nh\nr\ng\nqc\n' > /tmp/wd_reset_$$.jlink
  JLinkExe -NoGui 1 -AutoConnect 1 -Device HPM5E31xGNx -If JTAG -Speed 4000 -ExitOnError 1 \
      -CommandFile /tmp/wd_reset_$$.jlink >> "$LOG/watchdog.log" 2>&1
  rm -f /tmp/wd_reset_$$.jlink
fi
