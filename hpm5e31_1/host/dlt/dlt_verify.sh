#!/usr/bin/env bash
# Bench C：板端 dlt_log -> 主机 UDP 3490 -> dlt_recv.py 落标准 .dlt -> dlt-viewer(xvfb) 截图
set -uo pipefail
cd "$(dirname "$0")"
PY="${PY:-$HOME/pyenv/bin/python}"
OUT="${1:-/tmp/dlt/board.dlt}"
mkdir -p /tmp/dlt
"$PY" dlt_recv.py --duration 6 --out "$OUT"
RC=$?
echo "[dlt] .dlt 文件: $(ls -l "$OUT" | awk '{print $5}') 字节"
# dlt-viewer 打开（xvfb 无头模式）+ 截图，作为"官方工具可解析"证据
pkill -f dlt-viewer 2>/dev/null; pkill -f "Xvfb :96" 2>/dev/null; sleep 0.3
( Xvfb :96 -screen 0 1600x1000x24 >/tmp/xvfb96.log 2>&1 & ); sleep 1.5
( cd /tmp/dlt && DISPLAY=:96 dlt-viewer "$OUT" >/tmp/dlt/viewer.log 2>&1 & )
sleep 9
DISPLAY=:96 ffmpeg -loglevel error -f x11grab -video_size 1600x1000 -i :96 -frames:v 1 -y /tmp/dlt/dlt_viewer.png
pkill -f dlt-viewer 2>/dev/null; pkill -f "Xvfb :96" 2>/dev/null
if [ -f /tmp/dlt/dlt_viewer.png ]; then
  echo "[dlt] dlt-viewer 截图: /tmp/dlt/dlt_viewer.png ($(ls -l /tmp/dlt/dlt_viewer.png | awk '{print $5}') 字节)"
fi
exit $RC
