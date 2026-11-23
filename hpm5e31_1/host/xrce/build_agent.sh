#!/usr/bin/env bash
# build_agent.sh —— 构建 Micro-XRCE-DDS Agent（ref2 源码 v3.0.2 + 系统 Fast DDS 3.6.2）
#
# 为什么不用官方 superbuild：
#   1) 板卡环境（远程 Ubuntu 26.04）已由 ROS 2 Lyrical 提供 Fast DDS 3.6.2 / Fast CDR 2.3.6，
#      superbuild 会联网重新拉一份，慢且容易打架；
#   2) Agent 3.0.2 依赖 spdlog 1.9.2（自带 fmt 8）。系统 libspdlog 1.15 的 fmt 10 不接受
#      Agent 里 CustomEndPoint 的 ostream 形式格式化（编译期 static_assert 失败），
#      所以这里先下载并静态编译 spdlog 1.9.2（必须 -DCMAKE_POSITION_INDEPENDENT_CODE=ON，
#      否则 Agent 的共享库链接时报 relocation R_X86_64_TPOFF32 错误）。
#
# 依赖：cmake/ninja/git/curl、/opt/ros/lyrical（Fast DDS 3.6.2 + Fast CDR 2.3.6）、
#       外网代理 http://<proxy>:7890（只用于下载 spdlog）
#
# 用法：
#   bash host/xrce/build_agent.sh [--src /path/to/Micro-XRCE-DDS-Agent] [--prefix DIR]
set -euo pipefail

SRC="${XRCE_AGENT_SRC:-ref2/Micro-XRCE-DDS-Agent}"
PREFIX_ARG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --src) SRC="$2"; shift 2;;
    --prefix) PREFIX_ARG="$2"; shift 2;;
    -h|--help) sed -n '2,20p' "$0"; exit 0;;
    *) echo "unknown arg: $1"; exit 2;;
  esac
done

WORK="${XRCE_BUILD_DIR:-${TMPDIR:-/tmp}/xrce_agent_build}"
PREFIX="${PREFIX_ARG:-${WORK}/prefix}"
ROS_DISTRO_DIR="${ROS_DIR:-/opt/ros/lyrical}"
JOBS="$(nproc)"

[ -d "$SRC" ] || { echo "ERROR: Agent 源码目录不存在: $SRC（ref2/Micro-XRCE-DDS-Agent）"; exit 3; }
[ -f "$ROS_DISTRO_DIR/setup.bash" ] || { echo "ERROR: 找不到 $ROS_DISTRO_DIR/setup.bash"; exit 3; }
# shellcheck disable=SC1090
source "$ROS_DISTRO_DIR/setup.bash"

mkdir -p "$WORK/src" "$PREFIX"

# ---- 1) spdlog 1.9.2（PIC 静态库） ----
if [ ! -f "$PREFIX/include/spdlog/spdlog.h" ]; then
  echo "[1/3] 下载并编译 spdlog 1.9.2"
  export https_proxy="${https_proxy:-http://<proxy>:7890}"
  export http_proxy="${http_proxy:-http://<proxy>:7890}"
  cd "$WORK/src"
  [ -f spdlog-1.9.2.tar.gz ] || curl -sL -o spdlog-1.9.2.tar.gz \
      https://github.com/gabime/spdlog/archive/refs/tags/v1.9.2.tar.gz
  rm -rf spdlog-1.9.2 && tar xzf spdlog-1.9.2.tar.gz
  cmake -S spdlog-1.9.2 -B spdlog-1.9.2/build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DSPDLOG_BUILD_EXAMPLE=OFF -DSPDLOG_BUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" > "$WORK/spdlog_cmake.log" 2>&1
  ninja -C spdlog-1.9.2/build > "$WORK/spdlog_build.log" 2>&1
  ninja -C spdlog-1.9.2/build install > "$WORK/spdlog_install.log" 2>&1
else
  echo "[1/3] spdlog 已存在: $PREFIX"
fi

# ---- 2) Agent ----
echo "[2/3] 配置并编译 Agent（P2P/Discovery profile 关闭：它们要 microxrcedds_client，本用例不需要）"
cd "$WORK"
rm -rf agent_build && mkdir -p agent_build && cd agent_build
cmake "$SRC" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DUAGENT_SUPERBUILD=OFF \
  -DUAGENT_P2P_PROFILE=OFF \
  -DUAGENT_DISCOVERY_PROFILE=OFF \
  -DUAGENT_USE_SYSTEM_FASTDDS=ON \
  -DUAGENT_USE_SYSTEM_FASTCDR=ON \
  -DUAGENT_USE_SYSTEM_LOGGER=ON \
  -DUAGENT_BUILD_TESTS=OFF \
  -DCMAKE_PREFIX_PATH="$PREFIX;$ROS_DISTRO_DIR" \
  -DCMAKE_INSTALL_PREFIX="$WORK/agent_install" > "$WORK/agent_cmake.log" 2>&1 || {
    echo "ERROR: cmake 失败，见 $WORK/agent_cmake.log"; tail -30 "$WORK/agent_cmake.log"; exit 4; }
ninja -j"$JOBS" > "$WORK/agent_build.log" 2>&1 || {
    echo "ERROR: 编译失败，见 $WORK/agent_build.log"; tail -40 "$WORK/agent_build.log"; exit 5; }
ninja install > "$WORK/agent_install.log" 2>&1

# ---- 3) 校验 ----
echo "[3/3] 安装结果"
AGENT="$WORK/agent_install/bin/MicroXRCEAgent"
[ -x "$AGENT" ] || { echo "ERROR: 未生成 $AGENT"; exit 6; }
LD_LIBRARY_PATH="$WORK/agent_install/lib:${LD_LIBRARY_PATH:-}" "$AGENT" --help | head -3
echo "AGENT_OK: $AGENT"
echo "提示：运行前 source $ROS_DISTRO_DIR/setup.bash 并 export LD_LIBRARY_PATH=$WORK/agent_install/lib"
