#!/usr/bin/env bash
# TC387 Linux GCC build & flash helper (analogous to build.ps1 on Windows)
# Usage: ./build.sh [action] [options]
#   action: configure|build|rebuild|clean|download|reset|all  (default build)
#   options:
#     --compiler gcc|tasking (default gcc, only gcc supported on Linux)
#     --build-type Debug|Release|RelWithDebInfo|MinSizeRel (default Debug)
#     --build-dir <path> (default build/gcc)
#     --target <name> (default tc387_lwip_iperf_gcc)
#     --flash-tool <path> (default: ref/aurix_flasher_linux-master/linux/aurix_flasher)
#     --id <boardId> (default 0)
# Examples:
#   ./build.sh build
#   ./build.sh download
#   ./build.sh rebuild --build-type Release
#   ./build.sh clean
#   ./build.sh all
#   ./build.sh reset
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
DEFAULT_COMPILER="gcc"
DEFAULT_BUILD_TYPE="Debug"
DEFAULT_TARGET="tc387_1"
DEFAULT_FLASH_TOOL="$PROJECT_ROOT/../ref/aurix_flasher_linux-master/linux/aurix_flasher"
DEFAULT_TAS_SERVER="$PROJECT_ROOT/../ref/aurix_flasher_linux-master/src/tas_server"

ACTION="build"
COMPILER="$DEFAULT_COMPILER"
BUILD_TYPE="$DEFAULT_BUILD_TYPE"
BUILD_DIR=""
TARGET_NAME="$DEFAULT_TARGET"
FLASH_TOOL="$DEFAULT_FLASH_TOOL"
DAS_PORT=0
VERBOSE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    configure|build|rebuild|clean|download|reset|all)
      ACTION="$1"; shift ;;
    --compiler)
      COMPILER="$2"; shift 2 ;;
    --build-type|--build_type)
      BUILD_TYPE="$2"; shift 2 ;;
    --build-dir|--build_dir)
      BUILD_DIR="$2"; shift 2 ;;
    --target)
      TARGET_NAME="$2"; shift 2 ;;
    --flash-tool|--flash_tool)
      FLASH_TOOL="$2"; shift 2 ;;
    --id)
      DAS_PORT="$2"; shift 2 ;;
    --verbose|-v)
      VERBOSE=1; shift ;;
    -h|--help)
      echo "TC387 build.sh - Linux GCC build & flash helper"
      echo "Usage: $0 [action] [options]"
      echo "  action: configure|build|rebuild|clean|download|reset|all (default build)"
      echo "  --compiler gcc|tasking"
      echo "  --build-type Debug|Release|RelWithDebInfo|MinSizeRel"
      echo "  --build-dir <path>"
      echo "  --target <name>"
      echo "  --flash-tool <path>"
      echo "  --id <boardId>"
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

if [[ -z "$BUILD_DIR" ]]; then
  BUILD_DIR="build/$COMPILER"
fi
BUILD_PATH="$PROJECT_ROOT/$BUILD_DIR"
TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/tricore-gcc-toolchain.cmake"
if [[ "$COMPILER" == "tasking" ]]; then
  TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/tasking-tricore-toolchain.cmake"
fi

ELF_PATH="$BUILD_PATH/$TARGET_NAME.elf"
HEX_PATH="$BUILD_PATH/$TARGET_NAME.hex"
FLASH_LOG="$BUILD_PATH/${TARGET_NAME}_flasher_log.xml"

# Detect toolchain bin
detect_toolchain() {
  if [[ "$COMPILER" == "tasking" ]]; then
    if [[ ! -f "$TOOLCHAIN_FILE" ]]; then echo "TASKING toolchain file missing: $TOOLCHAIN_FILE" >&2; exit 1; fi
    return 0
  fi
  # GCC: try /opt/tricore-gcc/bin
  if command -v tricore-elf-gcc >/dev/null 2>&1; then
    echo "Using tricore-elf-gcc: $(which tricore-elf-gcc) ($(tricore-elf-gcc --version | head -n1))"
    return 0
  fi
  if [[ -x "/opt/tricore-gcc/bin/tricore-elf-gcc" ]]; then
    export PATH="/opt/tricore-gcc/bin:$PATH"
    echo "Added /opt/tricore-gcc/bin to PATH"
    return 0
  fi
  echo "ERROR: tricore-elf-gcc not found. Install toolchain:" >&2
  echo "  1) Download https://github.com/NoMore201/tricore-gcc-toolchain/releases/download/13.4.1/tricore-gcc-13.4.1-linux.tar.gz" >&2
  echo "  2) sudo mkdir -p /opt/tricore-gcc-13.4.1 && sudo tar -xzf tricore-gcc-13.4.1-linux.tar.gz -C /opt/tricore-gcc-13.4.1 --strip-components=1" >&2
  echo "  3) sudo ln -sf /opt/tricore-gcc-13.4.1 /opt/tricore-gcc && export PATH=/opt/tricore-gcc/bin:\$PATH" >&2
  exit 1
}

do_configure() {
  detect_toolchain
  if [[ ! -f "$TOOLCHAIN_FILE" ]]; then echo "Toolchain file not found: $TOOLCHAIN_FILE" >&2; exit 1; fi
  echo "=== Configure | Compiler=$COMPILER | BuildType=$BUILD_TYPE ==="
  echo "  BuildDir   : $BUILD_PATH"
  echo "  Toolchain  : $TOOLCHAIN_FILE"
  cmake -S "$PROJECT_ROOT" -B "$BUILD_PATH" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
}

do_build() {
  do_configure
  echo "=== Build | $BUILD_TYPE ==="
  cmake --build "$BUILD_PATH" --config "$BUILD_TYPE" -- -j$(nproc)
  echo "Build done. ELF: $ELF_PATH  HEX: $HEX_PATH"
  if [[ -f "$ELF_PATH" ]]; then
    tricore-elf-size --format=berkeley "$ELF_PATH" || true
  fi
}

do_clean() {
  if [[ -d "$BUILD_PATH" ]]; then
    echo "=== Clean $BUILD_PATH ==="
    rm -rf "$BUILD_PATH"
  else
    echo "Nothing to clean: $BUILD_PATH"
  fi
}

ensure_hex() {
  if [[ ! -f "$HEX_PATH" ]]; then
    echo "HEX not found, building..."
    do_build
  fi
  if [[ ! -f "$HEX_PATH" ]]; then
    echo "ERROR: HEX still missing after build: $HEX_PATH" >&2; exit 1
  fi
}

check_tas() {
  # Try to ping TAS server on localhost:24817
  if command -v nc >/dev/null 2>&1; then
    if nc -z localhost 24817 2>/dev/null; then return 0; fi
  fi
  # try with bash tcp
  if (echo > /dev/tcp/localhost/24817) >/dev/null 2>&1; then return 0; fi
  return 1
}

do_download() {
  ensure_hex
  FLASH_EXE="$FLASH_TOOL"
  if [[ ! -x "$FLASH_EXE" ]]; then
    # try alternative location
    ALT="$PROJECT_ROOT/tools/aurix_flasher/aurix_flasher"
    if [[ -x "$ALT" ]]; then FLASH_EXE="$ALT"; fi
  fi
  if [[ ! -x "$FLASH_EXE" ]]; then
    echo "ERROR: aurix_flasher not found/executable: $FLASH_EXE" >&2
    echo "  Expected: $DEFAULT_FLASH_TOOL (chmod +x may be needed)" >&2
    echo "  Or build from ref/aurix_flasher_linux-master: cd ref/aurix_flasher_linux-master/linux && make -f Makefile_linux" >&2
    exit 1
  fi
  echo "=== Flash | HEX=$HEX_PATH | id=$DAS_PORT ==="
  echo "  Flasher: $FLASH_EXE"
  if ! check_tas; then
    echo "WARN: TAS server not running on localhost:24817" >&2
    echo "  You need Infineon DAS + TAS installed:" >&2
    echo "    1) Download DAS_v8_0_5_Linux.tar.gz from https://www.infineon.com/cms/en/product/promopages/DAS/" >&2
    echo "    2) Extract and run TAS_V1_1_0/bin/tas_server &" >&2
    echo "    3) Check: ./aurix_flasher -id list" >&2
    echo "  Trying anyway..."
    # Try to auto-start bundled tas_server if present
    if [[ -x "$DEFAULT_TAS_SERVER" ]]; then
      echo "Attempting to start bundled tas_server: $DEFAULT_TAS_SERVER"
      LD_LIBRARY_PATH="$(dirname "$DEFAULT_TAS_SERVER"):${LD_LIBRARY_PATH:-}" "$DEFAULT_TAS_SERVER" &
      TAS_PID=$!
      echo "tas_server pid $TAS_PID, waiting 2s..."
      sleep 2
      if ! check_tas; then
        echo "tas_server still not reachable, check libftd2xx.so dependency (ldd $DEFAULT_TAS_SERVER)" >&2
      fi
    fi
  fi
  set +e
  # Linux aurix_flasher supports subset of Windows params; keep minimal for compatibility
  if [[ "$(basename "$FLASH_EXE")" == *".exe" ]]; then
    "$FLASH_EXE" -hex "$HEX_PATH" -id "$DAS_PORT" -erase on -prog on -ver on -start on -log "$FLASH_LOG"
  else
    # Explicit -start on ensures Application Reset is issued; default is on but be explicit
    "$FLASH_EXE" -hex "$HEX_PATH" -id "$DAS_PORT" -start on
  fi
  RET=$?
  set -e
  if [[ $RET -ne 0 ]]; then
    echo "Flasher exit code $RET" >&2
    if [[ -f "$FLASH_LOG" ]]; then echo "Flasher log: $FLASH_LOG"; cat "$FLASH_LOG" 2>/dev/null | head -n 100; fi
    exit $RET
  fi
  echo "Flash OK. Log: $FLASH_LOG"
  # Linux flasher's final OCTRL (Application Reset, 0xF000047C) leaves CPU halted on some TC3xx.
  # Ensure CPU is running via the flasher's read path which does device_connect(RESET) + OCTRL.
  echo "=== Ensuring CPU is running (extra reset) ==="
  set +e
  "$FLASH_EXE" -id "$DAS_PORT" -read 0x80000000 > /dev/null 2>&1 || true
  set -e
  sleep 1
  echo "Reset done. Check P00.5 (0xF003A000 bit5) or serial at 921600."
}

do_reset() {
  FLASH_EXE="$FLASH_TOOL"
  if [[ ! -x "$FLASH_EXE" ]]; then echo "Flash tool not found: $FLASH_EXE" >&2; exit 1; fi
  echo "=== Reset via flasher | id=$DAS_PORT ==="
  # Linux aurix_flasher has no -reset; its -read path is the canonical reset:
  # hot attach read -> device_connect(RESET) -> OCTRL -> re-read P00.
  # We use -read 0x80000000 as a lightweight way to trigger RESET + Application Reset.
  set +e
  "$FLASH_EXE" -id "$DAS_PORT" -read 0x80000000 2>&1 | tail -n 30
  RET=${PIPESTATUS[0]:-$?}
  set -e
  if [[ $RET -ne 0 ]]; then
    echo "Reset via -read failed (code $RET), check TAS connection / -id list" >&2
    exit $RET
  fi
  echo "Reset OK. Board should be running (P00.5=1, serial 921600)."
}

case "$ACTION" in
  configure) do_configure ;;
  build) do_build ;;
  rebuild) do_clean; do_build ;;
  clean) do_clean ;;
  download) do_download ;;
  reset) do_reset ;;
  all) do_clean; do_build; do_download ;;
  *) echo "Unknown action $ACTION" >&2; exit 1 ;;
esac

echo "Done."
