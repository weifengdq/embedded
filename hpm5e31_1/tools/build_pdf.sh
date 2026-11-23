#!/usr/bin/env bash
# =============================================================================
# tools/build_pdf.sh —— 《HPM5E31 实战手册》PDF 合成脚本
#
# 功能：读取 docs/00-目录.md 的章节顺序，把 docs/chapters/*.md 按顺序拼成
#       单本 PDF：封面 + 版权页 + 自动目录(+可选图/表目录) + 各章（页眉页脚带
#       版本与页码）。版本号来自 docs/VERSION。
#
# 用法：
#   tools/build_pdf.sh                      # 合成全部已存在章节
#   tools/build_pdf.sh --version 0.2.0      # 覆盖版本号（不写 docs/VERSION）
#   tools/build_pdf.sh --only 04,05,26      # 只合成指定章（调试用）
#   tools/build_pdf.sh --no-cover --no-toc  # 无封面/无目录（快速自检）
#   tools/build_pdf.sh --lof                # 追加图目录/表目录
#   tools/build_pdf.sh --strict             # 目录里有缺失章节直接失败
#   tools/build_pdf.sh --keep-temp          # 保留 docs/build/.tmp
#   tools/build_pdf.sh -h | --help
#
# 输出：docs/build/HPM5E31-Practical-Guide-v<版本>.pdf
# 日志：tools/build_pdf.log（pandoc/XeLaTeX 输出 + 页数/大小摘要）
# 依赖：pandoc >= 3、texlive-xetex、texlive-lang-chinese、fonts-noto-cjk、
#       poppler-utils(pdfunite/pdfinfo)、python3
# 退出码：0 成功；1 参数/环境错误；2 合成失败
# =============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOCS="$ROOT/docs"
CHAPTERS="$DOCS/chapters"
TOCFILE="$DOCS/00-目录.md"
VERFILE="$DOCS/VERSION"
BUILD="$DOCS/build"
WORK="$BUILD/.tmp"
LOG="$ROOT/tools/build_pdf.log"

TITLE="HPM5E31 实战手册"
SUBTITLE="原理 · 例程 · 上板实测 · 避坑"
AUTHOR="hpm5e31_1 项目组"
VERSION=""
OUT=""
ONLY=""
COVER=1
TOC=1
LOF=1
STRICT=0
KEEP=0
QUIET=0
BUILD_DATE="$(date +%Y-%m-%d)"

die() { echo "ERROR: $*" >&2; exit 1; }
info() { [ "$QUIET" = 1 ] || echo "$*"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2;;
    --out) OUT="${2:-}"; shift 2;;
    --only) ONLY="${2:-}"; shift 2;;
    --date) BUILD_DATE="${2:-}"; shift 2;;
    --no-cover) COVER=0; shift;;
    --no-toc) TOC=0; shift;;
    --lof) LOF=1; shift;;
    --strict) STRICT=1; shift;;
    --keep-temp) KEEP=1; shift;;
    --quiet) QUIET=1; shift;;
    -h|--help) sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
    *) die "未知参数: $1（--help 查看用法）";;
  esac
done

# ---------- 依赖检查 ----------
for c in pandoc xelatex pdfunite pdfinfo python3; do
  command -v "$c" >/dev/null 2>&1 || die "缺少命令 $c（apt-get install pandoc texlive-xetex texlive-lang-chinese poppler-utils）"
done
[ -f "$TOCFILE" ] || die "找不到目录文件: $TOCFILE"
[ -d "$CHAPTERS" ] || die "找不到章节目录: $CHAPTERS"
[ -f "$ROOT/tools/latex/preamble.tex" ] || die "找不到导言区模板: tools/latex/preamble.tex"
python3 -c "import fitz" 2>/dev/null || die "缺少 PyMuPDF（python3 -c 'import fitz' 失败；pip install pymupdf）"
[ -f "$ROOT/tools/pdf_toc.py" ] || die "找不到 tools/pdf_toc.py"
[ -f "$ROOT/tools/pdf_outline.py" ] || die "找不到 tools/pdf_outline.py"

# ---------- 版本 ----------
if [ -z "$VERSION" ]; then
  [ -f "$VERFILE" ] || die "找不到 docs/VERSION，且未用 --version 指定"
  VERSION="$(tr -d ' \t\r\n' < "$VERFILE")"
fi
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "版本号格式应为 X.Y.Z，当前: $VERSION"

[ -n "$OUT" ] || OUT="$BUILD/HPM5E31-Practical-Guide-v$VERSION.pdf"
mkdir -p "$BUILD"
rm -rf "$WORK"
mkdir -p "$WORK"

# ---------- 记录日志 ----------
exec 3>>"$LOG"
{
  echo "================================================================"
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] build_pdf.sh  version=$VERSION  date=$BUILD_DATE"
  echo "  out=$OUT"
} >&3

# ---------- 解析 00-目录.md 的章节顺序 ----------
python3 - "$TOCFILE" <<'PY' > "$WORK/order.tsv" || die "解析 docs/00-目录.md 失败"
import re, sys
rows = []
for line in open(sys.argv[1], encoding="utf-8"):
    m = re.match(r"^\|\s*(\d{2})\s*\|\s*([^|]+?)\s*\|\s*([0-9A-Za-z_.\-]+\.md)\s*\|", line)
    if m:
        rows.append((m.group(1), m.group(2).strip().replace("|", "/"), m.group(3).strip()))
for nn, title, fname in rows:
    print("%s\t%s\t%s" % (nn, fname, title))
PY

INCLUDED=()
MISSING=()
while IFS=$'\t' read -r NN FNAME CTITLE; do
  [ -n "$NN" ] || continue
  if [ -n "$ONLY" ] && ! echo ",$ONLY," | grep -q ",$NN,"; then continue; fi
  if [ -f "$CHAPTERS/$FNAME" ]; then
    INCLUDED+=("$CHAPTERS/$FNAME")
  else
    MISSING+=("$NN $FNAME（$CTITLE）")
  fi
done < "$WORK/order.tsv"

if [ ${#MISSING[@]} -gt 0 ]; then
  info "WARNING: 以下章节尚未提交，本次跳过："
  for m in "${MISSING[@]}"; do info "  - $m"; done
  if [ "$STRICT" = 1 ]; then die "--strict 模式下章节缺失，终止"; fi
fi
[ ${#INCLUDED[@]} -gt 0 ] || die "没有任何可合成的章节（docs/chapters/ 为空？）"

# ---------- 预处理：合并 + HTML 注释 + 本地图片改绝对路径（缺图用占位文本） ----------
python3 - "$DOCS" "$WORK/book.md" "${INCLUDED[@]}" <<'PY' || die "Markdown 预处理失败"
import os, re, sys
docs, out, files = sys.argv[1], sys.argv[2], sys.argv[3:]

url_re = re.compile(r"^(?:https?:|mailto:|ftp:|#)")
img_re = re.compile(r"!\[([^\]]*)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
link_re = re.compile(r"(!?)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
missing = []

def abs_if_exists(p):
    if url_re.match(p) or p.startswith("/"):
        return p, True
    frag = ""
    if "#" in p:
        p, frag = p.split("#", 1)
        frag = "#" + frag
    cand = os.path.normpath(os.path.join(docs, p))
    return (cand + frag, True) if os.path.exists(cand) else (p, False)

def fix_img(m):
    alt, target = m.group(1), m.group(2)
    p, ok = abs_if_exists(target)
    if ok:
        return "![%s](%s)" % (alt, p)
    missing.append(target)
    return "**【缺图：%s】（文件 %s 不存在，等待负责人生成）**" % (alt or "无图题", target)

def fix_link(m):
    bang, target = m.group(1), m.group(2)
    p, ok = abs_if_exists(target)
    return "%s[%s](%s)" % (bang, m.group(0)[m.group(0).index("[")+1:m.group(0).index("]")], p) if False else m.group(0).replace(target, p, 1)

def fence_errors(text):
    errs = []
    in_fence = False
    open_line = 0
    for i, line in enumerate(text.splitlines(), 1):
        m = re.match(r"^\s*(\x60{3,})(.*)$", line)
        if not m:
            continue
        if not in_fence:
            in_fence = True
            open_line = i
        elif m.group(2).strip():
            errs.append((open_line, "代码块未闭合（第 %d 行遇到新的带语言围栏）" % i))
            open_line = i
        else:
            in_fence = False
    if in_fence:
        errs.append((open_line, "代码块未闭合（到文件末尾）"))
    return errs

parts = []
bad = []
for f in files:
    s = open(f, encoding="utf-8").read()
    for ln, msg in fence_errors(s):
        bad.append((f, ln, msg))
    s = re.sub(r"<!--.*?-->", "", s, flags=re.S)
    s = img_re.sub(fix_img, s)
    s = link_re.sub(fix_link, s)
    s = re.sub(r"^(\s*)[-*]\s+\[[ xX]\]\s+", r"\1* ", s, flags=re.M)
    s = s.replace("\r\n", "\n").replace("\r", "\n")
    parts.append(s.rstrip() + "\n")
if bad:
    sys.stderr.write("ERROR: 以下章节的 Markdown 代码围栏未闭合（会导致 LaTeX 死循环，请让 owner 修复）：\n")
    for f, ln, msg in bad:
        sys.stderr.write("  - %s:%d %s\n" % (f, ln, msg))
    sys.exit(3)

open(out, "w", encoding="utf-8").write("\n\n".join(parts))
sys.stderr.write("merged %d chapters, %d bytes\n" % (len(files), os.path.getsize(out)))
if missing:
    sys.stderr.write("WARNING: %d 张图缺失（已用占位文本代替，不影响编译）：\n" % len(missing))
    for m in sorted(set(missing)):
        sys.stderr.write("  - %s\n" % m)
PY

# ---------- 导言区模板渲染 ----------
python3 - "$ROOT/tools/latex/preamble.tex" "$WORK/preamble.tex" "$TITLE" "$VERSION" "$BUILD_DATE" <<'PY' || die "导言区渲染失败"
import os, sys
src, dst, title, ver, date = sys.argv[1:6]
s = open(src, encoding="utf-8").read()
s = s.replace("@TITLE@", title).replace("@VERSION@", "v" + ver).replace("@DATE@", date)
open(dst, "w", encoding="utf-8").write(s)
vp = os.path.join(os.path.dirname(os.path.dirname(src)), "latex", "verbatim.tex")
if os.path.exists(vp):
    s2 = s + chr(10) + open(vp, encoding="utf-8").read()
    open(dst, "w", encoding="utf-8").write(s2)
PY

if [ "$LOF" = 1 ]; then
  cat > "$WORK/lof.tex" <<'TEX'
\clearpage
\listoffigures
\clearpage
\clearpage
TEX
fi

# ---------- 可选：合成前刷新自动生成的表格（第 40 章附录 / SoC 速查） ----------
if [ -f "$ROOT/tools/gen_appendix_tables.py" ]; then
  info "==> 刷新第 40 章附录表（tools/gen_appendix_tables.py --write）..."
  ( cd "$ROOT" && python3 tools/gen_appendix_tables.py --write ) >>"$LOG" 2>&1     || info "WARNING: gen_appendix_tables.py 运行失败，沿用旧表"
fi
if [ -f "$ROOT/tools/gen_soc_tables.py" ]; then
  info "==> 刷新 SoC 速查表（tools/gen_soc_tables.py）..."
  ( cd "$ROOT" && python3 tools/gen_soc_tables.py ) >>"$LOG" 2>&1     || info "WARNING: gen_soc_tables.py 运行失败，沿用旧表"
fi

# ---------- pandoc: Markdown -> LaTeX ----------
PANDOC_ARGS=(
  "$WORK/book.md"
  --from "markdown+pipe_tables+fenced_code_blocks+tex_math_dollars+implicit_figures+raw_tex+strikeout+footnotes+task_lists"
  --to latex --standalone --output "$WORK/body.tex"
  --highlight-style=tango
  --include-in-header "$WORK/preamble.tex"
  --variable documentclass=book
  --variable classoption=oneside
  --variable classoption=openany
  --variable fontsize=11pt
  --variable geometry=a4paper
  --variable geometry:margin=2.4cm
  --variable colorlinks=true
  --variable linkcolor=RoyalBlue
  --variable urlcolor=RoyalBlue
)
[ "$TOC" = 1 ] && PANDOC_ARGS+=(--toc --toc-depth=2)
[ "$LOF" = 1 ] && PANDOC_ARGS+=(--include-before-body "$WORK/lof.tex")

info "==> pandoc 生成 LaTeX ..."
if ! pandoc "${PANDOC_ARGS[@]}" >>"$LOG" 2>&1; then
  tail -30 "$LOG"; die "pandoc 失败（详见 $LOG）"
fi

# ---------- 长路径/长标识符断行后处理（避免表格右侧被裁切） ----------
python3 "$ROOT/tools/latex_breaks.py" "$WORK/body.tex" >>"$LOG" 2>&1 \
  || info "WARNING: latex_breaks.py 失败（继续编译）"

# ---------- XeLaTeX 编译正文（2 遍解析目录/交叉引用） ----------
info "==> XeLaTeX 编译正文（2 遍）..."
cd "$WORK" || die "无法进入 $WORK"
for pass in 1 2; do
  if ! xelatex -interaction=nonstopmode -halt-on-error -file-line-error body.tex >>"$LOG" 2>&1; then
    echo "---- XeLaTeX 第 $pass 遍失败，错误摘录 ----" >&2
    grep -nE "^!|:[0-9]+:" body.log 2>/dev/null | head -20 >&2 || tail -40 body.log >&2
    die "XeLaTeX 编译失败（完整日志见 $LOG）"
  fi
done
[ -f body.pdf ] || die "XeLaTeX 未生成 body.pdf"
cd "$ROOT" || die "无法回到仓库根目录"

# ---------- 封面 + 版权页 ----------
FRONT_PDF=""
if [ "$COVER" = 1 ]; then
  info "==> 生成封面与版权页 ..."
  ART="$DOCS/figures/_common/cover-board.png"
  TITLE_TEX="${TITLE//_/\\_}"
  SUBTITLE_TEX="${SUBTITLE//_/\\_}"
  AUTHOR_TEX="${AUTHOR//_/\\_}"
  ART_TEX=""
  [ -f "$ART" ] && ART_TEX="\includegraphics[width=0.86\textwidth]{$ART}"
  cat > "$WORK/front.tex" <<TEX
\documentclass[12pt]{article}
\usepackage[a4paper,margin=2.2cm]{geometry}
\usepackage{xeCJK}
\setCJKmainfont{Noto Serif CJK SC}
\setCJKsansfont{Noto Sans CJK SC}
\setCJKmonofont{Noto Sans Mono CJK SC}
\usepackage{graphicx}
\usepackage{xcolor}
\usepackage{tabularx}
\usepackage{hyperref}
\hypersetup{pdftitle={$TITLE_TEX v$VERSION},pdfauthor={$AUTHOR_TEX}}
\pagestyle{empty}
\definecolor{brand}{HTML}{1F4E79}
\begin{document}
\begin{center}
\vspace*{1.0cm}
{\sffamily\bfseries\color{brand}\fontsize{34}{40}\selectfont $TITLE_TEX\par}
\vspace{0.5cm}
{\sffamily\fontsize{17}{22}\selectfont $SUBTITLE_TEX\par}
\vspace{0.8cm}
{\sffamily\fontsize{13}{18}\selectfont HPM5E31IPB1 / hpm5e31\_1 最小系统板\par}
\vspace{0.4cm}
$ART_TEX
\vfill
{\sffamily\fontsize{14}{18}\selectfont 版本 v$VERSION\par}
\vspace{0.25cm}
{\sffamily\fontsize{12}{16}\selectfont $BUILD_DATE\par}
\vspace{0.25cm}
{\sffamily\fontsize{12}{16}\selectfont $AUTHOR_TEX\par}
\vspace{0.8cm}
\end{center}
\newpage
\vspace*{2.2cm}
\begin{center}
{\sffamily\bfseries\Large 版权与版本说明}
\end{center}
\vspace{0.8cm}
\renewcommand{\arraystretch}{1.45}
\begin{tabularx}{\textwidth}{@{}lX@{}}
书名 & $TITLE_TEX \\\\
副标题 & $SUBTITLE_TEX \\\\
版本 & v$VERSION（生成日期 $BUILD_DATE）\\\\
适用芯片 & HPM5E31IPB1（HPM5E00 系列，RV32-IMAFDCBP）\\\\
适用板卡 & hpm5e31\_1 最小系统板 \\\\
工具链 & HPM SDK v1.13.0 + RISC-V GCC 11.1（riscv32-unknown-elf）\\\\
文档体系 & 书稿为 Markdown，经 pandoc + XeLaTeX 自动合成，源文件见 docs/chapters/ \\\\
开源仓库 & \url{https://github.com/weifengdq/embedded}（\texttt{hpm5e31\_1/} 子目录） \\\\
质量门槛 & tools/check\_docs.py 全量检查通过方可发布 \\\\
生成命令 & \texttt{tools/build\_pdf.sh --version $VERSION} \\\\
免责声明 & 本书含实测数据与工程经验，不构成对芯片行为的最终保证；\\\\
 & 以 HPMicro 官方数据手册、用户手册与勘误表为准。 \\\\
生成方式 & pandoc + XeLaTeX 自动合成；书稿源 \url{https://github.com/weifengdq/embedded} \\\\
\end{tabularx}
\vfill
\begin{center}
\small 本 PDF 由脚本生成，请勿直接编辑；修改请改 docs/chapters/*.md 后重新合成。
\end{center}
\end{document}
TEX
  ( cd "$WORK" && xelatex -interaction=nonstopmode -halt-on-error front.tex >>"$LOG" 2>&1 ) \
    || { grep -nE "^!" front.log 2>/dev/null | head -10 >&2; die "封面编译失败"; }
  [ -f "$WORK/front.pdf" ] || die "封面未生成 front.pdf"
  FRONT_PDF="$WORK/front.pdf"
fi

# ---------- 合并封面 + 正文 + 写入三级书签 + 校验 ----------
info "==> 合并 PDF 并写入书签（章/节/小节三级）..."
MIN_ENTRIES=$(( ${#INCLUDED[@]} * 2 ))
FRONT_ARGS=()
[ -n "$FRONT_PDF" ] && FRONT_ARGS=(--front "$FRONT_PDF")
python3 "$ROOT/tools/pdf_toc.py" "${FRONT_ARGS[@]}" \
  --body "$WORK/body.pdf" --out "$OUT" \
  --title "$TITLE" --author "$AUTHOR" --version "$VERSION" \
  --min-entries "$MIN_ENTRIES" --require-level 3 >>"$LOG" 2>&1 \
  || die "书签写入/校验失败（详见 $LOG；可先跑 python3 tools/pdf_outline.py --check 检查）"
[ -f "$OUT" ] || die "未生成输出文件 $OUT"

# 独立二次校验：书签条目数 >= 章节数×2 且达到三级
python3 "$ROOT/tools/pdf_outline.py" --check "$OUT" --first 0 \
  --min-entries "$MIN_ENTRIES" --require-level 3 >>"$LOG" 2>&1 \
  || die "书签二次校验失败（详见 $LOG）"
BOOKMARKS=$(python3 -c "import sys; sys.path.insert(0,'tools'); import pdf_outline; i=pdf_outline.inspect('$OUT'); print(i['outline'] if i else 0)")
info "    bookmarks=$BOOKMARKS（要求 >= $MIN_ENTRIES，三级）"

# ---------- 刷新 docs/README.md 章节索引 ----------
if [ -f "$ROOT/tools/gen_index.py" ]; then
  ( cd "$ROOT" && python3 tools/gen_index.py ) >>"$LOG" 2>&1 \
    || info "WARNING: gen_index.py 运行失败"
fi

# ---------- 摘要 ----------
PAGES="$(pdfinfo "$OUT" 2>/dev/null | awk '/^Pages:/{print $2}')"
SIZE="$(du -h "$OUT" | cut -f1)"
{
  echo "  result: $OUT"
  echo "  pages : ${PAGES:-?}"
  echo "  size  : $SIZE"
  echo "  chapters: ${#INCLUDED[@]}  skipped: ${#MISSING[@]}"
} | tee -a "$LOG" >&3
[ "$KEEP" = 1 ] || rm -rf "$WORK"

info ""
info "OK  $OUT"
info "    pages=$PAGES size=$SIZE chapters=${#INCLUDED[@]} skipped=${#MISSING[@]} version=v$VERSION"
info "    log=$LOG"
exit 0
