#!/usr/bin/env bash
# 交付验收一键检查：编译全量 -> 文档校验 -> 引用校验 -> 生成索引 -> PDF 合成 -> 汇总
# 用法: bash tools/final_check.sh [--with-smoke]
# 说明: 第 7 步为 MCAL 静态合规检查（tools/mcal_check.py，R6 新增）
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$here/.." && pwd)"
cd "$REPO"
source scripts/env.sh
WITH_SMOKE=0
[ "${1:-}" = "--with-smoke" ] && WITH_SMOKE=1
fail=0
step() { echo; echo "======== $* ========"; }

step "1/10 全量编译"
if bash tools/build_all.sh >/tmp/fc_build.log 2>&1; then
  echo "PASS: $(grep -a 'build_all:' /tmp/fc_build.log | tail -1)"
else
  echo "FAIL: 见 /tmp/fc_build.log"; tail -20 /tmp/fc_build.log; fail=1
fi

step "2/10 写作规范校验"
if python3 tools/check_docs.py --toc >/tmp/fc_docs.log 2>&1; then
  echo "PASS: $(tail -1 /tmp/fc_docs.log)"
else
  echo "FAIL:"; grep -a ERROR /tmp/fc_docs.log | head -10; fail=1
fi

step "3/10 引用校验（日志/图/章号）"
if python3 tools/check_refs.py >/tmp/fc_refs.log 2>&1; then
  echo "PASS: $(tail -1 /tmp/fc_refs.log)"
else
  echo "FAIL:"; tail -10 /tmp/fc_refs.log; fail=1
fi

step "4/10 生成索引与自动表"
python3 tools/gen_soc_tables.py
python3 tools/gen_toc.py
python3 tools/gen_status.py
python3 tools/gen_appendix_tables.py --write

step "5/10 去 AI 味提示（不阻断）"
if python3 tools/polish_check.py --top 5 >/tmp/fc_polish.log 2>&1; then
  echo "提示: $(head -1 /tmp/fc_polish.log)"
else
  echo "WARN: polish_check 运行失败（不阻断）"
fi

step "6/10 图资产检查（孤儿图/缺图）"
if python3 tools/check_figs.py >/tmp/fc_figs.log 2>&1; then
  echo "PASS: $(head -1 /tmp/fc_figs.log)"
else
  echo "FAIL:"; head -12 /tmp/fc_figs.log; fail=1
fi

step "7/10 MCAL 静态合规检查（R6）"
if python3 tools/mcal_check.py >/tmp/fc_mcal.log 2>&1; then
  echo "PASS: $(tail -1 /tmp/fc_mcal.log)"
else
  echo "FAIL:"; grep -a ERROR /tmp/fc_mcal.log | head -12; fail=1
fi

step "8/10 冒烟模式表覆盖（新增例程是否登记）"
if python3 tools/check_smoke_coverage.py >/tmp/fc_smoke_cov.log 2>&1; then
  echo "PASS: $(grep -a '结果：' /tmp/fc_smoke_cov.log | tail -1)"
else
  echo "WARN: 模式表有陈旧/格式问题（不阻断，见 /tmp/fc_smoke_cov.log）"
  grep -a 'ERROR' /tmp/fc_smoke_cov.log | head -5
fi

step "9/10 合成 PDF"
if bash tools/build_pdf.sh >/tmp/fc_pdf.log 2>&1; then
  echo "PASS: $(grep -aE 'pages|PDF:' /tmp/fc_pdf.log | tail -2)"
else
  echo "FAIL: 见 /tmp/fc_pdf.log"; tail -15 /tmp/fc_pdf.log; fail=1
fi

if [ "$WITH_SMOKE" = 1 ]; then
  step "10/10 全量上板冒烟"
  if bash scripts/smoke_all.sh >/tmp/fc_smoke.log 2>&1; then
    echo "PASS: $(grep -a '总计' docs/generated/smoke-report.md | head -1)"
  else
    echo "FAIL: 见 docs/generated/smoke-report.md"; fail=1
  fi
else
  step "10/10 冒烟（跳过，加 --with-smoke 可执行）"
fi

echo
if [ "$fail" = 0 ]; then
  echo "===== 验收通过 ====="
else
  echo "===== 验收未通过，见上面 FAIL 项 ====="
fi
exit $fail
