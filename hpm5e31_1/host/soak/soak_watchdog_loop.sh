#!/usr/bin/env bash
# 看门狗循环（后台跑）：直到 DONE 出现
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
TAG="${1:?tag}"
LOG="$repo/logs/soak-$TAG"
while [ ! -f "$LOG/DONE" ]; do
  bash "$here/soak_watchdog.sh" "$TAG"
  sleep 120
done
echo "$(date -Is) watchdog loop exit (DONE)" >> "$LOG/watchdog.log"
