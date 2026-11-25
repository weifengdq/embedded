#!/usr/bin/env bash
# run_interop.sh -- 板端 someip_rpc + 主机 vsomeip 客户端互操作（一次性、带环境修复）
#
# 两个关键环境前提（U-35-1 实测遇到）：
#   1) 主机必须有到 SD 组播的**路由**，否则 vsomeip 不会在接板网卡上 IGMP join：
#        ip route get 224.244.224.245   -> 若走默认路由(enp3s0/SSH 网口) 就是错的
#        修复: sudo ip route replace 224.244.224.245/32 dev <接板网卡>
#   2) vsomeip 在自定义前缀安装时，插件(libvsomeip3-cfg/sd)要能被 dlopen：
#        LD_LIBRARY_PATH=<prefix>/lib
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IFACE="${IFACE:-enp6s0}"
VSDIR="${VSOMEIP_PREFIX:-$HOME/tmp/tp-someip/vsomeip-install}"

echo "[interop] 1) 修组播路由（$IFACE）"
ip route get 224.244.224.245 2>/dev/null | head -1
if ! ip route get 224.244.224.245 2>/dev/null | grep -q "dev ${IFACE}"; then
    # 注意带上 src：enp6s0 上同时有 192.168.0.1（历史配置）和 192.168.2.1，
    # 不指定 src 时内核会挑 192.168.0.1，vsomeip 的 SD socket 绑定的是 192.168.2.1，
    # 对端单播回包会发到 192.168.0.1 上（同网卡也能收，但语义不清）
    sudo ip route replace 224.244.224.245/32 dev "${IFACE}" src 192.168.2.1 || echo "[interop] WARN route add failed"
fi
ip route get 224.244.224.245 2>/dev/null | head -1

echo "[interop] 2) 清理上次残留的 routing manager（否则报 'other routing manager present'，vsomeip 直接不初始化）"
pkill -f "vsomeip/build/examples" 2>/dev/null || true
pkill -x someip_client 2>/dev/null || true   # 注意用 -x 精确匹配进程名，-f 会匹配到本脚本自己的命令行（遇到过：rc=15 自杀）
sleep 0.3
sudo rm -f /tmp/vsomeip-0 /tmp/vsomeip-1 2>/dev/null || rm -f /tmp/vsomeip-0 /tmp/vsomeip-1 2>/dev/null || true
ls /tmp/vsomeip-* 2>/dev/null || echo "[interop] /tmp/vsomeip-* 已清空"

echo "[interop] 3) 跑 vsomeip 客户端"
export VSOMEIP_CONFIGURATION="${VSOMEIP_CONFIGURATION:-${here}/config/someip_client.json}"
export VSOMEIP_APPLICATION_NAME="${VSOMEIP_APPLICATION_NAME:-someip_client}"
export LD_LIBRARY_PATH="${VSDIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec timeout "${VSTIMEOUT:-70}" "${here}/build/someip_client" "$@"