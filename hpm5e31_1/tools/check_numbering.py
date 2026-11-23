#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/check_numbering.py —— 全书编号一致性检查（R8 / T5 验收门）

检查五类问题（每条都给 文件:行号）：
  1. 章号不连续：docs/chapters 文件名号必须 00..N-1 连续、无重号、无空号；
     H1 必须是 "# 第 N 章 ..." 且 N 与文件名号一致。
  2. 标题重号/跳号：H2 必须是 "## <章>.<节>"，节号从 1 连续；H3 必须是
     "### <章>.<节>.<小节>"，小节号在同一节内从 1 连续；H3 出现在任何 H2 之前算错误；
     标题层级跳跃给警告。
  3. § 引用悬空："§N.M[.K]" 必须能解析到已存在的章/节/小节；外部文档引用
     （RM/DS/UM/SWS/pdf/手册/应用说明/Rev/p. 等上下文）自动跳过，不误报。
  4. 图表号与文件不匹配：图/表题号必须唯一且在本章内连续；正文 "图 N-M"/"表 N-M"
     必须能对上目标章的图/表；图片文件必须存在，文件名 fig-N-M 必须与图题号一致；
     docs/figures/<章>/ 目录号必须是真实存在的章号。
  5. 目录登记（可选，--no-toc 关闭）：docs/00-目录.md 的章号/文件名必须与磁盘一致。

用法：
  python3 tools/check_numbering.py                 # 全量检查（错误 -> 退出码 1）
  python3 tools/check_numbering.py --file docs/chapters/49-mcal-mem-crc.md
  python3 tools/check_numbering.py --json
  python3 tools/check_numbering.py --strict        # 警告也算失败
  python3 tools/check_numbering.py --quiet         # 只打印错误与汇总

退出码：0 通过；1 有问题；2 用法错误。
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
CH_DIR = DOCS / "chapters"
FIG_DIR = DOCS / "figures"
TOC = DOCS / "00-目录.md"

TICK = chr(96)
FENCE = re.compile(r"^\s*(" + TICK + r"{3,})")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*$")

RE_H1 = re.compile(r"^#\s*第\s*(\d{1,2})\s*章\s*\S")
RE_H2 = re.compile(r"^##\s*(\d{1,2})\.(\d{1,2})\s+\S")
RE_H3 = re.compile(r"^###\s*(\d{1,2})\.(\d{1,2})\.(\d{1,2})\s+\S")

RE_SEC = re.compile(r"§\s*(\d{1,2})(?:\.(\d{1,2}))?(?:\.(\d{1,2}))?")
RE_CH_HINT = re.compile(r"第\s*(\d{1,2})\s*章|本章")
RE_FIGCAP = re.compile(r"!\[([^\]]*?)\]\(([^)\s]+)")
RE_FIGNO_IN_ALT = re.compile(r"图\s*(\d{1,2})-(\d{1,2})")
RE_FIGNUM = re.compile(r"图\s*(\d{1,2})-(\d{1,2})")
RE_TABNUM = re.compile(r"表\s*(\d{1,2})-(\d{1,2})")
RE_IMG = re.compile(r"!\[[^\]]*\]\(([^)\s]+)")

# 可疑标记 / 占位符残留（R7 曾留下 @BT@ 反引号占位符；R8 的 gsusb 一度用 %...% 包代码）
RE_TOKEN_AT = re.compile(r"@[A-Z]{2,6}@|@@[A-Z]{2,6}@@")
RE_TOKEN_DUNDER = re.compile(
    r"(?<![A-Za-z0-9_])_{2,}(?!(?:ENABLE|DISABLE|LWIP|CONFIG|USE|HPM|PACKED|WEAK|INLINE|ASM|attribute)__?)"
    r"[A-Z][A-Z0-9_]{2,}_{1,}(?![A-Za-z0-9_])")


def find_suspicious(line, in_fence=False):
    """返回 [(类型, 命中文本)]。只报"看起来是占位符/包裹代码"的，不误报 87.5% / C 取模 / printf 格式串。"""
    out = []
    for m in RE_TOKEN_AT.finditer(line):
        out.append(("占位符 token", m.group(0)))
    for m in RE_TOKEN_DUNDER.finditer(line):
        out.append(("双下划线占位", m.group(0)))
    if in_fence:
        return out
    # 行内代码里的 % 是 printf/取模，不算占位符
    stripped = re.sub(chr(96) + r"[^" + chr(96) + r"\n]*" + chr(96), lambda mm: " " * len(mm.group(0)), line)
    for m in re.finditer(r"%([^%\n]{3,80})%", stripped):
        c = m.group(1)
        if "|" in c:
            continue
        bad = False
        for ch in c:
            o = ord(ch)
            if 0x4E00 <= o <= 0x9FFF or 0x3000 <= o <= 0x303F or 0xFF00 <= o <= 0xFFEF:
                bad = True
                break
        if bad:
            continue
        ascii_letters = sum(1 for ch in c if ch.isascii() and ch.isalpha())
        if ascii_letters >= 2 and ((" " in c) or any(x in c for x in "/._-=")):
            out.append(("百分号包裹", m.group(0)))
    return out


EXTERNAL = re.compile(
    r"(?<![A-Za-z])(RM|DS|UM|SWS|ASAM|ISO|CiA|M_CAN)(?![A-Za-z])|pdf|PDF|手册|应用说明|勘误|Errata|"
    r"(?<![A-Za-z])Rev|(?<![0-9A-Za-z])p\.|契约|开发约定|AUTOSAR|oasis|OASIS|DesignWare|databook|抽取|"
    r"Bosch|User Manual|Specification|Document ID")


class Result:
    def __init__(self):
        self.errors = []
        self.warnings = []
        self.infos = []
        self.stats = []

    def err(self, path, line, msg):
        if ALLOW_RULES and allowed(path, line, msg):
            return
        self.errors.append((str(path), line, msg))

    def warn(self, path, line, msg):
        if ALLOW_RULES and allowed(path, line, msg):
            return
        self.warnings.append((str(path), line, msg))

    def info(self, path, line, msg):
        self.infos.append((str(path), line, msg))


ALLOW_FILE = ROOT / "tools" / "numbering_allow.txt"
ALLOW_RULES = []


def load_allow():
    global ALLOW_RULES
    ALLOW_RULES = []
    if not ALLOW_FILE.exists():
        return
    for raw in read(ALLOW_FILE).splitlines():
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        try:
            ALLOW_RULES.append(re.compile(s))
        except re.error:
            pass


def allowed(rel, line, msg):
    key = "%s:%d: %s" % (rel, line, msg)
    return any(r.search(key) for r in ALLOW_RULES)


def read(p):
    return p.read_text(encoding="utf-8")


def iter_body(lines):
    """逐行产出 (行号, 文本, 是否在代码块内)。"""
    in_fence = False
    for i, line in enumerate(lines, 1):
        if FENCE.match(line):
            in_fence = not in_fence
            yield i, line, True
            continue
        yield i, line, in_fence


def parse_chapters(res):
    """解析全部章节：章号、标题、节/小节编号、图/表号、图片引用。"""
    chapters = {}
    files = sorted(CH_DIR.glob("*.md"))
    for p in files:
        m = re.match(r"^(\d{2})-", p.name)
        if not m:
            res.err(p.relative_to(ROOT), 0, "文件名不是 NN-短横线英文名.md 形式")
            continue
        no = int(m.group(1))
        if no in chapters:
            res.err(p.relative_to(ROOT), 0, "章号重复：%02d（另一个文件 %s）" % (no, chapters[no]["path"].name))
            continue
        chapters[no] = dict(no=no, path=p, lines=read(p).splitlines(), secs={}, subs={},
                            fig_caps={}, fig_cap_lines={}, tab_caps={}, tab_cap_lines={},
                            fig_mentions=set(), tab_mentions=set(), h2=0, h3=0)
    nums = sorted(chapters)
    for idx, no in enumerate(nums):
        if no != idx:
            res.err(chapters[no]["path"].relative_to(ROOT), 1,
                    "章号不连续：期望 %02d，实际 %02d（缺失 %02d）" % (idx, no, idx))
            break
    return chapters


def check_headings(ch, res):
    rel = ch["path"].relative_to(ROOT)
    lines = ch["lines"]
    in_fence = False
    h1_seen = 0
    sec = 0
    sub = 0
    cur_sec = None
    prev_lvl = 0
    for i, line, inf in iter_body(lines):
        if inf:
            continue
        m = HEADING.match(line)
        if not m:
            continue
        lvl = len(m.group(1))
        t = m.group(2)
        if prev_lvl and lvl > prev_lvl + 1:
            res.warn(rel, i, "标题层级跳跃：H%d -> H%d（%s）" % (prev_lvl, lvl, t[:24]))
        prev_lvl = lvl
        if lvl == 1:
            h1_seen += 1
            mh = re.match(r"^第\s*(\d{1,2})\s*章\s*\S", t)
            if not mh:
                res.err(rel, i, "一级标题不是 '第 N 章 ...' 形式：%r" % t[:40])
            elif int(mh.group(1)) != ch["no"]:
                res.err(rel, i, "一级标题章号 %s 与文件名章号 %02d 不一致" % (mh.group(1), ch["no"]))
        elif lvl == 2:
            mh = RE_H2.match(line)
            if not mh:
                res.err(rel, i, "二级标题缺少 '<章>.<节> ' 编号：%r" % t[:40])
                continue
            c_no, s_no = int(mh.group(1)), int(mh.group(2))
            if c_no != ch["no"]:
                res.err(rel, i, "二级标题章号 %d 与文件名章号 %02d 不一致" % (c_no, ch["no"]))
            sec += 1
            sub = 0
            cur_sec = s_no
            if s_no != sec:
                if s_no in ch["secs"]:
                    res.err(rel, i, "二级标题重号 %d.%d" % (c_no, s_no))
                else:
                    res.err(rel, i, "二级标题跳号：期望 %d.%d，实际 %d.%d（%s）"
                            % (ch["no"], sec, c_no, s_no, t[:24]))
            ch["secs"].setdefault(s_no, i)
            ch["h2"] += 1
        elif lvl == 3:
            mh = RE_H3.match(line)
            if not mh:
                res.err(rel, i, "三级标题缺少 '<章>.<节>.<小节> ' 编号：%r" % t[:40])
                continue
            c_no, s_no, ss_no = int(mh.group(1)), int(mh.group(2)), int(mh.group(3))
            if c_no != ch["no"]:
                res.err(rel, i, "三级标题章号 %d 与文件名章号 %02d 不一致" % (c_no, ch["no"]))
            if cur_sec is None:
                res.err(rel, i, "三级标题出现在任何二级标题之前：%r" % t[:40])
            elif s_no != cur_sec:
                res.err(rel, i, "三级标题节号 %d 与所在节 %d 不一致" % (s_no, cur_sec))
            sub += 1
            if ss_no != sub:
                if (s_no, ss_no) in ch["subs"]:
                    res.err(rel, i, "三级标题重号 %d.%d.%d" % (c_no, s_no, ss_no))
                else:
                    res.err(rel, i, "三级标题跳号：期望 %d.%d.%d，实际 %d.%d.%d（%s）"
                            % (ch["no"], s_no, sub, c_no, s_no, ss_no, t[:24]))
            ch["subs"].setdefault((s_no, ss_no), i)
            ch["h3"] += 1
    if h1_seen == 0:
        res.err(rel, 1, "缺少一级标题 '# 第 N 章 ...'")
    elif h1_seen > 1:
        res.warn(rel, 1, "出现 %d 个一级标题（应只有 1 个）" % h1_seen)


def scan_figs_tabs(ch, res):
    """收集图/表题号与正文提及（题号用于唯一性/连续性，提及用于引用解析）。"""
    rel = ch["path"].relative_to(ROOT)
    lines = ch["lines"]
    in_fence = False
    for i, line, inf in iter_body(lines):
        if inf:
            continue
        for m in RE_FIGCAP.finditer(line):
            alt, target = m.group(1), m.group(2)
            fno = RE_FIGNO_IN_ALT.search(alt)
            if fno:
                n, k = int(fno.group(1)), int(fno.group(2))
                nxt = alt[fno.end():fno.end() + 1]
                if nxt.isascii() and nxt.isalpha():
                    continue  # 图 50-7b 这类字母变体，不参与编号唯一性/连续性
                if n == ch["no"]:
                    if k in ch["fig_caps"]:
                        res.err(rel, i, "图题号重复：图 %d-%d（首次在第 %d 行）" % (n, k, ch["fig_caps"][k]))
                    else:
                        ch["fig_caps"][k] = i
        for m in RE_FIGNUM.finditer(line):
            if int(m.group(1)) == ch["no"]:
                ch["fig_mentions"].add(int(m.group(2)))
        for m in RE_TABNUM.finditer(line):
            if int(m.group(1)) == ch["no"]:
                ch["tab_mentions"].add(int(m.group(2)))
        # 表题：行首 "表 N-M ..." 且下一非空行是表格；只认"强题注"（无句号、只含一个表号）
        mt = re.match(r"^(\*\*)?表\s*(\d{1,2})-(\d{1,2})", line.strip())
        if mt and int(mt.group(2)) == ch["no"]:
            j = i
            nxt = ""
            while j < len(lines):
                cand = lines[j].strip()
                if cand:
                    nxt = cand
                    break
                j += 1
            strong = ("。" not in line
                      and len(re.findall(r"表\s*\d{1,2}-\d{1,2}", line)) == 1)
            after = line.strip()[mt.end():mt.end() + 1]
            if after.isascii() and after.isalpha():
                continue  # 表 25-4b / 表 39-7b 这类字母变体
            if (nxt.startswith("|") or nxt.startswith("**表")) and strong:
                k = int(mt.group(3))
                if k in ch["tab_caps"]:
                    res.err(rel, i, "表题号重复：表 %d-%d（首次在第 %d 行）" % (ch["no"], k, ch["tab_caps"][k]))
                else:
                    ch["tab_caps"][k] = i
        mt2 = re.match(r"^#{2,6}\s*表\s*(\d{1,2})-(\d{1,2})", line)
        if mt2 and int(mt2.group(1)) == ch["no"]:
            k = int(mt2.group(2))
            if k not in ch["tab_caps"]:
                ch["tab_caps"][k] = i


def check_number_continuity(ch, res):
    """题注号连续性：只作为 INFO 输出（很多表没有题注，属内容问题，不算门禁失败）。"""
    rel = ch["path"].relative_to(ROOT)
    for kind, caps, mentions, label in (
            ("fig", ch["fig_caps"], ch["fig_mentions"], "图"),
            ("tab", ch["tab_caps"], ch["tab_mentions"], "表")):
        if not caps:
            continue
        mx = max(caps)
        missing = [k for k in range(1, mx + 1) if k not in caps]
        if missing:
            res.info(rel, 0, "%s题注号不连续：缺 %s（题注 %d 个、正文提及 %d 个、最大 %d）"
                     % (label, missing[:10], len(caps), len(mentions), mx))


def resolve_sec(chapters, ch, a, bs, cs, pre):
    """与 renumber.py 同口径：返回 (目标章, 节, 小节) 或 None。"""
    hint = None
    for m in RE_CH_HINT.finditer(pre):
        hint = int(m.group(1)) if m.group(1) else ch["no"]
    if a == ch["no"]:
        return a, bs, cs
    if hint is not None and hint in chapters and hint != a:
        return hint, a, bs
    if hint is not None and hint == a and a in chapters:
        return a, bs, cs
    if a in chapters:
        return a, bs, cs
    return None


def check_section_refs(chapters, ch, res):
    rel = ch["path"].relative_to(ROOT)
    lines = ch["lines"]
    in_fence = False
    prev = ""
    for i, line, inf in iter_body(lines):
        if inf:
            continue
        ctx = prev[-40:] + " " + line
        prev = line
        for m in RE_SEC.finditer(line):
            raw = m.group(0)
            a = int(m.group(1))
            bs = int(m.group(2)) if m.group(2) else None
            cs = int(m.group(3)) if m.group(3) else None
            left = line[max(0, m.start() - 10):m.start()]
            right = line[m.end():m.end() + 12]
            if EXTERNAL.search(ctx) or re.match(r"^\s*[（(]?\s*(p\.|p\s*\d)", right):
                continue
            if EXTERNAL.search(left):
                continue
            got = resolve_sec(chapters, ch, a, bs, cs, line[max(0, m.start() - 20):m.start()])
            if got is None:
                res.err(rel, i, "§ 引用指向不存在的章：%s" % raw)
                continue
            T, sec, sub = got
            if T not in chapters:
                res.err(rel, i, "§ 引用指向不存在的章：%s" % raw)
                continue
            tgt = chapters[T]
            if sec is None:
                res.warn(rel, i, "§ 引用缺节号：%s" % raw)
                continue
            if sec not in tgt["secs"]:
                res.err(rel, i, "§ 引用悬空：%s（第 %d 章没有节 %d）" % (raw, T, sec))
                continue
            if sub is not None and (sec, sub) not in tgt["subs"]:
                res.err(rel, i, "§ 小节引用悬空：%s（第 %d 章没有 %d.%d）" % (raw, T, sec, sub))


def check_figtab_refs(chapters, ch, res):
    rel = ch["path"].relative_to(ROOT)
    lines = ch["lines"]
    in_fence = False
    for i, line, inf in iter_body(lines):
        if inf:
            continue
        for m in RE_FIGNUM.finditer(line):
            n, k = int(m.group(1)), int(m.group(2))
            tgt = chapters.get(n)
            if tgt is None:
                if not EXTERNAL.search(line):
                    res.err(rel, i, "图号引用指向不存在的章：图 %d-%d" % (n, k))
                continue
            if k not in tgt["fig_mentions"] and k not in tgt["fig_caps"]:
                res.err(rel, i, "图号悬空：图 %d-%d（第 %d 章没有这张图）" % (n, k, n))
        for m in RE_TABNUM.finditer(line):
            n, k = int(m.group(1)), int(m.group(2))
            tgt = chapters.get(n)
            if tgt is None:
                if not EXTERNAL.search(line):
                    res.err(rel, i, "表号引用指向不存在的章：表 %d-%d" % (n, k))
                continue
            if k not in tgt["tab_mentions"] and k not in tgt["tab_caps"]:
                res.err(rel, i, "表号悬空：表 %d-%d（第 %d 章没有这张表）" % (n, k, n))


def check_images(chapters, ch, res):
    rel = ch["path"].relative_to(ROOT)
    lines = ch["lines"]
    in_fence = False
    for i, line, inf in iter_body(lines):
        if inf:
            continue
        for m in RE_IMG.finditer(line):
            target = m.group(1).split("#")[0]
            if target.startswith(("http", "/", "data:")):
                continue
            cand = (DOCS / target) if (DOCS / target).exists() else (ch["path"].parent / target)
            if not cand.exists():
                res.err(rel, i, "图片文件不存在：%s" % target)
                continue
            fn = re.search(r"fig-(\d{1,2})-(\d{1,2})", cand.name)
            if fn:
                n, k = int(fn.group(1)), int(fn.group(2))
                if n != ch["no"]:
                    res.err(rel, i, "图片文件名章号 %02d 与本章 %02d 不一致：%s" % (n, ch["no"], cand.name))
                cap = None
                mm = re.search(r"!\[([^\]]*)\]", line)
                if mm:
                    fno = RE_FIGNO_IN_ALT.search(mm.group(1))
                    if fno and (int(fno.group(1)), int(fno.group(2))) != (n, k):
                        res.err(rel, i, "图题号 图 %s-%s 与文件名 %s 不一致"
                                % (fno.group(1), fno.group(2), cand.name))


def check_tokens(ch, res):
    """残留占位符 / 不合规标记（@BT@、__XXX__、%code% 等）——apply 后的门禁。"""
    rel = ch["path"].relative_to(ROOT)
    for i, line, inf in iter_body(ch["lines"]):
        if inf:
            continue
        for name, tok in find_suspicious(line, inf):
            res.err(rel, i, "发现可疑标记（%s）：%r" % (name, tok[:50]))


def check_figure_dirs(chapters, res):
    if not FIG_DIR.is_dir():
        res.err("docs/figures", 0, "缺少 docs/figures 目录")
        return
    for d in sorted(FIG_DIR.iterdir()):
        if not d.is_dir() or d.name == "_common":
            continue
        m = re.match(r"^(\d{2})(-.+)?$", d.name)
        if not m:
            res.warn("docs/figures/" + d.name, 0, "图目录名不是 '<章号>[-后缀]' 形式")
            continue
        no = int(m.group(1))
        if no not in chapters:
            res.err("docs/figures/" + d.name, 0, "图目录章号 %02d 没有对应章节" % no)
            continue
        for f in sorted(d.glob("*.png")):
            fm = re.match(r"^fig-(\d{2})-(\d{1,2})-", f.name)
            if not fm:
                res.warn("docs/figures/%s/%s" % (d.name, f.name), 0, "图文件名不是 fig-N-M-*.png 形式")
                continue
            fn_no, k = int(fm.group(1)), int(fm.group(2))
            if fn_no != no:
                res.err("docs/figures/%s/%s" % (d.name, f.name), 0,
                        "图文件名章号 %02d 与目录章号 %02d 不一致" % (fn_no, no))
            if k not in chapters[no]["fig_mentions"]:
                res.warn("docs/figures/%s/%s" % (d.name, f.name), 0,
                         "图文件 fig-%02d-%d 在正文里没有出现（孤儿图）" % (fn_no, k))


def check_toc(chapters, res):
    if not TOC.exists():
        res.warn("docs/00-目录.md", 0, "目录文件不存在")
        return
    rows = []
    for i, line in enumerate(read(TOC).splitlines(), 1):
        m = re.match(r"^\|\s*(\d{2})\s*\|\s*([^|]+?)\s*\|\s*([0-9A-Za-z_.\-]+\.md)\s*\|", line)
        if m:
            rows.append((i, int(m.group(1)), m.group(2).strip(), m.group(3).strip()))
    nums = [r[1] for r in rows]
    for idx, no in enumerate(sorted(nums)):
        if idx not in nums:
            res.err("docs/00-目录.md", 0, "目录章号不连续：缺 %02d" % idx)
            break
    seen = set()
    for ln, no, title, fn in rows:
        if no in seen:
            res.err("docs/00-目录.md", ln, "目录章号重复：%02d" % no)
        seen.add(no)
        if fn not in [c["path"].name for c in chapters.values()]:
            res.err("docs/00-目录.md", ln, "目录登记的文件不存在：%s" % fn)
        elif int(fn[:2]) != no:
            res.err("docs/00-目录.md", ln, "目录章号 %02d 与文件名 %s 不一致" % (no, fn))
        elif no in chapters:
            h1 = next((l for l in chapters[no]["lines"] if l.startswith("# ")), "")
            t = re.sub(r"^#\s*第\s*\d{1,2}\s*章\s*", "", h1).strip()
            if t and title.strip() and title.strip() != t:
                res.warn("docs/00-目录.md", ln, "目录标题与章标题不一致：%r vs %r" % (title.strip(), t))


def main(argv=None):
    ap = argparse.ArgumentParser(description="全书编号一致性检查（R8/T5 验收门）")
    ap.add_argument("--file", "-f", action="append", default=[], help="只检查指定文件（可多次）")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--strict", action="store_true", help="警告也算失败")
    ap.add_argument("--quiet", "-q", action="store_true", help="只打印错误与汇总")
    ap.add_argument("--no-toc", action="store_true", help="不检查 00-目录.md")
    ap.add_argument("--sections-only", action="store_true", help="只检查章号与标题编号")
    ap.add_argument("--no-tokens", action="store_true", help="不检查占位符/可疑标记")
    args = ap.parse_args(argv)

    load_allow()
    res = Result()
    chapters = parse_chapters(res)
    if not chapters:
        sys.stderr.write("ERROR: docs/chapters 下没有 NN-*.md\n")
        return 2

    only = None
    if args.file:
        only = []
        for f in args.file:
            p = Path(f)
            if not p.is_absolute():
                p = (ROOT / f) if (ROOT / f).exists() else (Path.cwd() / f)
            if not p.exists():
                sys.stderr.write("ERROR: 文件不存在: %s\n" % f)
                return 2
            only.append(p.resolve())

    for no in sorted(chapters):
        ch = chapters[no]
        check_headings(ch, res)
        scan_figs_tabs(ch, res)
        if not args.no_tokens:
            check_tokens(ch, res)

    for no in sorted(chapters):
        ch = chapters[no]
        if only is not None and ch["path"].resolve() not in only:
            continue
        check_number_continuity(ch, res)
        if not args.sections_only:
            check_section_refs(chapters, ch, res)
            check_figtab_refs(chapters, ch, res)
            check_images(chapters, ch, res)

    if not args.sections_only:
        check_figure_dirs(chapters, res)
        if not args.no_toc:
            check_toc(chapters, res)

    stats = []
    for no in sorted(chapters):
        ch = chapters[no]
        stats.append(dict(chapter=no, file=ch["path"].name, h2=ch["h2"], h3=ch["h3"],
                          secs=len(ch["secs"]), subs=len(ch["subs"]),
                          figs=len(ch["fig_caps"]), tabs=len(ch["tab_caps"])))

    failed = bool(res.errors) or (args.strict and bool(res.warnings))
    if args.json:
        print(json.dumps(dict(ok=not failed, stats=stats,
                              errors=[dict(file=a, line=b, message=c) for a, b, c in res.errors],
                              warnings=[dict(file=a, line=b, message=c) for a, b, c in res.warnings]),
                         ensure_ascii=False, indent=2))
    else:
        if not args.quiet:
            print("== 章节编号统计 ==")
            for s in stats:
                print("  %02d %-34s H2 %2d | H3 %2d | 节 %2d | 小节 %3d | 图 %2d | 表 %2d"
                      % (s["chapter"], s["file"][:34], s["h2"], s["h3"], s["secs"],
                         s["subs"], s["figs"], s["tabs"]))
        for f, ln, msg in res.errors:
            print("ERROR %s:%s %s" % (f, ln, msg))
        if not args.quiet:
            for f, ln, msg in res.warnings:
                print("WARN  %s:%s %s" % (f, ln, msg))
            for f, ln, msg in res.infos:
                print("INFO  %s:%s %s" % (f, ln, msg))
        print("\n结果：%d 章，%d 错误，%d 警告 -> %s"
              % (len(chapters), len(res.errors), len(res.warnings),
                 "FAIL" if failed else "PASS"))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())

