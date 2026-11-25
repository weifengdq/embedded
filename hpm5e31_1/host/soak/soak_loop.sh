#!/usr/bin/env bash
# 耐久测试守护：runner 异常退出自动拉起（同一 tag 续写日志），正常跑完（DONE）则退出。
# 用法: bash host/soak/soak_loop.sh <tag> [hours] [runner 其它参数...]
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
TAG="${1:-$(date +%Y%m%d-%H%M%S)}"
HOURS="${2:-15}"
LOGDIR="$repo/logs/soak-$TAG"
mkdir -p "$LOGDIR"
echo "[supervisor] start tag=$TAG hours=$HOURS at $(date -Is) pid=$$" >> "$LOGDIR/supervisor.log"
for i in $(seq 1 30); do
  if [ -f "$LOGDIR/DONE" ]; then
    echo "[supervisor] DONE found, exit at $(date -Is)" >> "$LOGDIR/supervisor.log"
    break
  fi
  python3 "$here/soak_runner.py" --duration-h "$HOURS" --tag "$TAG" "${@:3}" >> "$LOGDIR/stdout.log" 2>&1
  rc=$?
  echo "[supervisor] attempt=$i rc=$rc at $(date -Is)" >> "$LOGDIR/supervisor.log"
  [ -f "$LOGDIR/DONE" ] && break
  sleep 10
done
