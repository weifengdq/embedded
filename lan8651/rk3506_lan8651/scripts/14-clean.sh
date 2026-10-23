#!/bin/bash
# 清理（谨慎：cleanall 会删除所有编译产物，需完全重编）
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; . "$HERE/lib/common.sh"
target="${1:-}"
[ -n "$target" ] || die "用法: $0 <cleanall|clean-kernel|clean-rootfs|clean-loader|clean-firmware|clean-updateimg>"
run_build "clean-${target}" "$target"
