#!/bin/bash
# 公共变量与函数 —— 被其它脚本 source
# 可用环境变量覆盖，例如： SDK_DIR=/path/to/sdk bash scripts/xx.sh

# 仓库根目录（本文件位于 <repo>/scripts/lib/）
export REPO_DIR="${REPO_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"

# 厂商网盘资料（只读）
export VENDOR_DIR="${VENDOR_DIR:-/mnt/sda3/rk3506/HZ-RK3506G2_MiniEVM}"

# 工作根目录（英文路径）
export WORK_DIR="${WORK_DIR:-/mnt/sda3/rk3506/work}"

# SDK / 工具链
export SDK_DIR="${SDK_DIR:-$WORK_DIR/HZ-RK3506_MiniEVM_LINUX_SDK}"
export TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$WORK_DIR/toolchain/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf}"
export LOG_DIR="${LOG_DIR:-$WORK_DIR/logs}"

# 板级配置
export DEFCONFIG="${DEFCONFIG:-HZ-RK3506G2_MiniEVM_TF_defconfig}"

# SDK 压缩包与工具链压缩包
export SDK_TARBALL="${SDK_TARBALL:-$VENDOR_DIR/3、镜像及源码/HZ-RK3506G2_MiniEVM_Linux_SDK/HZ-RK3506_MiniEVM_LINUX_SDK.tar.gz}"
export TOOLCHAIN_TARBALL="${TOOLCHAIN_TARBALL:-$VENDOR_DIR/3、镜像及源码/交叉编译工具链/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf.tar.gz}"

export CROSS_COMPILE="${CROSS_COMPILE:-arm-none-linux-gnueabihf-}"

# --- 新主机 GCC 兼容性（Ubuntu 26.04 自带 gcc 15）--------------------------------
# Buildroot 2024.02 的部分老 host 包在新 GCC 上失败，两类问题与对策：
#  1) GCC 15 默认 C23：旧 gnulib 用 `[[__nodiscard__]]` 且位置非法 →
#     用 HOSTCC 包装器强制 `-std=gnu17`（只影响 C，不影响 C++）。
#  2) 老代码的隐式声明/指针转换在新 GCC 默认即错误 →
#     通过环境变量传入扩展的 HOST_CFLAGS/HOST_CXXFLAGS（Buildroot 用 `?=` 接收）。
# 设置 RK_NO_HOST_GCC_COMPAT=1 可整体关闭。
if [ -z "${RK_NO_HOST_GCC_COMPAT:-}" ]; then
  _shim="$WORK_DIR/bin/rk-host-cc"
  if [ ! -x "$_shim" ] || ! cmp -s "$_shim" "$REPO_DIR/scripts/lib/host-cc-shim.sh"; then
    mkdir -p "$WORK_DIR/bin"
    cp "$REPO_DIR/scripts/lib/host-cc-shim.sh" "$_shim"
    chmod +x "$_shim"
  fi
  export HOSTCC="$_shim"
  _compat="-Wno-error -Wno-error=implicit-function-declaration -Wno-error=implicit-int \
-Wno-error=int-conversion -Wno-error=incompatible-pointer-types \
-Wno-error=return-mismatch -Wno-error=declaration-missing-parameter-type \
-Wno-error=enum-int-mismatch"
  export HOST_CFLAGS="-O2 ${HOST_CFLAGS:-} ${_compat}"
  # GCC 15 的 libstdc++ 不再传递包含 <cstdint>，部分 C++ 包（如 cmake 的 cmcppdap）会报
  # “'uint32_t' has not been declared”。用 -include cstdint 兜底（仅 C++）。
  export HOST_CXXFLAGS="-O2 ${HOST_CXXFLAGS:-} ${_compat} -include cstdint"
fi

log()  { echo -e "\033[36m[rk3506] $*\033[0m"; }
warn() { echo -e "\033[33m[rk3506][WARN] $*\033[0m"; }
die()  { echo -e "\033[31m[rk3506][ERROR] $*\033[0m" >&2; exit 1; }

require_root() { [ "$(id -u)" = "0" ] || die "需要 root 权限执行"; }

# 运行 build.sh 并把日志 tee 到 $LOG_DIR
run_build() {
  local name="$1"; shift
  [ -d "$SDK_DIR" ] || die "SDK 目录不存在：$SDK_DIR"
  mkdir -p "$LOG_DIR"
  log "开始编译：$name  (log: $LOG_DIR/build_$name.log)"
  ( cd "$SDK_DIR" && ./build.sh "$@" ) 2>&1 | tee "$LOG_DIR/build_$name.log"
  local rc=${PIPESTATUS[0]}
  [ "$rc" = "0" ] || die "$name 编译失败（exit=$rc），见 $LOG_DIR/build_$name.log"
  log "$name 编译完成"
}
