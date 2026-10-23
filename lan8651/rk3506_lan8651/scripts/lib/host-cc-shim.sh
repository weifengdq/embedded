#!/bin/bash
# host C 编译器包装器：强制 -std=gnu17。
# 背景：Ubuntu 26.04 自带 GCC 15 默认使用 C23，导致部分旧 gnulib 代码
#       采用 `[[__nodiscard__]]` 且位置不被 GCC 接受，编译报错。
#       强制 gnu17 后 gnulib 走 `__attribute__((warn_unused_result))` 分支，正常。
# 仅包装 C 编译器（HOSTCC），不触碰 C++（HOSTCXX）。
exec /usr/bin/gcc -std=gnu17 "$@"
