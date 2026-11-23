#!/usr/bin/env bash
# 跑 vsomeip 客户端（先确认板端 someip_rpc 例程在跑、板 192.168.2.100 可 ping 通）
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export VSOMEIP_CONFIGURATION="${VSOMEIP_CONFIGURATION:-${here}/config/someip_client.json}"
export VSOMEIP_APPLICATION_NAME="${VSOMEIP_APPLICATION_NAME:-someip_client}"
# vsomeip 运行时按库文件名 dlopen libvsomeip3-cfg/libvsomeip3-sd（插件），
# 装在自定义前缀时必须让动态加载器能找到，否则报 "Configuration module could not be loaded!"
VSOMEIP_PREFIX="${VSOMEIP_PREFIX:-$HOME/tmp/tp-someip/vsomeip-install}"
export LD_LIBRARY_PATH="${VSOMEIP_PREFIX}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
BIN="${here}/build/someip_client"
[ -x "${BIN}" ] || { echo "build first: cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<vsomeip prefix> && cmake --build build -j"; exit 1; }
echo "VSOMEIP_CONFIGURATION=${VSOMEIP_CONFIGURATION}"
exec "${BIN}" "$@"