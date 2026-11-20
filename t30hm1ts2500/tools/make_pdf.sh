#!/usr/bin/env bash
# =============================================================================
# make_pdf.sh -- 把 s2500 仓库的所有文档合成一本中文 PDF 手册
#
#   输出: ${OUT_DIR}/t30hm1ts2500_manual.pdf   （默认 $REPO/.pdf_out/）
#   注意: PDF **不提交到 git**，只作为交付/打印用的汇总材料。
#
# 用法:
#   ./tools/make_pdf.sh                 # 生成 + 自检
#   ./tools/make_pdf.sh --no-check      # 只生成，跳过 PDF 自检
#   ./tools/make_pdf.sh --no-images     # 不重新生成配图
#   ./tools/make_pdf.sh --keep-build    # 保留中间文件（调试用）
#   OUT=/tmp/x.pdf ./tools/make_pdf.sh  # 指定输出文件
#   FONTSET=fandol ./tools/make_pdf.sh  # 换 ctex 字体集 (ubuntu/fandol/adobe)
#
# 依赖 (Ubuntu 22.04/24.04):
#   pandoc texlive-xetex texlive-lang-chinese fonts-noto-cjk
#   poppler-utils graphviz python3-matplotlib
# =============================================================================
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${OUT_DIR:-$REPO/.pdf_out}"
OUT="${OUT:-$OUT_DIR/t30hm1ts2500_manual.pdf}"
if [ -n "${VERSION_TAG:-}" ]; then
  :   # 调用方显式指定（VERSION_TAG=xxx ./tools/make_pdf.sh）
else
  VERSION_TAG="$(cd "$REPO" && git rev-parse HEAD 2>/dev/null || echo dev)"
  # 只有工作区确实有未提交改动时才标 -dirty（否则交付版会一直带着脏标记）
  if [ -n "$(cd "$REPO" && git status --porcelain 2>/dev/null | head -1)" ]; then
    VERSION_TAG="${VERSION_TAG}-dirty"
  fi
fi
DATE_STR="${DATE_STR:-$(date +%Y-%m-%d)}"
FONTSET="${FONTSET:-ubuntu}"
DO_CHECK=1
DO_IMAGES=1
KEEP_BUILD=0

for a in "$@"; do
  case "$a" in
    --no-check)  DO_CHECK=0 ;;
    --no-images) DO_IMAGES=0 ;;
    --keep-build) KEEP_BUILD=1 ;;
    -h|--help)   sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "未知参数: $a" >&2; exit 2 ;;
  esac
done

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[WARN]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[FAIL]\033[0m %s\n' "$*" >&2; exit 1; }

# ----------------------------------------------------------------- 1. 依赖
say "检查依赖"
missing=()
for c in pandoc xelatex pdftoppm pdfinfo pdftotext pdffonts python3; do
  command -v "$c" >/dev/null 2>&1 || missing+=("$c")
done
if [ "${#missing[@]}" -gt 0 ]; then
  die "缺少命令: ${missing[*]}
  sudo apt install -y pandoc texlive-xetex texlive-lang-chinese fonts-noto-cjk poppler-utils"
fi
kpsewhich ctex.sty      >/dev/null 2>&1 || die "缺少 ctex.sty，请装 texlive-lang-chinese"
kpsewhich framed.sty    >/dev/null 2>&1 || warn "缺少 framed.sty，代码块将没有底纹（装 texlive-latex-recommended）"
pandoc  --version | head -1
xelatex --version | head -1
if ! fc-list :lang=zh 2>/dev/null | grep -qi 'noto\|fandol\|arphic\|source han'; then
  warn "系统里没有中文字体，PDF 可能显示方块；建议 sudo apt install fonts-noto-cjk"
fi

mkdir -p "$OUT_DIR"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/s2500_pdf.XXXXXX")"
cleanup() {
  if [ "$KEEP_BUILD" = 1 ]; then
    echo "中间文件保留在: $BUILD"
  else
    rm -rf "$BUILD"
  fi
}
trap cleanup EXIT

cd "$REPO"

# ----------------------------------------------------------------- 2. 配图
IMGDIR="$REPO/docs/images"
if [ "$DO_IMAGES" = 1 ]; then
  if compgen -G "$IMGDIR/*.png" >/dev/null; then
    say "配图已存在 ($(ls "$IMGDIR"/*.png | wc -l) 张)，跳过生成"
  else
    say "生成配图 (matplotlib + graphviz)"
    python3 "$IMGDIR/gen_images.py" || warn "配图生成失败，PDF 里会缺图"
  fi
fi
say "可用配图: $(ls "$IMGDIR"/*.png 2>/dev/null | wc -l) 张"

# ----------------------------------------------------------------- 3. 章节顺序
# 阅读顺序: 总览 -> 入门 -> 环境 -> 硬件/协议 -> 移植 -> 测试/性能
#          -> 现场 -> 交付 -> 各平台 -> 实测记录 -> 交接
CHAPTERS=(
  "README.md"
  "docs/13_10baset1s_primer.md"
  "docs/12_dev_env_setup.md"
  "docs/01_hardware.md"
  "docs/02_bus_plan.md"
  "docs/03_spi_protocol.md"
  "docs/04_register_map.md"
  "docs/00_porting_guide.md"
  "docs/05_lan8651_vs_s2500.md"
  "docs/08_shell_commands.md"
  "docs/06_test_matrix.md"
  "docs/07_performance.md"
  "docs/11_test_examples.md"
  "docs/09_debug_env.md"
  "docs/10_hardware_troubleshooting.md"
  "docs/14_delivery_checklist.md"
  "stm32h723/README.md"
  "hpm5361/README.md"
  "s32k312/README.md"
  "tc387/README.md"
  "rk3506/README.md"
)
while IFS= read -r f; do CHAPTERS+=("$f"); done < <(ls -1 docs/results/*.md 2>/dev/null | sort)
# (release 版不含 handover)

say "装配正文 (${#CHAPTERS[@]} 个文件)"
BODY="$BUILD/body.md"
: > "$BODY"

# LaTeX 里没有的字符在 PDF 里换成等价符号（Markdown 源文件保持 emoji 不变）
SYMBOL_SED=(
  -e 's/✅/$\\checkmark$/g'
  -e 's/❌/$\\times$/g'
  -e 's/⚠️/$\\triangle$/g'
  -e 's/⚠/$\\triangle$/g'
  -e 's/⬜/$\\square$/g'
  -e 's/⛔/■/g'
  -e 's/✔/$\\checkmark$/g'
  -e 's/☐/$\\square$/g'
  -e 's/▶/>/g' -e 's/◀/</g' -e 's/▸/>/g'
  -e 's/\xef\xb8\x8f//g'
)

for f in "${CHAPTERS[@]}"; do
  if [ ! -f "$f" ]; then
    warn "跳过缺失文件: $f"
    continue
  fi
  {
    echo ""
    echo "<!-- ===== $f ===== -->"
    echo ""
    sed -E \
      -e 's#\]\(\.\./\.\./docs/images/#](docs/images/#g' \
      -e 's#\]\(\.\./\.\./images/#](docs/images/#g' \
      -e 's#\]\(\.\./images/#](docs/images/#g' \
      -e 's#\]\(\./images/#](docs/images/#g' \
      -e 's#\]\(images/#](docs/images/#g' \
      -e 's#src="images/#src="docs/images/#g' \
      -e '/^\[!\[/d' \
      -e '/img\.shields\.io/d' \
      "${SYMBOL_SED[@]}" \
      "$f"
    echo ""
  } >> "$BODY"
done

grep -q '^# ' "$BODY" || die "正文里没有任何一级标题，装配失败"

# ------------------------------------------- 3b. 预检：正文里的裸反斜杠
# Markdown 里写成 \r \n 而不加反引号时，LaTeX 会报 "Undefined control sequence"，
# 错误信息完全看不出是哪篇文档。这里提前扫一遍并指出文件与行号。
say "预检: 正文裸反斜杠（\r / \n 之类必须写在反引号里）"
python3 - "$BODY" <<'PY' || true
import re, sys

BT = chr(96)
path = sys.argv[1]
cur = "?"
infence = False
bad = 0
for i, line in enumerate(open(path, encoding="utf-8"), 1):
    st = line.strip()
    if st.startswith("<!-- ===== "):
        cur = st.replace("<!-- ===== ", "").replace(" ===== -->", "")
        continue
    if st.startswith(BT * 3):
        infence = not infence
        continue
    if infence:
        continue
    stripped = re.sub(BT + "[^" + BT + "]*" + BT, "", line)
    for m in re.finditer(r"\\[a-zA-Z]", stripped):
        bad += 1
        if bad <= 20:
            print("  [裸反斜杠] %s:%d  %s" % (cur, i, stripped.strip()[:90]))
if bad:
    print("  !! 共 %d 处，请给这些片段加反引号，否则 PDF 生成会失败" % bad)
else:
    print("  未发现")
PY

# ----------------------------------------------------------------- 4. LaTeX 头
HEADER="$BUILD/header.tex"
cat > "$HEADER" <<'LATEX'
% ---- 由 tools/make_pdf.sh 生成 ----
\usepackage{etoolbox}
\usepackage{fancyhdr}
\usepackage{longtable,booktabs,array}
\usepackage{xcolor}
\usepackage{framed}
\usepackage{amssymb}

% 让箭头 / 数学运算符 / 带圈数字 / 几何图形 / 杂项符号 都走 CJK 字体
% （Latin Modern 里没有 ≤ ≥ ≈ ↔ 等字形，不声明就会出现空白缺字）
\xeCJKDeclareCharClass{CJK}{%
  "2190 -> "21FF, "2200 -> "22FF, "2300 -> "23FF,
  "2460 -> "24FF, "25A0 -> "25FF, "2600 -> "26BF, "2B00 -> "2BFF}

% 页面
\setlength{\parindent}{0pt}
\setlength{\parskip}{0.35em}
\sloppy

% 不启用 LaTeX 自动编号（正文已有人工编号，避免出现 "1.1 1. 概述"）
\setcounter{secnumdepth}{0}
\setcounter{tocdepth}{2}

% 页眉页脚：左=手册名，右=页码；章首页也显示
\pagestyle{fancy}
\fancyhf{}
\fancyhead[L]{\small\color{gray} s2500 10BASE-T1S 技术手册}
\fancyhead[R]{\small\color{gray} 第 \thepage\ 页}
\renewcommand{\headrulewidth}{0.4pt}
\renewcommand{\footrulewidth}{0pt}
\fancypagestyle{plain}{%
  \fancyhf{}%
  \fancyhead[L]{\small\color{gray} s2500 10BASE-T1S 技术手册}%
  \fancyhead[R]{\small\color{gray} 第 \thepage\ 页}%
  \renewcommand{\headrulewidth}{0.4pt}%
}

% 表格
\AtBeginEnvironment{longtable}{\footnotesize}
\renewcommand{\arraystretch}{1.15}
\setlength{\LTpre}{0.6em}
\setlength{\LTpost}{0.6em}

% 代码块：把 pandoc 的 Shaded 环境换成带底纹和边框的 framed 盒子
% （用 AtBeginDocument 保证覆盖 pandoc 自带的 \newenvironment{Shaded}）
\definecolor{shadecolor}{RGB}{247,247,249}
\setlength{\FrameRule}{0.4pt}
\setlength{\FrameSep}{4pt}
\AtBeginDocument{%
  \renewenvironment{Shaded}{\begin{shaded}}{\end{shaded}}%
}

% >>>SCFONT  显式指定简体中文字形（ctex 的 ubuntu 字体集默认可能落到 CJK JP 字形）
\setCJKmainfont{Noto Serif CJK SC}
\setCJKsansfont{Noto Sans CJK SC}
\setCJKmonofont{Noto Sans Mono CJK SC}
% <<<SCFONT

\AtBeginDocument{\hypersetup{colorlinks=true,linkcolor=blue!55!black,
  urlcolor=blue!55!black,pdftitle={s2500 10BASE-T1S 技术手册},
  pdfauthor={s2500 project}}}
LATEX

# ----------------------------------------------------------------- 5. 封面
COVER_IN="$BUILD/cover.tex.in"
COVER="$BUILD/cover.tex"
cat > "$COVER_IN" <<'LATEX'
% ---- 封面 ----
\begin{titlepage}
\thispagestyle{empty}
\centering
\vspace*{1.0cm}

{\color{gray}\large onsemi T30HM1TS2500 (S2500)}\\[0.5cm]
{\Huge\bfseries 10BASE-T1S 多平台适配}\\[0.25cm]
{\Huge\bfseries 技 术 手 册}\\[0.8cm]

{\large STM32H723 \quad HPM5361 \quad S32K312 \quad TC387 \quad RK3506}\\[0.3cm]
{\large IEEE 802.3cg \quad OPEN Alliance OA-SPI \quad PLCA}\\[0.8cm]

\includegraphics[width=0.92\textwidth,keepaspectratio]{docs/images/topology.png}

\vfill
{\small 版本 \texttt{@VERSION@} \quad 生成日期 @DATE@}\\[0.3cm]
{\small 本文档由 \texttt{tools/make\_pdf.sh} 从仓库 Markdown 自动生成，请勿手工编辑}
\end{titlepage}

% ---- 关于本手册 ----
\thispagestyle{empty}
\vspace*{0.3cm}
{\Large\bfseries 关于本手册}\\[0.4cm]

本手册由仓库中全部 Markdown 文档按阅读顺序汇编而成，覆盖：项目简介、10BASE-T1S / PLCA 入门、
开发环境搭建、硬件与总线、SPI 协议与寄存器、移植指南、命令手册、测试矩阵、性能分析、
现场排障、交付清单、5 个平台的工程说明、实测记录与会话交接。

\vspace{0.3cm}
{\bfseries 建议阅读顺序}\\[0.15cm]
\begin{enumerate}\setlength{\itemsep}{2pt}
  \item 第 1 章（总览）→ 第 2 章（入门）→ 第 3 章（开发环境）：建立全局认识
  \item 第 4--9 章：硬件 / 总线 / SPI 协议 / 寄存器 / 移植
  \item 第 10--15 章：命令、测试矩阵、性能、现场排障
  \item 第 16 章：交付与验收清单（项目经理与客户）
  \item 第 17 章起：各平台工程说明（STM32H723 / HPM / S32K / TC387 / RK3506）
  \item 附录：实测记录归档与会话交接说明
\end{enumerate}

\vspace{0.3cm}
{\bfseries 约定}\\[0.15cm]
\begin{itemize}\setlength{\itemsep}{2pt}
  \item 主机 = Ubuntu（\texttt{192.168.10.10}，USB-10T1S，PLCA 协调者 ID 0）
  \item 板子 = \texttt{.11} STM32H723 / \texttt{.12} HPM / \texttt{.13} S32K / \texttt{.14} TC387 / \texttt{.15} RK3506
  \item 表格符号：$\checkmark$ 通过 \quad $\triangle$ 部分受限 \quad $\times$ 失败 \quad $\square$ 未测
  \item 所有实测数字标注来源文档；未测项目一律标注"未测"，不做推测
\end{itemize}

\vfill
{\footnotesize 代码许可 MIT；\texttt{common/lwip}、\texttt{common/letter-shell} 保留各自原始许可；\\
数据手册 PDF 版权归 onsemi，仅作内部参考。}
\clearpage
LATEX
sed -e "s|@VERSION@|$VERSION_TAG|g" -e "s|@DATE@|$DATE_STR|g" "$COVER_IN" > "$COVER"

# ----------------------------------------------------------------- 6. 生成 PDF
# 说明：刻意不用 --listings。listings 会把行内代码变成 \lstinline!...!，
#       一旦代码里出现反斜杠（如 \r \n）就会炸；pandoc 默认的 \texttt
#       与高亮写法会自动转义，更稳。
COMMON_OPTS=(
  --from "markdown+pipe_tables+raw_html+task_lists+footnotes+definition_lists+header_attributes+tex_math_dollars+strikeout"
  --to pdf
  --pdf-engine xelatex
  --pdf-engine-opt "-interaction=nonstopmode"
  --toc
  --toc-depth 2
  --columns 100
  --highlight-style pygments
  --resource-path ".:$REPO"
  --include-before-body "$COVER"
  -V documentclass=ctexbook
  -V classoption=openany
  -V fontsize=10pt
  -V geometry:a4paper
  -V geometry:margin=2.2cm
  -V geometry:includeheadfoot
  -V monofont="DejaVu Sans Mono"
  -V monofontoptions="Scale=0.88"
  -V toc-title="目　录"
)

# 备用头：去掉显式 SC 字体设置（万一系统里没有该字体也能出 PDF）
HEADER_PLAIN="$BUILD/header-plain.tex"
sed '/>>>SCFONT/,/<<<SCFONT/d' "$HEADER" > "$HEADER_PLAIN"

try_build() {   # $1=fontset  $2=header  $3=output  $4=log
  pandoc "$BODY" "${COMMON_OPTS[@]}" --include-in-header "$2" \
    -V classoption=fontset="$1" -o "$3" 2>"$4"
}

say "生成 PDF (pandoc + xelatex, fontset=$FONTSET) ... 通常需要 1-3 分钟"
mkdir -p "$(dirname "$OUT")"
ok=0
if try_build "$FONTSET" "$HEADER" "$OUT" "$BUILD/err1.log"; then
  ok=1
else
  warn "第 1 次尝试失败（fontset=$FONTSET + SC 字体），去掉显式字体再试"
  grep -E '^(!|l\.[0-9]+|Error)' "$BUILD/err1.log" | head -12 >&2 || true
  if try_build "$FONTSET" "$HEADER_PLAIN" "$OUT" "$BUILD/err2.log"; then
    ok=1
  else
    warn "第 2 次尝试失败，改用 fontset=fandol 再试"
    grep -E '^(!|l\.[0-9]+|Error)' "$BUILD/err2.log" | head -12 >&2 || true
    if try_build fandol "$HEADER_PLAIN" "$OUT" "$BUILD/err3.log"; then
      ok=1
    else
      grep -E '^(!|l\.[0-9]+|Error)' "$BUILD/err3.log" | head -25 >&2 || true
      die "PDF 生成失败（加 --keep-build 可保留中间文件排查）"
    fi
  fi
fi
[ -s "$OUT" ] || die "PDF 未生成: $OUT"

SIZE="$(du -h "$OUT" | cut -f1)"
PAGES="$(pdfinfo "$OUT" 2>/dev/null | awk '/^Pages:/{print $2}')"
say "生成成功: $OUT  ($SIZE, ${PAGES:-?} 页)"

# ----------------------------------------------------------------- 7. 自检
if [ "$DO_CHECK" = 0 ]; then
  say "--no-check，跳过自检"
  exit 0
fi

say "自检 1/5: 字体嵌入"
FONTS="$(pdffonts "$OUT" 2>/dev/null || true)"
echo "$FONTS" | head -20
if echo "$FONTS" | grep -qi 'noto\|fandol\|arphic\|source han\|cjk'; then
  say "  CJK 字体已嵌入"
else
  warn "没有看到 CJK 字体嵌入，中文可能显示为方块"
fi

say "自检 2/5: 中文可提取性"
TXT="$BUILD/all.txt"
pdftotext -layout "$OUT" "$TXT" 2>/dev/null || true
if [ -s "$TXT" ]; then
  REPL="$(grep -c $'\xef\xbf\xbd' "$TXT" || true)"
  CJK="$(grep -o '[一-龥]' "$TXT" | wc -l)"
  say "  中文字符: $CJK 个；乱码替换符 U+FFFD: ${REPL:-0} 个"
  [ "${CJK:-0}" -gt 10000 ] || warn "中文字符偏少 (${CJK:-0})，可能有大段乱码"
  [ "${REPL:-0}" -eq 0 ] || warn "存在 ${REPL} 个乱码替换符，请检查对应页面"
else
  warn "pdftotext 提取为空"
fi

say "自检 3/5: 目录与页码"
TOC_TXT="$BUILD/toc.txt"
pdftotext -layout -f 2 -l 8 "$OUT" "$TOC_TXT" 2>/dev/null || true
if grep -q '目' "$TOC_TXT"; then
  DOTLEAD="$(grep -cE '(\. ){3,}' "$TOC_TXT" || true)"
  say "  目录页存在；带页码的条目: ${DOTLEAD:-0} 条"
  [ "${DOTLEAD:-0}" -gt 30 ] || warn "目录条目偏少 (${DOTLEAD:-0})"
  grep -qE '\?\?\?' "$TOC_TXT" && warn "目录里出现 ???（交叉引用未解析）" || true
else
  warn "没有在前几页找到目录"
fi

say "自检 4/5: 章节与图片"
say "  正文一级标题(章): $(grep -c '^# ' "$BODY" || true)"
say "  正文图片引用: $(grep -cE '!\[[^]]*\]\(docs/images/' "$BODY" || true)"

say "自检 5/5: 渲染首页 / 目录 / 中间 / 末页 PNG 供人工抽查"
PREV="$OUT_DIR/pdf_preview"
mkdir -p "$PREV"
rm -f "$PREV"/*.png 2>/dev/null || true
pdftoppm -png -r 90 -f 1 -l 2 "$OUT" "$PREV/page" 2>/dev/null || true
if [ -n "${PAGES:-}" ] && [ "$PAGES" -gt 3 ]; then
  MID=$(( PAGES / 2 ))
  pdftoppm -png -r 90 -f "$MID" -l "$MID" "$OUT" "$PREV/mid" 2>/dev/null || true
  pdftoppm -png -r 90 -f "$PAGES" -l "$PAGES" "$OUT" "$PREV/last" 2>/dev/null || true
fi
ls -1 "$PREV"/*.png 2>/dev/null | sed 's/^/  /' || true

echo
say "完成"
echo "  PDF     : $OUT"
echo "  预览图  : $PREV/"
echo "  页数    : ${PAGES:-?}"
echo "  提示    : PDF 不提交 git（输出在 $OUT_DIR）"
