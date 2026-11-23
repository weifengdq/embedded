#!/usr/bin/env bash
# scripts/smoke_all.sh - 全量上板冒烟回归（板锁串行，失败不中断）
# 用法: bash scripts/smoke_all.sh [--secs 6] [--only N|名字片段,另一片段] [--no-build] [--list]
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
source "$here/env.sh"
ROOT="$REPO_ROOT"
PATTERNS="$ROOT/tools/smoke_patterns.txt"
REPORT="$ROOT/docs/generated/smoke-report.md"
SECS=6
ONLY=""
DOBUILD=1
LIST_ONLY=0
MERGE=""
LOCK_WAIT=900
if printenv HPM_LOCK_WAIT >/dev/null; then LOCK_WAIT="$(printenv HPM_LOCK_WAIT)"; fi
while [ $# -gt 0 ]; do
  case "$1" in
    --secs) SECS="$2"; shift 2;;
    --only) ONLY="$2"; shift 2;;
    --no-build) DOBUILD=0; shift;;
    --patterns) PATTERNS="$2"; shift 2;;
    --report) REPORT="$2"; shift 2;;
    --list) LIST_ONLY=1; shift;;
    --merge) MERGE="$2"; shift 2;;
    -h|--help) sed -n '2,3p' "$0"; exit 0;;
    *) echo "unknown arg: $1"; exit 2;;
  esac
done
[ -f "$PATTERNS" ] || { echo "patterns not found: $PATTERNS"; exit 2; }
mkdir -p "$(dirname "$REPORT")"
ROWS="$(mktemp /tmp/smoke_rows.XXXXXX)"
trap 'rm -f "$ROWS"' EXIT
if [ -n "$MERGE" ] && [ -f "$MERGE" ]; then cp "$MERGE" "$ROWS"; fi

pattern_re() { awk -F'\t' -v n="$1" '$1==n { print $2; exit }' "$PATTERNS"; }
pattern_send() { awk -F'\t' -v n="$1" '$1==n { print $3; exit }' "$PATTERNS"; }
pattern_secs() { awk -F'\t' -v n="$1" '$1==n { print $4; exit }' "$PATTERNS"; }
collect_samples() {
  find "$ROOT/samples" -mindepth 1 -maxdepth 3 -name CMakeLists.txt -printf '%h\n' 2>/dev/null \
    | sed "s#$ROOT/##" | sort | while read -r rel; do
        case "$(basename "$rel")" in _*) continue;; esac
        echo "$rel"
      done
}
N_ALL=$(collect_samples | wc -l)

if [ "$LIST_ONLY" = 1 ]; then
  echo "共 $N_ALL 个例程"
  collect_samples | while read -r rel; do
    echo "  $rel  pattern=[$(pattern_re "$rel")]  send=[$(pattern_send "$rel")]"
  done
  exit 0
fi

SELF=/tmp/smoke_sel.$$
if [ -n "$ONLY" ]; then
  if printf '%s' "$ONLY" | grep -qE '^[0-9]+$'; then
    collect_samples | head -n "$ONLY" > "$SELF"
  else
    : > "$SELF"
    for k in $(printf '%s' "$ONLY" | tr ',' ' '); do
      collect_samples | grep -- "$k" >> "$SELF"
    done
  fi
else
  collect_samples > "$SELF"
fi
N_RUN=$(wc -l < "$SELF")
echo "== smoke_all: $N_RUN/$N_ALL 例程, --secs $SECS, build=$DOBUILD, lock_wait=$LOCK_WAIT s =="
echo "== 报告: $REPORT =="
START_TS=$(date +%s)
N_PASS=0; N_FAIL=0; N_SKIP=0

run_one() {
  rel="$1"
  pat="$(pattern_re "$rel")"
  send="$(pattern_send "$rel")"
  if [ -z "$pat" ]; then
    printf '%s\tSKIP\t0s\t\t%s\n' "$rel" "无可用串口特征串（需人工观察或先用命令触发）" >> "$ROWS"
    echo "[SKIP] $rel"
    N_SKIP=$((N_SKIP+1))
    return
  fi
  t0=$(date +%s)
  per="$(pattern_secs "$rel")"
  secs_use="$SECS"
  [ -n "$per" ] && secs_use="$per"
  set -- --secs "$secs_use" --expect "$pat"
  [ -n "$send" ] && set -- "$@" --send "$send"
  [ "$DOBUILD" = 1 ] || set -- "$@" --no-build
  out="$(HPM_LOCK_WAIT="$LOCK_WAIT" bash "$here/hwrun.sh" "$rel" "$@" 2>&1)"
  rc_hw=$?
  t1=$(date +%s)
  elapsed="$((t1 - t0))s"
  logtail="${out##*---- LOG: }"
  logpath="${logtail%% (rc=*}"
  caprc="${logtail#* (rc=}"
  caprc="${caprc%% ----*}"
  caprc="${caprc%%)*}"
  case "$out" in *"---- LOG:"*) ;; *) logpath=""; caprc="";; esac
  logline="---- LOG: ${logpath} (rc=${caprc}) ----"
  if [ "$rc_hw" -ne 0 ]; then
    result="FAIL"
    key="hwrun 失败(退出码 $rc_hw): $(printf '%s\n' "$out" | tail -1 | cut -c1-130)"
    N_FAIL=$((N_FAIL+1))
  elif [ -z "$logline" ] || [ ! -f "$logpath" ]; then
    result="FAIL"
    key="未找到串口日志: $(printf '%s\n' "$out" | tail -1 | cut -c1-120)"
    N_FAIL=$((N_FAIL+1))
  elif [ "$caprc" = "0" ]; then
    result="PASS"
    N_PASS=$((N_PASS+1))
    key="$(grep -m1 -E "$pat" "$logpath" 2>/dev/null | tr -d '\r' | cut -c1-150)"
    [ -z "$key" ] && key="$(grep -m1 -v '^[[:space:]]*$' "$logpath" | tr -d '\r' | cut -c1-120)"
  else
    result="FAIL"
    N_FAIL=$((N_FAIL+1))
    key="6s 内未匹配 /$pat/；最后一行: $(tail -1 "$logpath" | tr -d '\r' | cut -c1-110)"
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$rel" "$result" "$elapsed" "$logpath" "$key" >> "$ROWS"
  printf '[%-4s] %-42s %4s  %s\n' "$result" "$rel" "$elapsed" "$(printf '%s' "$key" | cut -c1-110)"
}

while read -r rel; do
  [ -n "$rel" ] && run_one "$rel"
done < "$SELF"
rm -f "$SELF"

RESULTS="$ROOT/docs/generated/smoke-results.tsv"
mkdir -p "$(dirname "$RESULTS")"
awk -F"\t" 'NF>=5{k[NR]=$1; r[NR]=$0} END{for(i=NR;i>=1;i--){ if(k[i]!="" && !(k[i] in seen)){seen[k[i]]=1; buf[++n]=r[i]} } for(i=n;i>=1;i--) print buf[i]}' "$ROWS" > "$RESULTS"
N_PASS=$(awk -F"\t" '$2=="PASS"' "$RESULTS" | wc -l)
N_FAIL=$(awk -F"\t" '$2=="FAIL"' "$RESULTS" | wc -l)
N_SKIP=$(awk -F"\t" '$2=="SKIP"' "$RESULTS" | wc -l)
END_TS=$(date +%s)
TOTAL_S=$((END_TS - START_TS))
TOTAL=$((N_PASS + N_FAIL + N_SKIP))
TS_HUMAN="$(date '+%Y-%m-%d %H:%M:%S')"
MIN=$((TOTAL_S / 60)); SEC=$((TOTAL_S % 60))
{
  echo "# 全量上板冒烟回归报告（smoke-report）"
  echo ""
  echo "| 项 | 值 |"
  echo "|---|---|"
  echo "| 生成时间 | $TS_HUMAN |"
  echo "| 板卡 | hpm5e31_1（HPM5E31IPB1） |"
  echo "| 串口 | $SERIAL_PORT @ $SERIAL_BAUD 8N1 |"
  echo "| 抓取窗口 | 首轮统一 ${SECS}s 快筛；FAIL 项按特征串表第 4 列加长重跑后合并 |"
  echo "| 板锁等待上限 | ${LOCK_WAIT}s（hwrun.sh 内部 flock） |"
  echo "| 特征串表 | tools/smoke_patterns.txt |"
  echo "| 总计 | $TOTAL 例：PASS $N_PASS / FAIL $N_FAIL / SKIP $N_SKIP |"
  echo "| 本轮耗时 | $MIN 分 $SEC 秒（全量+merge 多轮时以各轮日志为准） |"
  echo ""
  echo "**判定方法**：每个例程烧写后抓串口若干秒（默认 6 s，可按例程在特征串表第 4 列覆盖为 15~120 s），用特征正则匹配（hwrun.sh --expect），匹配到即 PASS。"
  echo ""
  echo "**重要**：表中 FAIL 表示在该例抓取窗口内没有匹配到特征串，不代表例程本身失败（常见原因：需要先发命令触发、或输出周期长于窗口）。"
  echo "首轮 6 s 快速筛查后，对 FAIL 项已用 scripts/smoke_all.sh --merge 加长窗口/补发送命令重跑，以合并后的本表结果为准。"
  echo ""
  echo "## 结果明细"
  echo ""
  echo "| 例程 | 结果 | 耗时 | 日志 | 关键输出 |"
  echo "|---|---|---|---|---|"
  while IFS="$(printf '\t')" read -r rel result elapsed logpath key; do
    case "$result" in
      PASS) mark="PASS";;
      FAIL) mark="FAIL";;
      *)    mark="SKIP";;
    esac
    if [ -n "$logpath" ] && [ -f "$logpath" ]; then
      logcell="$(printf '%s' "$logpath" | sed "s#$ROOT/##")"
    else
      logcell="-"
    fi
    key="$(printf '%s' "$key" | tr '|' '/' | tr -d '\r')"
    printf '| %s | %s | %s | %s | %s |\n' "$rel" "$mark" "$elapsed" "$logcell" "$key"
  done < "$RESULTS"
  echo ""
  echo "## 真失败项（重跑后仍 FAIL）"
  echo ""
  if [ "$N_FAIL" = 0 ]; then echo "无。全部 $TOTAL 例均 PASS（SKIP 项说明见下）。"; else echo "共 $N_FAIL 例仍失败，逐条原因见下方“失败与跳过说明”。"; fi
  echo ""
  echo "## 失败与跳过说明"
  echo ""
  awk -F'\t' '$2=="FAIL"{print "- FAIL "$1"："$5}' "$RESULTS"
  awk -F'\t' '$2=="SKIP"{print "- SKIP "$1"："$5}' "$RESULTS"
  echo ""
  echo "## 复现方法"
  echo ""
  echo "  cd $ROOT"
  echo "  bash scripts/smoke_all.sh"
  echo "  bash scripts/smoke_all.sh --only 10"
  echo "  bash scripts/smoke_all.sh --only gpio/gpio_speed,spi/spi_dma_bw"
  echo ""
  echo "新增例程时：在 tools/smoke_patterns.txt 追加一行（例程路径 TAB 特征正则 TAB 可选发送命令），再跑 bash scripts/smoke_all.sh --only 名字 验证。"
} > "$REPORT"
echo ""
echo "== smoke_all 完成: PASS $N_PASS / FAIL $N_FAIL / SKIP $N_SKIP，用时 $MIN 分 $SEC 秒 =="
echo "== 报告: $REPORT =="
[ "$N_FAIL" -eq 0 ]
