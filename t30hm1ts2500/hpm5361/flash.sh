#!/usr/bin/env bash
# 烧写 HPM5361 + T30HM1TS2500 固件 (板载 FT2232H JTAG)
#
# 必须使用带 hpm_xpi flash 驱动的 OpenOCD:
#   - 官方预编译: https://github.com/hpmicro/riscv-openocd/releases (hpm_xpi_v0.5.0)
#   - 源码编译: git clone -b hpm_xpi_v0.5.0 https://github.com/hpmicro/riscv-openocd
#               ./bootstrap && ./configure --enable-ftdi && make -j$(nproc)
# Debian/Ubuntu 自带的 openocd 0.12 没有 hpm_xpi, 会报:
#   Error: flash driver 'hpm_xpi' not found
#
# 用法:
#   ./flash.sh                            # 烧 build/output/demo.elf
#   ./flash.sh path/to/x.elf
#   OPENOCD=/opt/hpm-openocd/bin/openocd ./flash.sh
#
# openocd 查找顺序: $OPENOCD -> $HPM_OPENOCD -> 常见安装路径 -> PATH
set -euo pipefail
cd "$(dirname "$0")"

export HPM_SDK_BASE="${HPM_SDK_BASE:-${HOME}/hpm5e31/sdk_env_v1.13.0/hpm_sdk}"
export OPENOCD_SCRIPTS="${HPM_SDK_BASE}/boards/openocd"
ELF="${1:-build/output/demo.elf}"

have_hpm_xpi() {
    # grep -a 直接扫二进制里的驱动名字符串 (不用 strings|grep -q, 避免 pipefail+SIGPIPE 误判)
    [ -x "$1" ] || return 1
    grep -aq hpm_xpi "$1" 2>/dev/null
}

OCD="${OPENOCD:-}"
if [ -z "$OCD" ]; then
    for c in "${HPM_OPENOCD:-}" \
            ${HOME}/tools/ocd050/usr/local/bin/openocd \
            /opt/hpm-openocd/bin/openocd \
            "$(command -v openocd 2>/dev/null || true)"; do
        if [ -n "$c" ] && have_hpm_xpi "$c"; then OCD="$c"; break; fi
    done
fi

if [ ! -f "$ELF" ]; then
    echo "找不到 $ELF, 请先执行 ./build.sh" >&2
    exit 1
fi
if [ -z "$OCD" ] || [ ! -x "$OCD" ]; then
    echo "找不到带 hpm_xpi 的 openocd" >&2
    echo "请设置 OPENOCD=/path/to/hpm-openocd/bin/openocd (见文件头说明)" >&2
    exit 1
fi

if ! lsusb 2>/dev/null | grep -qi '0403:6010'; then
    echo "警告: 未发现 FT2232H (0403:6010), 检查 USB 线/供电" >&2
fi

COMMON_ARGS=(
    -c "set HPM_SDK_BASE ${HPM_SDK_BASE}; set BOARD hpm5300evk; set PROBE ft2232;"
    -f "${HPM_SDK_BASE}/boards/openocd/hpm5300_all_in_one.cfg"
)

echo "[1/3] OpenOCD: $OCD ($("$OCD" --version 2>&1 | head -1))"
echo "[2/3] ELF    : $ELF"
echo "[3/3] 烧写中 ..."

# 首选: 使能 FT2232H 的 nSRST 输出 (layout_init 的 OE 增加 0x0800),
#       硬复位后烧写, 最后 reset run 从复位向量启动。
if "$OCD" "${COMMON_ARGS[@]}" \
    -c "ftdi_layout_init 0x0208 0x0a0b" \
    -c "init; reset halt; program $ELF verify; reset run; exit"; then
    echo "烧写完成 (SRST reset run)"
    exit 0
fi

echo "SRST 复位烧写失败, 尝试无复位烧写 (烧写后写 PPOR 软复位)..." >&2
"$OCD" "${COMMON_ARGS[@]}" \
    -c "init; halt; program $ELF verify; mww 0xF410001C 0x10; exit"

echo "烧写完成 (无复位 + PPOR 软复位)"
