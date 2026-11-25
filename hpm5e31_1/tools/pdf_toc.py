#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""tools/pdf_toc.py —— 合并 front+body、写入 PDF 书签(outline)与元数据、并自校验

背景：xelatex 生成的 body.pdf 自带 hyperref 书签（章节/小节），但旧的合成方式用
pdfunite 合并封面后会把 outline 丢掉。本脚本改用 PyMuPDF(fitz) 合并，并按
"body 自带书签 + 前置书签"重建三级书签，最后重新打开校验条目数/层级，防止回归。

书签层级：
  1 封面 / 版权页 / 目录 / 插图目录（前置页）
  1 章（第 N 章 ...）
  2 节（1. ... / 2. ...）
  3 小节（2.1 ...）
（章节书签由 body.pdf 的 hyperref 书签继承；正文中的 H3 已通过 preamble 的
 bookmarksdepth=3 打开。若某章无 H3，则该章书签只到 2 级。）

用法（一般由 tools/build_pdf.sh 调用，也可单独跑）：
  python3 tools/pdf_toc.py --body body.pdf --out final.pdf \
      --title "HPM5E31 实战手册" --author "hpm5e31_1 项目组" --version 0.4.0
  python3 tools/pdf_toc.py --front cover.pdf --body body.pdf --out final.pdf ...
  python3 tools/pdf_toc.py --body body.pdf --out final.pdf --min-entries 58 --require-level 3

退出码：0 成功且校验通过；2 参数/输入错误；3 书签校验失败。
"""

import argparse
import re
import sys
import fitz


def detect_front_pages(body_doc, max_scan=24):
    """在 body.pdf 前部找"目 录"与"插图目录"所在页（1-based）。"""
    toc_page = None
    lof_page = None
    for i in range(min(max_scan, body_doc.page_count)):
        text = body_doc[i].get_text()
        head = "\n".join([l.strip() for l in text.splitlines() if l.strip()][:8])
        if toc_page is None and re.search(r"^目\s*录$", head, flags=re.M):
            toc_page = i + 1
        if lof_page is None and re.search(r"插图目录", head):
            lof_page = i + 1
        if toc_page and lof_page:
            break
    return toc_page, lof_page


def clean_title(t):
    t = re.sub(r"\\numberline\{[^}]*\}", "", t)
    t = re.sub(r"\\texorpdfstring\{([^{}]*)\}\{[^{}]*\}", r"\1", t)
    t = t.replace("{", "").replace("}", "")
    return " ".join(t.split())


def main(argv=None):
    ap = argparse.ArgumentParser(description="合并 PDF 并写入三级书签 + 校验")
    ap.add_argument("--front", help="封面/版权页 PDF（可选；有则合并到最前面）")
    ap.add_argument("--body", required=True, help="xelatex 生成的正文 PDF（带 hyperref 书签）")
    ap.add_argument("--out", required=True, help="输出 PDF")
    ap.add_argument("--title", default="HPM5E31 实战手册")
    ap.add_argument("--author", default="hpm5e31_1 项目组")
    ap.add_argument("--version", default="")
    ap.add_argument("--subject", default="HPM5E31 原理、例程、上板实测与经验")
    ap.add_argument("--min-entries", type=int, default=0, help="书签条目数下限（建议=章节数×2）")
    ap.add_argument("--require-level", type=int, default=0, help="要求达到的最大书签层级（建议 3）")
    args = ap.parse_args(argv)

    try:
        body = fitz.open(args.body)
    except Exception as e:  # noqa: BLE001
        sys.stderr.write("ERROR: 无法打开 body PDF %s: %s\n" % (args.body, e))
        return 2
    front_doc = None
    if args.front:
        try:
            front_doc = fitz.open(args.front)
        except Exception as e:  # noqa: BLE001
            sys.stderr.write("ERROR: 无法打开 front PDF %s: %s\n" % (args.front, e))
            return 2

    shift = front_doc.page_count if front_doc is not None else 0
    body_toc = body.get_toc(simple=True)  # [level, title, page(1-based)]
    toc_page, lof_page = detect_front_pages(body)

    front_entries = []
    if front_doc is not None:
        front_entries.append([1, "封面", 1])
        if front_doc.page_count >= 2:
            front_entries.append([1, "版权页", 2])
    if toc_page:
        front_entries.append([1, "目录", toc_page + shift])
    if lof_page:
        front_entries.append([1, "插图目录", lof_page + shift])
    front_entries.sort(key=lambda e: e[2])   # 按实际页码排序（物理顺序）
    entries = front_entries

    prev_level = 1
    for lvl, title, page in body_toc:
        title = clean_title(title)
        if not title:
            continue
        lvl = max(1, min(int(lvl), prev_level + 1))
        entries.append([lvl, title, int(page) + shift])
        prev_level = lvl

    if not entries:
        sys.stderr.write("ERROR: 没有可写入的书签（body.pdf 无 outline？）\n")
        return 3

    # 合并
    out = fitz.open()
    if front_doc is not None:
        out.insert_pdf(front_doc)
    out.insert_pdf(body)

    # 元数据
    meta = dict(out.metadata or {})
    meta.update({
        "title": ("%s v%s" % (args.title, args.version)).strip(),
        "author": args.author,
        "subject": args.subject,
        "keywords": "HPM5E31, HPM5E00, RISC-V, 实战手册, 例程, 实测, 勘误",
        "creator": "pandoc + XeLaTeX",
        "producer": "PyMuPDF (bookmarks)",
    })
    out.set_metadata(meta)
    out.set_toc(entries)
    out.save(args.out, garbage=3, deflate=True)
    out.close()
    body.close()
    if front_doc is not None:
        front_doc.close()

    # 重新打开校验（防止写入失败/丢失）
    chk = fitz.open(args.out)
    toc = chk.get_toc(simple=True)
    pages = chk.page_count
    levels = {}
    for e in toc:
        levels[e[0]] = levels.get(e[0], 0) + 1
    max_level = max(levels) if levels else 0
    chk.close()

    print("pdf_toc: %s  pages=%d  outline=%d  levels=%s  shift=%d" %
          (args.out, pages, len(toc), dict(sorted(levels.items())), shift))
    if not toc:
        sys.stderr.write("ERROR: 输出 PDF 没有书签\n")
        return 3
    if args.min_entries and len(toc) < args.min_entries:
        sys.stderr.write("ERROR: 书签条目 %d < 下限 %d（章节数×2）\n" % (len(toc), args.min_entries))
        return 3
    if args.require_level and max_level < args.require_level:
        sys.stderr.write("ERROR: 最大书签层级 %d < 要求 %d（应为章/节/小节三级）\n"
                         % (max_level, args.require_level))
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
