#!/bin/bash
# 安装 Python 2.7（Ubuntu 24.04+ 官方源已移除），U-Boot 构建脚本 `make PYTHON=python2` 需要。
# 做法：下载 Ubuntu jammy(universe) 的 python2.7 相关 .deb，用 dpkg-deb -x 解包到 /，不做 apt 安装。
set -eu

if [ "$(id -u)" != "0" ]; then
  echo "请用 sudo 运行： sudo bash $0" >&2
  exit 1
fi

if command -v python2 >/dev/null 2>&1 && python2 -c "print('ok')" >/dev/null 2>&1; then
  echo "python2 已安装： $(python2 -V 2>&1) $(command -v python2)"
  exit 0
fi

BASE="${PY2_DEB_BASE:-http://archive.ubuntu.com/ubuntu/pool/universe/p/python2.7}"
VER="${PY2_VER:-2.7.18-13ubuntu1.5}"
DEBS="libpython2.7-minimal_${VER}_amd64.deb libpython2.7-stdlib_${VER}_amd64.deb python2.7-minimal_${VER}_amd64.deb python2.7_${VER}_amd64.deb"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"

for f in $DEBS; do
  echo "==> download $f"
  curl -fsSL -o "$f" "$BASE/$f"
done

for f in $DEBS; do
  echo "==> extract $f -> /"
  dpkg-deb -x "$f" /
done

# 建立 python2 命令
[ -e /usr/bin/python2 ] || ln -sf /usr/bin/python2.7 /usr/bin/python2
ln -sf /usr/bin/python2.7 /usr/local/bin/python2

echo "==> 验证"
python2 -V
python2 -c "print('python2 ok')"
