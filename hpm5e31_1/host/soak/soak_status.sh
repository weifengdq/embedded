#!/usr/bin/env bash
# 看当前耐久测试状态：bash host/soak/soak_status.sh <tag>
set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
TAG="${1:-}"
D="$repo/logs/soak-$TAG"
[ -d "$D" ] || { echo "no dir: $D"; exit 1; }
echo "== status.json"; cat "$D/status.json" 2>/dev/null
echo; echo "== supervisor"; tail -5 "$D/supervisor.log" 2>/dev/null
echo; echo "== events (tail)"; tail -15 "$D/events.log" 2>/dev/null
echo; echo "== phases (tail)"; tail -5 "$D/phases.jsonl" 2>/dev/null
