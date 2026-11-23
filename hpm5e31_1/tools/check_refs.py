#!/usr/bin/env python3
"""tools/check_refs.py —— 书稿交叉引用与资源存在性检查（最终 QA 用）

检查项：
  1. 章节里引用的 logs/*.log 是否真实存在（支持 logs/xxx.log 与 logs/.last_xxx 之外的精确路径）；
  2. 章节里引用的图片（figures/...png）是否存在、是否 0 字节、是否 PNG 且能读出宽高（损坏检测）；
  3. docs/figures/ 下所有 PNG 是否为合法 PNG（0 字节/截断/非 PNG 报错）；
  4. "第 X 章"引用是否在 1~28 范围内，且 00-目录.md 中该章存在；
  5. "图 X-Y" 与图文件命名的章号是否一致（弱检查）。

用法：
  python3 tools/check_refs.py                 # 检查全部章节
  python3 tools/check_refs.py --file docs/chapters/23-benchmarks.md
  python3 tools/check_refs.py --json
  python3 tools/check_refs.py --figures-only  # 只检查 docs/figures 下 PNG 完整性

退出码：0 全部通过；1 有问题。
"""

import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
CHAPTERS = DOCS / "chapters"
TOC = DOCS / "00-目录.md"

RE_LOG = re.compile(r"(?:logs/)([0-9A-Za-z._\-]+\.log)")
RE_FIG = re.compile(r"!\[[^\]]*\]\(([^)\s]+\.(?:png|jpg|jpeg|svg|webp))")
RE_CHREF = re.compile(r"第\s*(\d{1,2})\s*章")


def png_info(path):
    """返回 (ok, width, height, reason)。不依赖 Pillow。"""
    try:
        data = path.read_bytes()
    except OSError as e:
        return False, 0, 0, "无法读取: %s" % e
    if not data:
        return False, 0, 0, "0 字节文件"
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return False, 0, 0, "不是 PNG（magic 不符，可能是 JPG/占位）"
    if len(data) < 33:
        return False, 0, 0, "PNG 截断（<33 字节）"
    if data[12:16] != b"IHDR":
        return False, 0, 0, "缺少 IHDR"
    w, h = struct.unpack(">II", data[16:24])
    if w == 0 or h == 0:
        return False, 0, 0, "宽高为 0"
    return True, w, h, ""


def parse_toc():
    chs = {}
    if TOC.exists():
        for line in TOC.read_text(encoding="utf-8").splitlines():
            m = re.match(r"^\|\s*(\d{2})\s*\|\s*([^|]+?)\s*\|\s*([0-9A-Za-z_.\-]+\.md)", line)
            if m:
                chs[int(m.group(1))] = m.group(2).strip()
    return chs


def main(argv=None):
    ap = argparse.ArgumentParser(description="书稿交叉引用与资源存在性检查")
    ap.add_argument("--file", "-f", action="append", default=[], help="只检查指定章节")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--figures-only", action="store_true")
    args = ap.parse_args(argv)

    errors, warnings = [], []
    toc = parse_toc()
    files = []
    if args.file:
        files = [Path(f).resolve() for f in args.file]
    elif not args.figures_only:
        files = sorted(CHAPTERS.glob("*.md"))

    # ---- 1/2/4/5：章节引用 ----
    for p in files:
        rel = p.relative_to(ROOT) if p.is_relative_to(ROOT) else p
        text = p.read_text(encoding="utf-8")
        body = re.sub(r"\x60[^\x60\n]*\x60", "", text)  # 行内代码里的示例路径跳过
        for log in set(RE_LOG.findall(text)):
            if re.search(r"YYYY|HHMMSS|示例|<", log):
                continue
            if not (ROOT / "logs" / log).exists():
                # 只对正文（非代码块）里的引用报错
                if re.search(r"logs/" + re.escape(log), body):
                    errors.append((str(rel), "日志不存在: logs/%s" % log))
        for fig in set(RE_FIG.findall(text)):
            q = fig.split("#")[0]
            cand = (DOCS / q) if (DOCS / q).exists() else (p.parent / q)
            if not cand.exists():
                errors.append((str(rel), "图片不存在: %s" % fig))
                continue
            ok, w, h, why = png_info(cand) if cand.suffix.lower() == ".png" else (True, 0, 0, "")
            if not ok:
                errors.append((str(rel), "图片损坏: %s（%s）" % (fig, why)))
            elif w and (w < 200 or h < 120):
                warnings.append((str(rel), "图片过小: %s（%dx%d）" % (fig, w, h)))
            m = re.match(r"(\d{2})-", p.name)
            if m and q.startswith("figures/") and not q.startswith("figures/" + m.group(1)):
                warnings.append((str(rel), "图片不在本章目录: %s" % fig))
        for ch in set(RE_CHREF.findall(text)):
            n = int(ch)
            if n not in toc:
                warnings.append((str(rel), "引用的章号未在 00-目录.md 登记: 第 %d 章" % n))

    # ---- 2.5 图表编号一致性（重复号、图题号与文件名不一致） ----
    for p in files:
        rel = p.relative_to(ROOT) if p.is_relative_to(ROOT) else p
        text = p.read_text(encoding="utf-8")
        figs = re.findall(r"!\[[^\]]*?图\s*(\d{1,2})-(\d{1,2})[^\]]*?\]\(([^)\s]+)", text)
        seen = {}
        for ch, no, path_ in figs:
            key = (int(ch), int(no))
            seen[key] = seen.get(key, 0) + 1
            m = re.search(r"fig-(\d{1,2})-(\d{1,2})", path_)
            if m and (int(m.group(1)), int(m.group(2))) != key:
                warnings.append((str(rel), "图题号 图 %s-%s 与文件名 %s 不一致" % (ch, no, path_.split("/")[-1])))
        for key, n in seen.items():
            if n > 1:
                errors.append((str(rel), "图号重复 %d 次: 图 %d-%d" % (n, key[0], key[1])))
        tabs = re.findall(r"^表\s*(\d{1,2})-(\d{1,2})\s*[：:]", text, flags=re.M)
        seen_t = {}
        for ch, no in tabs:
            k = (int(ch), int(no))
            seen_t[k] = seen_t.get(k, 0) + 1
        for key, n in seen_t.items():
            if n > 1:
                warnings.append((str(rel), "表题重复定义 %d 次: 表 %d-%d" % (n, key[0], key[1])))

    # ---- 3：全量 PNG 完整性 ----
    for png in sorted((DOCS / "figures").rglob("*.png")):
        ok, w, h, why = png_info(png)
        if not ok:
            errors.append((str(png.relative_to(ROOT)), "PNG 损坏: %s" % why))
        elif w < 200 or h < 120:
            warnings.append((str(png.relative_to(ROOT)), "PNG 过小: %dx%d" % (w, h)))

    if args.json:
        print(json.dumps(dict(ok=not errors,
                              errors=[dict(file=a, message=b) for a, b in errors],
                              warnings=[dict(file=a, message=b) for a, b in warnings]),
                         ensure_ascii=False, indent=2))
    else:
        for f, m in errors:
            print("ERROR %s: %s" % (f, m))
        for f, m in warnings:
            print("WARN  %s: %s" % (f, m))
        print("\n结果：%d 错误，%d 警告 -> %s" % (len(errors), len(warnings), "FAIL" if errors else "PASS"))
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
