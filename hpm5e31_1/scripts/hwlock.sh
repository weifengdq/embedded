#!/usr/bin/env bash
# scripts/hwlock.sh —— 板卡互斥锁（PID 目录 + 陈旧锁自愈）
#
# 背景：旧实现 flock + 继承 fd：hwrun/flash 被 kill 后，锁 fd 可能被孤儿进程继承，
#       造成"锁永远被持有，后续所有等待者死锁"（2026-10-30 17:10 实际发生）。
# 新实现 mkdir 原子锁 + PID 存活检测：
#   - 持锁进程死亡（含 kill -9）→ 下一个等待者自动清理；
#   - 锁目录不继承给任何子进程，无 fd 泄漏；
#   - 兼容 HPM_HW_LOCK_HELD=1（flash.sh 在已持锁时不再加锁）。
#
# 用法：
#   source "$here/hwlock.sh"
#   hw_lock_acquire 600 || exit 4     # 参数=等待秒数；0=成功 1=超时
#   export HPM_HW_LOCK_HELD=1         # 告知 flash.sh 不要重复加锁
# acquire 内部已注册 EXIT/INT/TERM trap 自动释放。

HPM_HW_LOCK="${HPM_HW_LOCK:-/tmp/hpm5e31_hw.lock}"
HPM_HW_LOCKDIR="${HPM_HW_LOCK}.d"

hw_lock_release() {
  if [ -d "$HPM_HW_LOCKDIR" ] && [ "$(cat "$HPM_HW_LOCKDIR/pid" 2>/dev/null)" = "$$" ]; then
    rm -rf "$HPM_HW_LOCKDIR"
  fi
}

hw_lock_acquire() {
  local timeout t0 pid
  timeout="${1:-600}"
  t0=$(date +%s)
  while :; do
    if mkdir "$HPM_HW_LOCKDIR" 2>/dev/null; then
      printf '%s\n' "$$" > "$HPM_HW_LOCKDIR/pid"
      printf '%s\n' "$(date '+%F %T')" > "$HPM_HW_LOCKDIR/since"
      printf '%s\n' "${HPM_HW_LOCK_NAME:-$0}" > "$HPM_HW_LOCKDIR/what"
      trap 'hw_lock_release' EXIT INT TERM
      return 0
    fi
    pid=$(cat "$HPM_HW_LOCKDIR/pid" 2>/dev/null || true)
    if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
      echo "[hwlock] 陈旧锁：持有者 pid=$pid 已不存在，自动清理" >&2
      rm -rf "$HPM_HW_LOCKDIR"
      continue
    fi
    if [ -z "$pid" ] && [ -n "$(find "$HPM_HW_LOCKDIR" -maxdepth 0 -mmin +1 2>/dev/null)" ]; then
      echo "[hwlock] 无主锁目录（>1min 无 pid），自动清理" >&2
      rm -rf "$HPM_HW_LOCKDIR"
      continue
    fi
    if [ $(( $(date +%s) - t0 )) -ge "$timeout" ]; then
      echo "[hwlock] 等锁超时 ${timeout}s；持有者 pid=$pid since=$(cat "$HPM_HW_LOCKDIR/since" 2>/dev/null) what=$(cat "$HPM_HW_LOCKDIR/what" 2>/dev/null)" >&2
      return 1
    fi
    sleep 0.5
  done
}
