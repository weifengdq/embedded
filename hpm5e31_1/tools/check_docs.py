#!/usr/bin/env python3
"""tools/check_docs.py —— 书稿质量门槛检查（对应 docs/写作规范.md 第 6 节）

章节分两类，门槛不同（见 docs/00-目录.md 的负责人/定位）：
  * 外设/专题章（03~24、26、27）：完整门槛
      ≥250 行、≥8 个二级标题、≥3 张表、≥2 张图、≥1 段代码块、
      含"文档信息/本章导览/概述/工作原理/寄存器/例程/实测/性能/坑/对比/
      参考来源/待确认项与风险"关键词；
  * 综述/环境/附录章（00、01、02、25、41）：简化门槛
      ≥120 行、≥6 个二级标题、≥2 张表、≥1 段代码块或图、
      必须有"参考来源"和"待确认项与风险"，不检查外设 5 关键词。

用法：
  python3 tools/check_docs.py                     # 检查 docs/chapters/ 下全部章节
  python3 tools/check_docs.py --file docs/chapters/26-registers-errata.md
  python3 tools/check_docs.py --json              # 机器可读输出（给 CI / Lead 汇总）
  python3 tools/check_docs.py --strict            # 警告也算失败
  python3 tools/check_docs.py --min-lines 200     # 放宽外设章行数门槛（默认 250）
  python3 tools/check_docs.py --toc               # 额外检查 00-目录.md 登记文件是否存在
  python3 tools/check_docs.py -q                  # 只打印统计与错误

退出码：0 全部通过；1 有错误（或 --strict 下有警告）；2 用法错误。
依赖：仅 Python 3 标准库。
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
CHAPTERS = DOCS / "chapters"
TOC_FILE = DOCS / "00-目录.md"

RE_FENCE = re.compile(r"^\s*(\x60{3,})(.*)$")
RE_HEADING = re.compile(r"^(#{1,6})\s+(.*)$")
RE_TABLE_SEP = re.compile(r"^\s*\|[\s:|-]+\|\s*$")
RE_IMAGE = re.compile(r"!\[([^\]]*)\]\(([^)\s]+)")
RE_LINK = re.compile(r"(!?)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
RE_FIGNO = re.compile(r"图\s*(\d{1,2})-(\d{1,2})")
RE_TABNO = re.compile(r"表\s*(\d{1,2})-(\d{1,2})")
RE_CHNO = re.compile(r"^#\s*第\s*(\d{1,2})\s*章")
RE_TODO = re.compile(r"\b(TODO|TBD|FIXME|XXX)\b|待补充|待填写|占位")
# R7：占位符（如 __BULK_IN_ACK__）必须被拦住——写作规范禁止占位，PDF 会原样印出来
# 排除常见的双下划线宏（SDK 的 __ENABLE_xxx / __DISABLE_xxx、lwIP 的 __LWIP_xxx 等）
RE_PLACEHOLDER = re.compile(r"_{2,}(?!(?:ENABLE|DISABLE|LWIP|CONFIG|USE|HPM|PACKED|WEAK|INLINE|ASM|attribute)__?)[A-Z][A-Z0-9_]{2,}_{1,}")
RE_INLINE_CODE = re.compile(r"\x60[^\x60\n]*\x60")
RE_ASCII_ADJ_CJK = re.compile(r"([\u4e00-\u9fff])\s*([,;:])\s*([\u4e00-\u9fff])")
RE_ASCII_PAREN = re.compile(r"([\u4e00-\u9fff])\(([^)]*)\)([\u4e00-\u9fff])")

# 综述/环境/附录型章节（不按外设章门槛检查）
REVIEW_CHAPTERS = {"00", "01", "02", "25", "51"}
# 这些二级标题下的"待补充/待确认"是写作规范允许的，不按占位错误处理
SOFT_SECTION = re.compile(r"待确认|局限|风险|已知|扩展阅读|参考来源")


class Result:
    def __init__(self):
        self.errors = []
        self.warnings = []
        self.stats = []

    def err(self, path, line, msg):
        self.errors.append((str(path), line, msg))

    def warn(self, path, line, msg):
        self.warnings.append((str(path), line, msg))


def chapter_type(path):
    m = re.match(r"(\d{2})-", path.name)
    ch = m.group(1) if m else "??"
    return ("review" if ch in REVIEW_CHAPTERS else "peripheral"), ch


def strip_code_blocks(text):
    """返回与原文等行数的列表，代码块内行替换为空行（用于标题/正文分析）。"""
    out = []
    in_fence = False
    for line in text.splitlines():
        if RE_FENCE.match(line):
            in_fence = not in_fence
            out.append("")
            continue
        out.append("" if in_fence else line)
    return out


def resolve_target(path, target):
    """把 Markdown 链接目标解析到存在的文件；按章节目录、docs/、仓库根依次尝试。"""
    if not target or target.startswith(("http://", "https://", "mailto:", "#", "ftp:")):
        return True
    raw = target.split("#", 1)[0]
    if not raw:
        return True
    cands = [path.parent / raw, DOCS / raw, ROOT / raw]
    return any(c.exists() for c in cands)


def check_file(path, res, min_lines):
    rel = path.relative_to(ROOT)
    ctype, ch = chapter_type(path)
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        res.err(rel, 1, "不是 UTF-8 编码")
        return
    lines = text.splitlines()
    n_lines = len(lines)
    if "\r" in text:
        res.err(rel, 1, "含 CRLF 行尾，请统一为 LF")

    body = strip_code_blocks(text)          # 代码块已清空，供标题/正文分析
    body_text = "\n".join(body)

    # ---- 统计 ----
    headings = []  # (行号, 级别, 文本)
    for i, l in enumerate(body):
        m = RE_HEADING.match(l)
        if m:
            headings.append((i + 1, len(m.group(1)), m.group(2).strip()))
    n_sections = sum(1 for _, lvl, _ in headings if lvl == 2)
    n_tables = sum(1 for l in lines if RE_TABLE_SEP.match(l))
    images = RE_IMAGE.findall(text)
    n_images = len(images)
    fences = [l for l in lines if RE_FENCE.match(l)]
    n_code_blocks = len(fences) // 2
    stats = dict(type=ctype, lines=n_lines, sections=n_sections, tables=n_tables,
                 figures=n_images, code_blocks=n_code_blocks)
    res.stats.append((str(rel), stats))

    # ---- 门槛（按章节类型） ----
    need_lines = min_lines if ctype == "peripheral" else min(120, min_lines)
    need_sections = 8 if ctype == "peripheral" else 6
    if n_lines < need_lines:
        res.err(rel, 0, "行数 %d < 门槛 %d（%s 章）" % (n_lines, need_lines, ctype))
    if n_sections < need_sections:
        res.err(rel, 0, "二级标题 %d 个 < %d（%s 章）" % (n_sections, need_sections, ctype))
    if n_tables < 2:
        res.err(rel, 0, "表格 %d 张 < 2" % n_tables)
    if ctype == "peripheral":
        if n_tables < 3:
            res.err(rel, 0, "表格 %d 张 < 3（外设章）" % n_tables)
        if n_images < 2:
            res.err(rel, 0, "图 %d 张 < 2（外设章）" % n_images)
        if n_code_blocks < 1:
            res.err(rel, 0, "代码块 0 段 < 1（外设章）")
    else:
        if n_images + n_code_blocks < 1:
            lvl = res.warn if ch == "00" else res.err
            lvl(rel, 0, "综述章建议至少有 1 张图或 1 段代码块")

    # ---- 标题与必备内容 ----
    first = next((l for l in body if l.startswith("#")), "")
    m = RE_CHNO.match(first)
    if not m:
        res.err(rel, 1, "首个一级标题不是 '第 N 章 ...' 形式：%r" % first[:40])
    elif m.group(1) and int(m.group(1)) != int(ch):
        res.err(rel, 1, "章号与文件名不一致：标题 %s / 文件 %s" % (m.group(1), ch))

    required_common = ["文档信息", "本章导览", "参考来源", "待确认项与风险"]
    for kw in required_common:
        if kw not in body_text:
            res.err(rel, 0, "缺少必备内容/小节关键词：%s" % kw)
    if ctype == "peripheral":
        for kw in ["概述", "工作原理", "寄存器", "例程", "实测", "性能", "坑", "对比"]:
            if kw not in body_text:
                res.err(rel, 0, "外设章缺少必备内容/小节关键词：%s" % kw)
    for kw in ["实测日期", "可信度", "对应例程"]:
        if kw not in body_text:
            res.warn(rel, 0, "文档信息表缺少字段：%s" % kw)

    # ---- 代码块 ----
    # 代码围栏成对性 + 闭合检查（可发现"只有开围栏没有闭围栏"的生成 bug）
    fence_open = None
    in_fence = False
    for i, l in enumerate(lines):
        mf = RE_FENCE.match(l)
        if not mf:
            continue
        ticks = len(mf.group(1))
        info = mf.group(2).strip()
        if not in_fence:
            if not info:
                res.warn(rel, i + 1, "代码块未标注语言")
            in_fence = True
            fence_open = (i + 1, ticks)
        else:
            if info:
                res.err(rel, fence_open[0],
                        "代码块未闭合（第 %d 行是新的带语言围栏 %r）" % (i + 1, l.strip()[:24]))
                fence_open = (i + 1, ticks)
            else:
                if ticks < fence_open[1]:
                    res.err(rel, i + 1, "闭合围栏短于开始围栏（第 %d 行）" % fence_open[0])
                in_fence = False
                fence_open = None
    if in_fence and fence_open:
        res.err(rel, fence_open[0], "代码块未闭合（到文件末尾）")

    # ---- 图 ----
    for alt, target in images:
        if not resolve_target(path, target):
            res.err(rel, 0, "图片不存在：%s" % target)
        elif not target.lower().split("#")[0].endswith((".png", ".jpg", ".jpeg", ".svg", ".webp")):
            res.warn(rel, 0, "图片扩展名不常见：%s" % target)
        mf = RE_FIGNO.search(alt)
        if not mf:
            res.warn(rel, 0, "图题缺少 '图 N-M' 编号：%s" % alt[:30])
        elif int(mf.group(1)) != int(ch):
            res.warn(rel, 0, "图号章号 %s 与文件章号 %s 不一致：%s" % (mf.group(1), ch, alt[:30]))

    # ---- 表号引用：至少引用到一半的表（附录大表不要求逐个引用） ----
    n_tab_refs = len(set(RE_TABNO.findall(body_text)))
    if n_tables and n_tab_refs * 2 < n_tables:
        res.warn(rel, 0, "表 %d 张但正文只引用了 %d 个表号（应至少引用一半）" % (n_tables, n_tab_refs))

    # ---- 链接 ----
    for bang, target in RE_LINK.findall(text):
        if not resolve_target(path, target):
            res.err(rel, 0, "相对链接目标不存在：%s" % target)

    # ---- 占位文本（"待确认项与风险"等章节内允许） ----
    cur_section = ""
    in_fence = False
    for i, l in enumerate(lines):
        if RE_FENCE.match(l):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        mh = RE_HEADING.match(l)
        if mh:
            cur_section = mh.group(2)
        mt = RE_TODO.search(l)
        if mt and not SOFT_SECTION.search(cur_section) and "待确认" not in l:
            res.err(rel, i + 1, "发现占位文本 %r" % l.strip()[:60])
        mp = RE_PLACEHOLDER.search(l)
        if mp:
            res.err(rel, i + 1, "发现未替换的占位符 %r（PDF 会原样印出）" % mp.group(0))

    # ---- 空白/行尾 ----
    blank_run = 0
    for i, l in enumerate(lines):
        if l.strip() == "":
            blank_run += 1
            if blank_run >= 3:
                res.err(rel, i + 1, "连续 3 个以上空行")
                blank_run = 0
        else:
            blank_run = 0
    for i, l in enumerate(lines):
        if l != l.rstrip():
            res.warn(rel, i + 1, "行尾有多余空格")

    # ---- 中文标点 ----
    prose = RE_INLINE_CODE.sub("", body_text)
    for i, l in enumerate(prose.splitlines()):
        for mm in RE_ASCII_ADJ_CJK.finditer(l):
            res.warn(rel, i + 1, "中文之间用了半角标点 %r，应为全角" % mm.group(2))
        for mm in RE_ASCII_PAREN.finditer(l):
            res.warn(rel, i + 1, "中文语境用了半角括号：(%s)" % mm.group(2)[:20])

    # ---- 标题层级（代码块已排除，不会再把 #include 当标题） ----
    prev = 0
    for ln, lvl, _ in headings:
        if prev and lvl > prev + 1:
            res.warn(rel, ln, "标题层级跳跃：H%d -> H%d" % (prev, lvl))
        prev = lvl


def parse_toc(res):
    rows = []
    if not TOC_FILE.exists():
        res.err(TOC_FILE.relative_to(ROOT), 0, "找不到 docs/00-目录.md")
        return rows
    for i, line in enumerate(TOC_FILE.read_text(encoding="utf-8").splitlines(), 1):
        m = re.match(r"^\|\s*(\d{2})\s*\|\s*([^|]+?)\s*\|\s*([0-9A-Za-z_.\-]+\.md)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|", line)
        if m:
            rows.append(dict(ch=m.group(1), title=m.group(2).strip(), file=m.group(3).strip(),
                             owner=m.group(4).strip(), status=m.group(5).strip(), line=i))
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description="书稿质量门槛检查（docs/写作规范.md 第 6 节）")
    ap.add_argument("--file", "-f", action="append", default=[], help="只检查指定文件（可多次）")
    ap.add_argument("--min-lines", type=int, default=250, help="外设章最少行数，默认 250")
    ap.add_argument("--strict", action="store_true", help="警告也算失败")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--toc", action="store_true", help="检查 00-目录.md 中登记的章节文件是否存在")
    ap.add_argument("--quiet", "-q", action="store_true", help="只打印统计与错误")
    args = ap.parse_args(argv)

    res = Result()
    if args.file:
        files = []
        for f in args.file:
            p = Path(f)
            if not p.is_absolute():
                p = (ROOT / f) if (ROOT / f).exists() else (Path.cwd() / f)
            if not p.exists():
                sys.stderr.write("ERROR: 文件不存在: %s\n" % f)
                return 2
            files.append(p.resolve())
    else:
        files = sorted(CHAPTERS.glob("*.md"))
        if not files:
            sys.stderr.write("WARNING: docs/chapters/ 下没有 .md 文件\n")

    for p in files:
        check_file(p, res, args.min_lines)

    toc_rows = []
    if args.toc:
        toc_rows = parse_toc(res)
        for r in toc_rows:
            f = CHAPTERS / r["file"]
            if not f.exists():
                lvl = res.warn if r["status"] in ("待写", "进行中") else res.err
                lvl(TOC_FILE.relative_to(ROOT), r["line"], "目录登记但文件不存在：%s" % r["file"])

    failed = bool(res.errors) or (args.strict and bool(res.warnings))
    if args.json:
        out = dict(
            ok=not failed,
            files=[str(p.relative_to(ROOT)) for p in files],
            stats=[dict(file=s[0], **s[1]) for s in res.stats],
            errors=[dict(file=a, line=b, message=c) for a, b, c in res.errors],
            warnings=[dict(file=a, line=b, message=c) for a, b, c in res.warnings],
            toc=[{k: v for k, v in r.items()} for r in toc_rows],
        )
        print(json.dumps(out, ensure_ascii=False, indent=2))
    else:
        print("== 统计 ==")
        for f, st in res.stats:
            print("  %-42s [%-10s] 行 %4d | H2 %2d | 表 %2d | 图 %2d | 代码 %2d"
                  % (Path(f).name, st["type"], st["lines"], st["sections"], st["tables"],
                     st["figures"], st["code_blocks"]))
        for f, line, msg in res.errors:
            print("ERROR %s:%s %s" % (f, line, msg))
        for f, line, msg in res.warnings:
            print("WARN  %s:%s %s" % (f, line, msg))
        print("\n结果：%d 个文件，%d 错误，%d 警告 -> %s"
              % (len(files), len(res.errors), len(res.warnings),
                 "FAIL" if failed else "PASS"))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
