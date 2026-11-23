#!/usr/bin/env python3
"""tools/polish_check.py —— 去 AI 味 / 文风检查（R5 增加）

扫描 docs/chapters/*.md 中的套话、空话、超长句和机械连接词，输出
docs/generated/polish-report.md，并把问题按章节汇总到终端。

用法:
  python3 tools/polish_check.py                 # 全量扫描 + 写报告
  python3 tools/polish_check.py --file docs/chapters/40-gs-usb-candlelight.md
  python3 tools/polish_check.py --top 10        # 终端只打印前 10 个文件
  python3 tools/polish_check.py --strict        # 有问题则退出码 1（CI 用）

说明: 这是"提示"而不是硬门槛——技术术语、引用原文、表格里的词不计入。
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHAPTERS = ROOT / "docs" / "chapters"
OUT = ROOT / "docs" / "generated" / "polish-report.md"

# 套话/空话（按严重度分组）
BANNED = {
    "机械连接词": ["首先，", "其次，", "最后，", "再者，", "另一方面，", "与此同时，", "综上所述", "总而言之", "总的来说", "简而言之", "换言之", "由此可见"],
    "空话套话": ["值得注意的是", "值得一提的是", "需要指出的是", "不难发现", "众所周知", "显而易见", "毋庸置疑", "不言而喻", "可以说", "从某种意义上说", "在一定程度上", "深入探讨", "赋能", "一站式", "全方位", "显著提升", "极大地", "极具", "至关重要", "不可忽视"],
    "AI 腔": ["本文将", "本节将", "本章将", "接下来我们", "接下来，我们", "下面让我们", "让我们", "我们将会", "希望本文", "相信读者", "读者可以", "可以看到", "可以看出", "需要注意的是"],
    "口号式": ["完美", "极致", "颠覆", "革命性", "业界领先", "最佳实践", "不可或缺"],
}
RE_FENCE = re.compile(r"^\s*\x60{3,}")
RE_INLINE = re.compile(r"\x60[^\x60\n]*\x60")
RE_URL = re.compile(r"https?://\S+")
SENT_SPLIT = re.compile(r"[。！？；\n]")


def strip_non_prose(line: str) -> str:
    """去掉行内代码、链接、表格分隔线，减少误报。"""
    if RE_FENCE.match(line):
        return ""
    line = RE_INLINE.sub("", line)
    line = RE_URL.sub("", line)
    if line.strip().startswith("|"):
        return ""          # 表格内容不查文风
    if line.lstrip().startswith(("#", ">", "-", "*", "1.", "2.", "3.")):
        line = line.lstrip("#>*- 0123456789.")
    return line


def scan_file(p: Path):
    issues = []
    text = p.read_text(encoding="utf-8", errors="ignore")
    in_fence = False
    for i, raw in enumerate(text.splitlines(), 1):
        if RE_FENCE.match(raw):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        line = strip_non_prose(raw)
        for kind, words in BANNED.items():
            for w in words:
                if w in line:
                    issues.append((i, kind, '"%s"' % w, raw.strip()[:90]))
        # 超长句（去代码后，按句号切分，> 90 个汉字）
        for sent in SENT_SPLIT.split(line):
            han = len(re.findall(r"[\u4e00-\u9fff]", sent))
            if han > 90:
                issues.append((i, "超长句", "%d 字" % han, sent.strip()[:90]))
    return issues


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", action="append", default=[])
    ap.add_argument("--top", type=int, default=50)
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args()

    files = [Path(f) for f in args.file] if args.file else sorted(CHAPTERS.glob("*.md"))
    rows = []
    total = 0
    for p in files:
        if not p.exists():
            print("missing:", p)
            continue
        iss = scan_file(p)
        if iss:
            rows.append((p, iss))
            total += len(iss)

    rows.sort(key=lambda r: -len(r[1]))
    print("== polish_check: %d 个文件，%d 处提示 ==" % (len(rows), total))
    for p, iss in rows[: args.top]:
        print("%-42s %3d 处" % (p.name, len(iss)))
        for ln, kind, word, snippet in iss[:3]:
            print("    L%-5d [%s] %s  %s" % (ln, kind, word, snippet))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    out = ["# 去 AI 味检查报告（自动生成）", "",
           "> 由 \x60tools/polish_check.py\x60 生成。提示项按章节聚合；表格/代码/引用不计。",
           "> 处理原则：能删就删，不能删就换成具体事实或数字；不要为了过检查把句子改得别扭。", ""]
    out.append("| 章节 | 提示数 | 首条位置 |")
    out.append("|---|---:|---|")
    for p, iss in rows:
        out.append("| %s | %d | L%d %s |" % (p.name, len(iss), iss[0][0], iss[0][2]))
    out.append("")
    for p, iss in rows:
        out.append("## %s（%d 处）" % (p.name, len(iss)))
        out.append("")
        out.append("| 行 | 类型 | 词/长度 | 原文片段 |")
        out.append("|---:|---|---|---|")
        for ln, kind, word, snippet in iss:
            sn = snippet.replace("|", "\\|")
            out.append("| %d | %s | %s | %s |" % (ln, kind, word, sn))
        out.append("")
    OUT.write_text("\n".join(out), encoding="utf-8")
    print("report:", OUT)
    if args.strict and total:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
