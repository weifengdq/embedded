#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""tools/latex_breaks.py —— 给 pandoc 生成的 LaTeX 正文插入断行点

问题：pandoc 把 Markdown 表格转成 longtable 的 p{} 定宽列，正文里的长路径
（如 $HOME/sdk_env_v1.13.0/hpm_sdk）和长标识符（如 hpm_soc_ip.h）
在 LaTeX 里没有合法断点，会把表格/正文撑出页面右边界被裁切。

做法：只在 begin{document} 之后、且不在下列命令的参数内部时：
  * 在 "/" 后插入 \allowbreak{}（允许在路径分隔符后换行）
  * 在 "\_"（pandoc 转义的下划线）后插入 \allowbreak{}（允许在标识符中换行）
保护对象：\includegraphics{...}（图片路径）、\url{}、\href{}、\label{}、\ref{}、\pageref{}。
不改语义、不改 URL，仅增加合法断行点。

用法：
  python3 tools/latex_breaks.py <body.tex> [--check]
  --check 只统计会新增多少个断点，不写文件（返回 0）
"""

import argparse
import re
import sys

MASK = re.compile(r"\\includegraphics(?:\[[^\]]*\])?\{[^}]*\}|\\url\{[^}]*\}|\\href\{[^}]*\}|\\label\{[^}]*\}|\\ref\{[^}]*\}|\\pageref\{[^}]*\}")
SLASH = re.compile(r"(?<=[^\s/])/(?=[^\s])(?!\\allowbreak)")
UNDER = re.compile(r"\\_(?=[A-Za-z0-9])(?!\\allowbreak)")
BEGIN = "\\begin{document}"
END = "\\end{document}"


def transform(s):
    s = SLASH.sub(r"/\\allowbreak{}", s)
    s = UNDER.sub(r"\\_\\allowbreak{}", s)
    return s


def process(text):
    start = text.find(BEGIN)
    end = text.rfind(END)
    if start < 0 or end < 0:
        return text, 0
    head, body, tail = text[:start], text[start:end], text[end:]
    count = 0
    out = []
    last = 0
    for m in MASK.finditer(body):
        seg = transform(body[last:m.start()])
        count += seg.count("\\allowbreak{}")
        out.append(seg)
        out.append(m.group(0))
        last = m.end()
    seg = transform(body[last:])
    count += seg.count("\\allowbreak{}")
    out.append(seg)
    return head + "".join(out) + tail, count


def main(argv=None):
    ap = argparse.ArgumentParser(description="给 pandoc LaTeX 正文插入断行点")
    ap.add_argument("tex", help="pandoc 生成的 .tex 文件")
    ap.add_argument("--check", action="store_true", help="只统计，不写文件")
    args = ap.parse_args(argv)
    text = open(args.tex, encoding="utf-8").read()
    new, count = process(text)
    print("inserted %d breakpoints into %s" % (count, args.tex))
    if not args.check:
        open(args.tex, "w", encoding="utf-8").write(new)
    return 0


if __name__ == "__main__":
    sys.exit(main())
