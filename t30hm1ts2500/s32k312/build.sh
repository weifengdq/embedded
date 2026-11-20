#!/bin/bash
# S32K312 + T30HM1TS2500 构建/烧写脚本
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="Debug"

usage() {
    echo "用法: $0 <build|rebuild|flash|monitor|clean|distclean> [Debug|Release]"
    echo "  build      编译 (默认 Debug)"
    echo "  rebuild    清理后重新编译"
    echo "  flash      编译并烧写 (J-Link SWD)"
    echo "  monitor    打开串口 (115200, 默认 /dev/ttyACM2, 可用 UART_DEV 覆盖)"
    echo "  clean      清理目标文件"
    echo "  distclean  删除 build 目录"
    exit 1
}

[ -z "$1" ] && usage
CMD="$1"
[ -n "$2" ] && BUILD_TYPE="$2"
cd "$SCRIPT_DIR"

case "$CMD" in
    build)
        cmake --preset $BUILD_TYPE
        cmake --build --preset $BUILD_TYPE
        ;;
    rebuild)
        rm -rf "build/$BUILD_TYPE"
        cmake --preset $BUILD_TYPE
        cmake --build --preset $BUILD_TYPE
        ;;
    flash)
        "$0" build $BUILD_TYPE
        ./flash.sh $BUILD_TYPE
        ;;
    monitor)
        DEV="${UART_DEV:-/dev/ttyACM2}"
        echo "打开 $DEV @115200 (Ctrl-A Ctrl-X 退出)"
        exec picocom -b 115200 "$DEV"
        ;;
    clean)
        cmake --build --preset $BUILD_TYPE --target clean || true
        ;;
    distclean)
        rm -rf build
        ;;
    *)
        usage
        ;;
esac

