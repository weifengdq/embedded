#!/bin/bash
# 配置环境变量。用法：  source scripts/04-setup-env.sh
# 幂等，可重复 source。
# 说明：会一并 source scripts/lib/common.sh，从而获得
#       GCC15 兼容设置（HOSTCC 包装器 + HOST_CFLAGS），
#       这样之后直接运行 SDK 的 ./build.sh 也能生效。

# 自行定位仓库
_RK_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export RK3506_REPO="${RK3506_REPO:-$_RK_REPO}"

# 载入公共变量与 GCC15 兼容设置
. "$_RK_REPO/scripts/lib/common.sh"

# 交叉编译工具链
export CROSS_COMPILE="${CROSS_COMPILE:-arm-none-linux-gnueabihf-}"
export ARCH=arm
export TOOLCHAIN_BIN="$TOOLCHAIN_DIR/bin"
case ":$PATH:" in
  *":$TOOLCHAIN_BIN:"*) ;;
  *) export PATH="$TOOLCHAIN_BIN:$PATH" ;;
esac

echo "HOSTCC         = $HOSTCC"
echo "SDK_DIR        = $SDK_DIR"
echo "TOOLCHAIN_DIR  = $TOOLCHAIN_DIR"
echo "CROSS_COMPILE  = $CROSS_COMPILE"
echo "DEFCONFIG      = $DEFCONFIG"
if [ -x "$TOOLCHAIN_BIN/${CROSS_COMPILE}gcc" ]; then
  echo -n "toolchain ver  = "; "$TOOLCHAIN_BIN/${CROSS_COMPILE}gcc" -dumpversion
else
  echo "toolchain      = (未找到，先运行 scripts/03-extract-sources.sh)"
fi
