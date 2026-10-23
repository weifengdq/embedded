#!/bin/bash
# 打包固件 + update.img
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"
run_build firmware firmware
run_build updateimg updateimg
log "产物：$SDK_DIR/output/firmware"
ls -la "$SDK_DIR/output/firmware" 2>/dev/null || true
