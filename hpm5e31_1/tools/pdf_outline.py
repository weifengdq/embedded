#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""tools/pdf_outline.py —— PDF 书签(outline)验收工具

用途：合成后验收、交接自查。只读文件，不修改 PDF。

用法：
  python3 tools/pdf_outline.py --check final.pdf
  python3 tools/pdf_outline.py --check final.pdf --min-entries 58 --require-level 3
  python3 tools/pdf_outline.py --dump final.pdf          # 全部书签（level<TAB>page<TAB>title）
  python3 tools/pdf_outline.py --json final.pdf          # 机器可读
  python3 tools/pdf_outline.py --check final.pdf --first 10

退出码：0 通过；1 不合格（无书签/条目不足/层级不够）；2 无法打开文件。
"""

import argparse
import json
import sys

try:
    import fitz
except ImportError:  # pragma: no cover
    sys.stderr.write("ERROR: 缺少 PyMuPDF，请先 pip install pymupdf\n")
    sys.exit(2)


def inspect(path):
    try:
        d = fitz.open(path)
    except Exception as e:  # noqa: BLE001
        sys.stderr.write("ERROR: 无法打开 %s: %s\n" % (path, e))
        return None
    toc = d.get_toc(simple=True)
    levels = {}
    for e in toc:
        levels[e[0]] = levels.get(e[0], 0) + 1
    info = dict(
        path=path,
        pages=d.page_count,
        outline=len(toc),
        levels=dict(sorted(levels.items())),
        max_level=max(levels) if levels else 0,
        metadata={k: v for k, v in (d.metadata or {}).items() if v},
        entries=toc,
    )
    d.close()
    return info


def main(argv=None):
    ap = argparse.ArgumentParser(description="PDF 书签验收（只读）")
    ap.add_argument("pdf", help="PDF 文件路径")
    ap.add_argument("--check", action="store_true", help="检查模式（默认）：打印统计并校验")
    ap.add_argument("--dump", action="store_true", help="导出全部书签")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--first", type=int, default=20, help="检查模式打印前 N 条（默认 20）")
    ap.add_argument("--min-entries", type=int, default=1, help="条目数下限（默认 1）")
    ap.add_argument("--require-level", type=int, default=3, help="要求最大层级（默认 3）")
    args = ap.parse_args(argv)

    info = inspect(args.pdf)
    if info is None:
        return 2

    if args.json:
        print(json.dumps(info, ensure_ascii=False, indent=2))
        return 0

    if args.dump:
        for lvl, title, page in info["entries"]:
            print("%d\t%d\t%s" % (lvl, page, title))
        return 0

    print("文件      : %s" % info["path"])
    print("页数      : %d" % info["pages"])
    print("书签条目  : %d" % info["outline"])
    print("层级分布  : %s（最大 %d 级）" % (info["levels"], info["max_level"]))
    md = info["metadata"]
    print("元数据    : title=%r author=%r" % (md.get("title"), md.get("author")))
    print("前 %d 条书签：" % args.first)
    for lvl, title, page in info["entries"][:args.first]:
        print("  %s %-4d %s" % ("  " * (lvl - 1) + "└─" if lvl > 1 else "├─", page, title))

    problems = []
    if info["outline"] == 0:
        problems.append("没有书签")
    if args.min_entries and info["outline"] < args.min_entries:
        problems.append("条目数 %d < 下限 %d" % (info["outline"], args.min_entries))
    if args.require_level and info["max_level"] < args.require_level:
        problems.append("最大层级 %d < 要求 %d" % (info["max_level"], args.require_level))
    if problems:
        print("\n结果: FAIL —— " + "；".join(problems))
        return 1
    print("\n结果: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
