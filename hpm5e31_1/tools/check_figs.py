#!/usr/bin/env python3
"""tools/check_figs.py —— 图资产检查（R5 增加）

检查 docs/figures/ 下的每张 PNG 是否在 docs/chapters/*.md 中被引用（按路径或文件名），
并统计缺失图片（正文引用了但文件不存在）。用于避免"生成了图但没放进书里"。

用法:
  python3 tools/check_figs.py            # 列出孤儿图与缺图，孤儿图非空则退出码 1
  python3 tools/check_figs.py --warn     # 只警告，不改变退出码
  python3 tools/check_figs.py --json
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
FIGS = DOCS / "figures"
CH = DOCS / "chapters"
IMG = re.compile(r"!\[[^\]]*\]\(([^)\s]+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--warn", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    texts = "\n".join(p.read_text(encoding="utf-8") for p in sorted(CH.glob("*.md")))
    texts += "\n" + (DOCS / "README.md").read_text(encoding="utf-8")

    orphans = []
    total = 0
    for f in sorted(FIGS.rglob("*.png")):
        total += 1
        if "_common" in f.parts:
            continue
        rel = f.relative_to(DOCS).as_posix()
        if rel not in texts and f.name not in texts:
            orphans.append(rel)

    missing = []
    for p in sorted(CH.glob("*.md")):
        for m in IMG.finditer(p.read_text(encoding="utf-8")):
            target = m.group(1)
            if target.startswith(("http", "/")):
                continue
            cand = (DOCS / target).resolve()
            if not cand.exists():
                missing.append((p.name, target))

    result = {"total": total, "orphans": orphans, "missing": missing}
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=1))
    else:
        print("图总数 %d，孤儿图 %d，缺图 %d" % (total, len(orphans), len(missing)))
        for o in orphans:
            print("  孤儿:", o)
        for name, t in missing:
            print("  缺图:", name, "->", t)
    if args.warn:
        return 0
    return 1 if (orphans or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
