#!/usr/bin/env bash
# 新建示例骨架: scripts/new_sample.sh <category> <name> [project]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/env.sh"
CAT="${1:?usage: $0 <category> <name>}"; NAME="${2:?}"; PROJ="${3:-hpm5e31_1_$NAME}"
D="$REPO_ROOT/samples/$CAT/$NAME"
mkdir -p "$D/src"
cat > "$D/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS \$ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project($PROJ)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_app_src(src/main.c)
EOF
cat > "$D/src/main.c" <<'EOF'
#include "board.h"
#include "hpm_debug_console.h"

int main(void)
{
    board_init();
    printf("hello from %s\n", BOARD_NAME);
    while (1) {
    }
    return 0;
}
EOF
echo "created $D"
