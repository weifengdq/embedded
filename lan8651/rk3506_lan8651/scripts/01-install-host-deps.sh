#!/bin/bash
# 安装 RK3506 SDK 主机编译依赖（Ubuntu 22.04 / 24.04 / 25.x / 26.x 通用，逐个安装失败不中断）
set -u
export DEBIAN_FRONTEND=noninteractive

if [ "$(id -u)" != "0" ]; then
  echo "请用 sudo 运行： sudo bash $0" >&2
  exit 1
fi

TCP_PKGS="git ssh make gcc g++ libssl-dev lz4 expect patchelf chrpath gawk texinfo \
diffstat binfmt-support bison flex fakeroot cmake gcc-multilib g++-multilib \
unzip device-tree-compiler libncurses-dev ncurses-dev bzip2 expat gpgv \
libgmp-dev libmpc-dev libmpfr-dev bc python3 python-is-python3 u-boot-tools \
swig cpio rsync wget curl file libelf-dev python3-pip dosfstools mtools \
e2fsprogs libncursesw5-dev"

echo "==> apt-get update"
apt-get update

ok=""; fail=""
for p in $TCP_PKGS; do
  if dpkg -s "$p" >/dev/null 2>&1; then echo "OK(already) $p"; continue; fi
  if apt-get install -y --no-install-recommends "$p" >/dev/null 2>&1; then
    echo "OK   $p"; ok="$ok $p"
  else
    echo "FAIL $p"; fail="$fail $p"
  fi
done

echo
echo "安装完成。失败（通常为新发行版已移除、可忽略）：$fail"
echo "--- 关键工具检查 ---"
for t in gcc g++ make bison flex dtc mkimage lz4 expect python3 bc; do
  printf "%-12s" "$t"; command -v "$t" >/dev/null 2>&1 && echo "OK $(command -v $t)" || echo "MISSING"
done
