#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/renumber.py —— 全书章节/小节编号统一 + 交叉引用重映射（R8 / T5）

规则见 docs/写作规范.md 第 3 节：
  * 章：连续编号 0..51。原 29~52 顺延 -1（第 28 章空号 = R5 把附录移走后留下的空号），
    附录变第 51 章；章号以 docs/chapters/NN-*.md 的排序位置为准。
  * 节：H2 一律写成 "## <章>.<n> <标题>"，n 从 1 连续（含"文档信息""本章导览"）。
  * 小节：H3 一律写成 "### <章>.<节>.<n>"，n 从 1 连续。
  * 正文引用："§N.M[.K]"、"第 N 章"（含 第 N~M 章、第 N/M 章 区间）、"图 N-M"、"表 N-M"
    （含 图 N-1~N-2、表 N-1/N-2 简写）、图目录 docs/figures/<章>/、图文件名 fig-N-M-*.png、
    章节文件名 NN-*.md 全部重映射。
  * tools/ 下生成脚本里的章号映射同步（gen_toc.py、check_docs.py 的 REVIEW_CHAPTERS、
    gen_appendix_tables.py、build_pdf.sh、mcal_check.py、tools/figs/*、docs/generated/*）。

§ 引用的双条件判定（R8 约定，防止把 RM/DS/UM 的章号当成本书章号）：
  ① 该编号能解析到"旧标题表"里真实存在的节/小节；
  ② 左侧 10 字符内没有外部文档标记（RM/DS/UM/SWS/pdf/手册/应用说明/Rev/p.），
     且右侧不是 "(p." / "p.数字"（页码）。
  两条都满足才改写；只满足一条进"人工复核清单"；两条都不满足视为外部引用，跳过。

安全设计：
  * 默认 --dry-run，只统计 + 样例 diff；--apply 才写盘。落地前先 git tag pre-r8-renumber。
  * 幂等：若全书章号已连续且 H2/H3 已规范，判定"已执行过"，直接退出 0（--force 可强跑）。
  * tools/renumber_whitelist.txt 支持按行白名单（每行一个正则，匹配 相对路径:行号: 原文）。
  * --root <镜像根> 可在仓库的完整副本上试跑；--only 30,31 只处理指定章（验证用）。

用法：
  python3 tools/renumber.py --plan                 # 打印章号映射表
  python3 tools/renumber.py --dry-run              # 统计 + 样例 diff + 明细报告（不写盘）
  python3 tools/renumber.py --dry-run --diff 20    # 额外打印 20 段 unified diff
  python3 tools/renumber.py --apply                # 落地（正文 + 图表目录 + tools 脚本 + 重新生成目录）
  python3 tools/renumber.py --apply --only 30,31   # 只改 30、31 两章（镜像验证用）
  python3 tools/renumber.py --apply --extra        # 连仓库根 README.md 一起改

退出码：0 成功/无变化；1 有阻塞问题；2 参数错误。
"""

import argparse
import difflib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS_DIR = ROOT / "docs"
CH_DIR = DOCS_DIR / "chapters"
FIG_DIR = DOCS_DIR / "figures"
TOOLS_DIR = ROOT / "tools"
WHITELIST = TOOLS_DIR / "renumber_whitelist.txt"

TICK = chr(96)
FENCE = re.compile(r"^\s*(" + TICK + r"{3,})")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*$")

RE_H1 = re.compile(r"^第\s*\d{1,2}\s*章\s*")
RE_NUM3 = re.compile(r"^(\d{1,2})\.(\d{1,2})\.(\d{1,2})\s+(.*)$")
RE_NUM2 = re.compile(r"^(\d{1,2})\.(\d{1,2})\s+(.*)$")
RE_NUM1 = re.compile(r"^(\d{1,2})\.\s+(.*)$")

RE_CHREF = re.compile(r"第\s*(\d{1,2})(?:\s*([~～/])\s*(\d{1,2}))?\s*章")
RE_CH_HINT = re.compile(r"第\s*(\d{1,2})\s*章|本章")
RE_FIGTAB = re.compile(r"([图表])\s*((?:\d{1,2}-\d{1,2})(?:\s*[~～/、，,与和]\s*\d{1,2}-\d{1,2})*)")
RE_ITEM = re.compile(r"(\d{1,2})-(\d{1,2})")
RE_FIGDIR = re.compile(r"(figures/)(\d{2})(?=[-/])")
RE_FIGFILE = re.compile(r"(fig-)(\d{2})(?=-\d)")
RE_CHFILE = re.compile(r"(?<![0-9A-Za-z])(\d{2})-([a-z0-9][a-z0-9-]*\.md)")
RE_SEC = re.compile(r"§\s*(\d{1,2})(?:\.(\d{1,2}))?(?:\.(\d{1,2}))?")

# 外部文档标记（左邻判定）与页码（右邻判定）
EXTERNAL_LEFT = re.compile(r"(?<![A-Za-z])(RM|DS|UM|SWS|ASAM|ISO|CiA|M_CAN)(?![A-Za-z])|pdf|PDF|手册|应用说明|勘误|Errata|(?<![A-Za-z])Rev|(?<![0-9A-Za-z])p\.|契约|开发约定|AUTOSAR|oasis|OASIS|DesignWare|databook|抽取|Bosch|User Manual|Specification|Document ID")
RIGHT_PAGE = re.compile(r"^\s*[（(]?\s*(p\.|p\s*\d)")
EXTERNAL_CH = re.compile(r"(?<![A-Za-z])(RM|DS|UM|SWS)(?![A-Za-z])|pdf|PDF|手册|应用说明|(?<![A-Za-z])Rev|(?<![0-9A-Za-z])p\.")

STYLE_H2 = "h2"
STYLE_H3 = "h3"


def strip_h2num(t):
    for rx in (RE_NUM3, RE_NUM2, RE_NUM1):
        m = rx.match(t)
        if m:
            return m.groups()[-1]
    return t


def strip_h3num(t):
    for rx in (RE_NUM3, RE_NUM2):
        m = rx.match(t)
        if m:
            return m.groups()[-1]
    return t


class Chapter:
    """单章的编号计划：旧号 -> 新号、节号映射、正文重写。"""

    def __init__(self, old_no, path):
        self.old_no = old_no
        self.path = path
        self.new_no = old_no
        self.new_path = path
        self.sec_map = {}
        self.sub_map = {}
        self.sec_dup = []
        self.sub_dup = []
        self.anomalies = []
        self.h2_count = 0
        self.h3_count = 0
        self.prefixed_h2 = 0
        self.local_h2 = 0
        self.h2_rewritten = 0
        self.h3_rewritten = 0
        self.lines = []
        self.head_meta = {}
        self.bare_style = False
        self._bare_next = 1

    @property
    def chapter_prefixed(self):
        return self.prefixed_h2 > self.local_h2

    def canonical(self):
        in_fence = False
        sec = 0
        sub = 0
        h1_ok = False
        for line in self.lines:
            if FENCE.match(line):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            m = HEADING.match(line)
            if not m:
                continue
            lvl = len(m.group(1))
            t = m.group(2)
            if lvl == 1:
                if not re.match(r"^第\s*%d\s*章\s+\S" % self.old_no, t):
                    return False
                h1_ok = True
            elif lvl == 2:
                sec += 1
                sub = 0
                if not re.match(r"^%d\.%d\s+\S" % (self.old_no, sec), t):
                    return False
            elif lvl == 3:
                sub += 1
                if not re.match(r"^%d\.%d\.%d\s+\S" % (self.old_no, sec, sub), t):
                    return False
        return h1_ok


def read_text(p):
    return p.read_text(encoding="utf-8")


def write_text(p, s):
    p.write_text(s, encoding="utf-8", newline="\n")


def discover_chapters(root):
    ch = root / "docs" / "chapters"
    out = []
    for p in sorted(ch.glob("*.md")):
        m = re.match(r"^(\d{2})-", p.name)
        if m:
            out.append(Chapter(int(m.group(1)), p))
    return out


def build_map(chapters):
    cmap = {}
    for idx, c in enumerate(chapters):
        c.new_no = idx
        cmap[c.old_no] = idx
        c.new_path = c.path.with_name("%02d-%s" % (idx, c.path.name[3:]))
    return cmap


def parse_heading(ch, t, lvl, lineno):
    """解析一个标题的旧编号并登记映射，返回 (新节号, 新小节号, 去编号后的标题)。"""
    if lvl == 2:
        ch.h2_count += 1
        new_sec = ch.h2_count
        m3 = RE_NUM3.match(t)
        m2 = RE_NUM2.match(t)
        m1 = RE_NUM1.match(t)
        key = None
        title = t
        if m3:
            g1, g2, g3 = int(m3.group(1)), int(m3.group(2)), int(m3.group(3))
            ch.anomalies.append((lineno, "H2 用了三级编号 %d.%d.%d（建议降为 H3 或去掉层级）" % (g1, g2, g3), t))
            if g1 == ch.old_no:
                ch.prefixed_h2 += 1
                key = g2
            else:
                ch.local_h2 += 1
                key = g1
            title = m3.group(4)
        elif m2:
            g1, g2 = int(m2.group(1)), int(m2.group(2))
            if g1 == ch.old_no:
                ch.prefixed_h2 += 1
                key = g2
            else:
                ch.anomalies.append(
                    (lineno, "H2 编号 %d.%d 的章前缀不是本章（%d）" % (g1, g2, ch.old_no), t))
                ch.local_h2 += 1
                key = g1
            title = m2.group(3)
        elif m1:
            ch.local_h2 += 1
            key = int(m1.group(1))
            title = m1.group(2)
        else:
            mb = re.match(r"^(\d{1,2})\s+(.*)$", t)
            if ch.bare_style and mb and int(mb.group(1)) == ch._bare_next:
                ch.local_h2 += 1
                key = ch._bare_next
                ch._bare_next += 1
                title = mb.group(2)
        if key is not None:
            if key in ch.sec_map and ch.sec_map[key] != new_sec:
                ch.sec_dup.append((key, lineno))
            else:
                ch.sec_map.setdefault(key, new_sec)
        return new_sec, 0, title
    ch.h3_count += 1
    if getattr(ch, "_cur_sec", None) is None:
        ch._cur_sec = max(1, ch.h2_count)
    new_sec = ch._cur_sec
    ch._cur_sub = getattr(ch, "_cur_sub", 0) + 1
    new_sub = ch._cur_sub
    m3 = RE_NUM3.match(t)
    m2 = RE_NUM2.match(t)
    if m3:
        g1, g2, g3 = int(m3.group(1)), int(m3.group(2)), int(m3.group(3))
        title = m3.group(4)
        if g1 == ch.old_no:
            key = (g2, g3)
        else:
            ch.anomalies.append((lineno, "H3 编号 %d.%d.%d 的章前缀不是本章（%d）" % (g1, g2, g3, ch.old_no), t))
            key = (g1, g2)
        if key in ch.sub_map and ch.sub_map[key] != (new_sec, new_sub):
            ch.sub_dup.append((key, lineno))
        else:
            ch.sub_map.setdefault(key, (new_sec, new_sub))
        return new_sec, new_sub, title
    if m2:
        key = (int(m2.group(1)), int(m2.group(2)))
        if key in ch.sub_map and ch.sub_map[key] != (new_sec, new_sub):
            ch.sub_dup.append((key, lineno))
        else:
            ch.sub_map.setdefault(key, (new_sec, new_sub))
        return new_sec, new_sub, m2.group(3)
    return new_sec, new_sub, t


def scan_chapter(ch):
    """第一遍：解析标题，建立映射（含无点号 '## 3 标题' 风格的识别）。"""
    ch.lines = read_text(ch.path).splitlines()
    # 识别 bare 风格：H2 的编号是不带点号的 1..N 连续序列
    h2_titles = []
    in_fence = False
    for line in ch.lines:
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        m = HEADING.match(line)
        if m and len(m.group(1)) == 2:
            h2_titles.append(m.group(2))
    bare_nums = []
    for t in h2_titles:
        mb = re.match(r"^(\d{1,2})\s+\S", t)
        if mb:
            bare_nums.append(int(mb.group(1)))
    ch.bare_style = len(bare_nums) >= 4 and bare_nums == list(range(1, len(bare_nums) + 1))
    ch._bare_next = 1
    in_fence = False
    ch._cur_sec = None
    ch._cur_sub = 0
    for i, line in enumerate(ch.lines, 1):
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        m = HEADING.match(line)
        if not m:
            continue
        lvl = len(m.group(1))
        t = m.group(2)
        if lvl == 1:
            ch.head_meta[i] = (1, 0, 0, RE_H1.sub("", t).strip())
        elif lvl == 2:
            ch._cur_sub = 0
            sec, sub, title = parse_heading(ch, t, 2, i)
            ch._cur_sec = ch.h2_count
            ch.head_meta[i] = (2, sec, sub, title)
        elif lvl == 3:
            sec, sub, title = parse_heading(ch, t, 3, i)
            ch.head_meta[i] = (3, sec, sub, title)
        else:
            ch.head_meta[i] = (lvl, 0, 0, t)
    return ch


class Whitelist:
    def __init__(self, path):
        self.rules = []
        self.path = path
        if path.exists():
            for raw in read_text(path).splitlines():
                s = raw.strip()
                if not s or s.startswith("#"):
                    continue
                try:
                    self.rules.append(re.compile(s))
                except re.error as e:
                    sys.stderr.write("WARNING: 白名单正则无效，已忽略: %s (%s)\n" % (s, e))

    def match(self, rel, lineno, line):
        if not self.rules:
            return False
        key = "%s:%d: %s" % (rel, lineno, line)
        return any(r.search(key) for r in self.rules)


class Report:
    def __init__(self):
        self.changes = []   # (rel, lineno, kind, old, new)
        self.review = []    # (rel, lineno, kind, ref, reason)
        self.skips = []     # (rel, lineno, ref, reason)
        self.counts = {}
        self.renames = []
        self.regenerated = []

    def change(self, rel, lineno, kind, old, new):
        self.counts[kind] = self.counts.get(kind, 0) + 1
        if len(self.changes) < 20000:
            self.changes.append((str(rel), lineno, kind, " ".join(old.split())[:100],
                                 " ".join(new.split())[:100]))

    def need_review(self, rel, lineno, kind, ref, reason):
        self.counts["需复核:" + kind] = self.counts.get("需复核:" + kind, 0) + 1
        if len(self.review) < 5000:
            self.review.append((str(rel), lineno, kind, " ".join(ref.split())[:80], reason))

    def skip(self, rel, lineno, ref, reason):
        self.counts["跳过:" + reason] = self.counts.get("跳过:" + reason, 0) + 1
        if len(self.skips) < 8000:
            self.skips.append((str(rel), lineno, " ".join(ref.split())[:80], reason))

    def bump(self, key, n=1):
        self.counts[key] = self.counts.get(key, 0) + n


class Rewriter:
    """正文引用重映射（章节文件 / tools 脚本 / docs 散文共用）。"""

    def __init__(self, cmap, chapters, report):
        self.cmap = cmap
        self.by_old = {c.old_no: c for c in chapters}
        self.report = report
        self.wl = Whitelist(WHITELIST)
        self.figs = {}
        self.tabs = {}
        self.textfix = {}
        for relp, old, new, _why in globals().get("TEXT_FIX", []):
            self.textfix.setdefault(relp, []).append((old, new, _why))
        self.tfix_map = {relp: {old: new for old, new, _ in items}
                         for relp, items in self.textfix.items()}
        self.overrides = {}
        self.overrides_any = {}
        for relp, old, new, _reason in globals().get("OVERRIDES", []):
            self.overrides[(relp, old)] = (new, _reason)
            self.overrides_any.setdefault(old, (new, _reason))
        self._scan_captions(chapters)

    def _scan_captions(self, chapters):
        """收集各旧章的图号/表号（用于判断"引用的图/表是否存在"）。"""
        for c in chapters:
            figs = set()
            tabs = set()
            in_fence = False
            for line in c.lines:
                if FENCE.match(line):
                    in_fence = not in_fence
                    continue
                if in_fence:
                    continue
                for m in re.finditer(r"图\s*(\d{1,2})-(\d{1,2})", line):
                    if int(m.group(1)) == c.old_no:
                        figs.add(int(m.group(2)))
                for m in re.finditer(r"表\s*(\d{1,2})-(\d{1,2})", line):
                    if int(m.group(1)) == c.old_no:
                        tabs.add(int(m.group(2)))
            self.figs[c.old_no] = figs
            self.tabs[c.old_no] = tabs

    @staticmethod
    def _ext_before(line, pos, win, rx):
        """pos 之前 win 个字符内是否出现 rx（在整个 line 上匹配，避免截断破坏断言）。"""
        lo = max(0, pos - win)
        for mm in rx.finditer(line):
            if lo <= mm.start() and mm.end() <= pos:
                return True
        return False

    # ---------- 章号引用 ----------
    def chapter_refs(self, line, rel, lineno):
        def repl(m):
            raw = m.group(0)
            a = int(m.group(1))
            if a in self.cmap and self.cmap[a] == a and not (
                    m.group(2) and m.group(3) and self.cmap.get(int(m.group(3)), int(m.group(3))) != int(m.group(3))):
                return raw
            external = self._ext_before(line, m.start(), 6, EXTERNAL_CH)
            if a not in self.cmap:
                self.report.need_review(rel, lineno, "第N章", raw, "章号不在本书（可能是外部文档）")
                return raw
            if external:
                self.report.need_review(rel, lineno, "第N章", raw, "左侧 10 字符内有外部文档标记")
                return raw
            new_a = self.cmap[a]
            if m.group(2) and m.group(3):
                b = int(m.group(3))
                new_b = self.cmap.get(b, b)
                if new_a == a and new_b == b:
                    return raw
                new = "第 %d%s%d 章" % (new_a, m.group(2), new_b)
                self.report.change(rel, lineno, "第N章", raw, new)
                return new
            if new_a == a:
                return raw
            new = "第 %d 章" % new_a
            self.report.change(rel, lineno, "第N章", raw, new)
            return new
        return RE_CHREF.sub(repl, line)

    # ---------- 图/表号 ----------
    def figtab_refs(self, line, rel, lineno):
        def repl(m):
            sep = m.group(0)[len(m.group(1)):len(m.group(0)) - len(m.group(2))]
            tail = m.group(2)
            for old_ch_s, num_s in RE_ITEM.findall(tail):
                old_ch = int(old_ch_s)
                num = int(num_s)
                new_ch = self.cmap.get(old_ch, old_ch)
                if new_ch == old_ch:
                    continue
                pool = self.figs if m.group(1) == "图" else self.tabs
                if num not in pool.get(old_ch, set()):
                    if EXTERNAL_LEFT.search(line) or EXTERNAL_CH.search(line):
                        self.report.skip(rel, lineno, "%s %s-%s" % (m.group(1), old_ch_s, num_s),
                                         "外部文档的图表号")
                        continue
                    self.report.need_review(rel, lineno, m.group(1),
                                            "%s %s-%s" % (m.group(1), old_ch_s, num_s),
                                            "旧章里没有这个图/表号（按本书引用重映射章号，需确认）")
                new = "%s %d-%s" % (m.group(1), new_ch, num_s)
                old = "%s %s-%s" % (m.group(1), old_ch_s, num_s)
                self.report.change(rel, lineno, m.group(1), old, new)
                tail = tail.replace("%s-%s" % (old_ch_s, num_s), "%d-%s" % (new_ch, num_s), 1)
            return m.group(1) + sep + tail
        return RE_FIGTAB.sub(repl, line)

    # ---------- 路径与文件名 ----------
    def paths(self, line, rel, lineno):
        def figdir(m):
            n = int(m.group(2))
            if n in self.cmap and self.cmap[n] != n:
                new = m.group(1) + "%02d" % self.cmap[n]
                self.report.change(rel, lineno, "图目录", m.group(0), new)
                return new
            return m.group(0)

        def figfile(m):
            n = int(m.group(2))
            if n in self.cmap and self.cmap[n] != n:
                new = m.group(1) + "%02d" % self.cmap[n]
                self.report.change(rel, lineno, "图文件名", m.group(0), new)
                return new
            return m.group(0)

        def chfile(m):
            n = int(m.group(1))
            if n in self.cmap and self.cmap[n] != n:
                new = "%02d-%s" % (self.cmap[n], m.group(2))
                self.report.change(rel, lineno, "章节文件名", m.group(0), new)
                return new
            return m.group(0)

        line = RE_FIGDIR.sub(figdir, line)
        line = RE_FIGFILE.sub(figfile, line)
        line = RE_CHFILE.sub(chfile, line)
        return line

    @staticmethod
    def _hint(pre, old_no):
        hint = None
        for m in RE_CH_HINT.finditer(pre):
            hint = int(m.group(1)) if m.group(1) else old_no
        return hint

    def _resolve(self, ch, a, bs, cs, pre):
        """返回 (T, sec, sub) 或 None。"""
        old_no = ch.old_no if ch else None
        hint = self._hint(pre, old_no)
        if old_no is not None and a == old_no:
            return old_no, bs, cs
        if hint is not None and hint in self.by_old and hint != a:
            return hint, a, bs
        if hint is not None and hint == a and a in self.by_old:
            return a, bs, cs
        if a in self.by_old:
            if old_no is not None and ch is not None and bs is None and a in ch.sec_map and hint is None:
                return old_no, a, None
            if (old_no is not None and ch is not None and bs is None and a in ch.sec_map
                    and ch.chapter_prefixed):
                return old_no, a, None
            if old_no is not None and ch is not None and not ch.chapter_prefixed:
                if bs is None and a in ch.sec_map:
                    return old_no, a, None
                if bs is not None and ((a, bs) in ch.sub_map or a in ch.sec_map):
                    return old_no, a, bs
            return a, bs, cs
        return None

    # ---------- § 引用（双条件判定） ----------
    def section_refs(self, line, ch, rel, lineno):
        old_no = ch.old_no if ch else None
        line_manual = EXTERNAL_LEFT.search(line) is not None

        def repl(m):
            raw = m.group(0)
            a = int(m.group(1))
            bs = int(m.group(2)) if m.group(2) else None
            cs = int(m.group(3)) if m.group(3) else None
            right = line[m.end():m.end() + 12]
            external = (self._ext_before(line, m.start(), 10, EXTERNAL_LEFT)
                        or RIGHT_PAGE.match(right) is not None)
            pre = line[max(0, m.start() - 20):m.start()]
            hint = self._hint(pre, old_no)
            if not external:
                ov = self.overrides.get((rel, raw)) or self.overrides_any.get(raw)
                if ov:
                    new = ov[0]
                    self.report.change(rel, lineno, "§(override)", raw, new)
                    return new
            got = self._resolve(ch, a, bs, cs, pre)

            resolved = None
            degraded = False
            if got:
                T, sec, sub = got
                tgt = self.by_old[T]
                newT = self.cmap[T]
                if sec is not None:
                    if sub is not None:
                        g = tgt.sub_map.get((sec, sub))
                        if g:
                            resolved = "§%d.%d.%d" % (newT, g[0], g[1])
                        else:
                            s2 = tgt.sec_map.get(sec)
                            if s2 is not None:
                                resolved = "§%d.%d" % (newT, s2)
                                degraded = True
                    else:
                        s2 = tgt.sec_map.get(sec)
                        if s2 is not None:
                            resolved = "§%d.%d" % (newT, s2)

            if resolved is not None and resolved == raw and not degraded:
                return raw
            if ch is None and bs is None:
                self.report.skip(rel, lineno, raw, "非章节文件的自有节号（不是本书引用）")
                return raw
            if ch is None:
                got = self._resolve(ch, a, bs, cs, pre)
                if got:
                    T, sec, sub = got
                    tgt = self.by_old.get(T)
                    if tgt is not None and sec is not None:
                        r2 = None
                        if sub is not None:
                            g = tgt.sub_map.get((sec, sub))
                            if g:
                                r2 = "§%d.%d.%d" % (self.cmap[T], g[0], g[1])
                        if r2 is None:
                            s2 = tgt.sec_map.get(sec)
                            if s2 is not None:
                                r2 = "§%d.%d" % (self.cmap[T], s2)
                        if r2 and r2 != raw and not external:
                            self.report.change(rel, lineno, "§", raw, r2)
                            return r2
            is_self = (old_no is not None and a == old_no
                       and ch is not None and ch.chapter_prefixed)
            is_local = (ch is not None and old_no is not None and not ch.chapter_prefixed and (
                (bs is not None and (a, bs) in ch.sub_map) or (bs is None and a in ch.sec_map)))
            has_hint = hint is not None
            bookish = is_self or is_local or has_hint

            # ① 能解析到旧标题表 且 ② 上下文干净 -> 改写（仅限本书风格引用）
            if resolved is not None and not external and bookish:
                if degraded:
                    self.report.need_review(rel, lineno, "§", raw,
                                            "小节查不到，退化为节引用 %s" % resolved)
                if resolved != raw:
                    kind = "§简写" if "." not in raw else "§"
                    self.report.change(rel, lineno, kind, raw, resolved)
                return resolved
            # 带"第 N 章"提示却撞上外部标记：最需要人看一眼
            if bookish and resolved is not None and external and has_hint:
                self.report.need_review(rel, lineno, "§", raw,
                                        "带第N章提示但左/右邻有外部文档标记（未改）")
                return raw
            if external or line_manual:
                self.report.skip(rel, lineno, raw, "外部文档引用")
                return raw
            if resolved is None:
                self.report.need_review(rel, lineno, "§", raw,
                                        "上下文干净但旧节号解析不到（未改）")
                return raw
            self.report.need_review(rel, lineno, "§", raw,
                                    "跨章 § 无就近章号提示（未改，建议改写成 §<章>.<节>）")
            return raw
        return RE_SEC.sub(repl, line)

    def protect_textfix(self, line):
        """把 TEXT_FIX 的旧文本换成占位符，避免后续通用规则二次改写；restore 时还原成新文本。"""
        reps = []
        return line, reps

    def refs_line(self, line, ch, rel, lineno):
        # 授权过的逐条改写：先占位保护，避免被通用规则二次改写
        reps = []
        tf = self.tfix_map.get(rel)
        if tf:
            for old, new in tf.items():
                while old in line:
                    i = len(reps)
                    sent = "@@TX%d@@" % i
                    line = line.replace(old, sent, 1)
                    reps.append((sent, new, old))
                    self.report.change(rel, lineno, "逐条改写", old, new)
        # 先处理 §：它依赖"第 N 章"就近提示的旧章号，必须在章号改写之前
        line = self.section_refs(line, ch, rel, lineno)
        line = self.figtab_refs(line, rel, lineno)
        line = self.paths(line, rel, lineno)
        line = self.chapter_refs(line, rel, lineno)
        for sent, new, _old in reps:
            line = line.replace(sent, new)
        return line


def plan_table_fixes(ch, rel):
    """给 TABLE_FIX 里的重复题注分配"章内下一个未占用号"，返回 [(行号, 旧号, 新号)]。"""
    items = [(ln, old) for r, ln, old in TABLE_FIX if r == rel]
    if not items:
        return []
    nums = [int(m.group(1)) for m in re.finditer(r"表\s*%d-(\d{1,2})" % ch.old_no, "\n".join(ch.lines))]
    mx = max(nums) if nums else 0
    out = []
    for ln, old in sorted(items):
        mx += 1
        out.append((ln, old, mx))
    return out


def rebuild_chapter(ch, cmap, rw):
    rel = str(ch.path.relative_to(ROOT))
    tfix = plan_table_fixes(ch, rel)
    out = []
    in_fence = False
    for i, line in enumerate(ch.lines, 1):
        m = None
        if FENCE.match(line):
            in_fence = not in_fence
        elif not in_fence:
            m = HEADING.match(line)
        if m and i in ch.head_meta:
            if rw.wl.match(rel, i, line):
                out.append(line)
                continue
            lvl, secno, subno, title = ch.head_meta[i]
            title = rw.refs_line(title, ch, rel, i)
            if lvl == 1:
                new = "# 第 %d 章 %s" % (ch.new_no, title.strip())
                ch.h2_rewritten += 0
            elif lvl == 2:
                new = "## %d.%d %s" % (ch.new_no, secno, title.strip())
                ch.h2_rewritten += 1
            elif lvl == 3:
                new = "### %d.%d.%d %s" % (ch.new_no, secno, subno, title.strip())
                ch.h3_rewritten += 1
            else:
                new = line
            if new != line:
                rw.report.change(rel, i, "标题", line, new)
            out.append(new)
            continue
        if rw.wl.match(rel, i, line):
            out.append(line)
        else:
            new = rw.refs_line(line, ch, rel, i)
            new_ch_no = cmap.get(ch.old_no, ch.old_no)
            for fl, old, newno in tfix:
                oldtok = "表 %d-%d" % (new_ch_no, old)
                newtok = "表 %d-%d" % (new_ch_no, newno)
                if i == fl and oldtok in new:
                    new = new.replace(oldtok, newtok, 1)
                    rw.report.change(rel, i, "表号去重（题注）", oldtok, newtok)
                elif i > fl and oldtok in new:
                    cnt = new.count(oldtok)
                    new = new.replace(oldtok, newtok)
                    rw.report.change(rel, i, "表号去重（引用 x%d）" % cnt, oldtok, newtok)
            out.append(new)
    return out


def collect_renames(chapters, cmap, only, report):
    renames = []
    for c in chapters:
        if only and c.old_no not in only:
            continue
        if c.new_no != c.old_no:
            renames.append((c.path, c.new_path))
    figdirs = []
    figfiles = []
    if FIG_DIR.exists():
        for d in sorted(FIG_DIR.iterdir()):
            if not d.is_dir():
                continue
            m = re.match(r"^(\d{2})(-.+)?$", d.name)
            if not m:
                continue
            old_no = int(m.group(1))
            if only and old_no not in only:
                continue
            new_no = cmap.get(old_no, old_no)
            if new_no != old_no:
                figdirs.append((d, d.with_name("%02d%s" % (new_no, m.group(2) or ""))))
            for f in sorted(d.glob("*.png")):
                fm = re.match(r"^fig-(\d{2})-(\d+)(-.+)$", f.name)
                if not fm:
                    continue
                fn_no = int(fm.group(1))
                new_fn = cmap.get(fn_no, fn_no)
                if new_fn != fn_no:
                    figfiles.append((f, new_fn, fm.group(2), fm.group(3)))
    report.renames = renames
    return renames, figdirs, figfiles


TOOL_FILES = [
    "tools/gen_toc.py",
    "tools/check_docs.py",
    "tools/gen_appendix_tables.py",
    "tools/build_pdf.sh",
    "tools/mcal_check.py",
    "tools/gen_app_matrix.py",
]
EXTRA_FILES = ["README.md"]

# 显式判定：人工确认过的"本书内部引用 -> 新号"（①类）。键为 (相对路径, 旧引用)。
# 只放逐条核对过的条目；每行 (旧 -> 新) 都会出现在报告的判定清单里。
OVERRIDES = [
    # 第 48 章"本章导览"用的是"内容节序"编号（不含文档信息/本章导览/参考来源），
    # 与当前无编号 H2 对不上；按各条目实际指向的节标题逐条核对后显式映射。
    ("docs/chapters/48-mcal-can.md", "§48.1", "§47.3", "指向 概述（第 3 个 H2）"),
    ("docs/chapters/48-mcal-can.md", "§48.2", "§47.4", "指向 工作原理"),
    ("docs/chapters/48-mcal-can.md", "§48.3", "§47.5", "指向 寄存器"),
    ("docs/chapters/48-mcal-can.md", "§48.4", "§47.6", "指向 配置模型"),
    ("docs/chapters/48-mcal-can.md", "§48.5", "§47.7", "指向 API 实现与完成度"),
    ("docs/chapters/48-mcal-can.md", "§48.6", "§47.8", "指向 例程"),
    ("docs/chapters/48-mcal-can.md", "§48.7", "§47.9", "指向 实测"),
    ("docs/chapters/48-mcal-can.md", "§48.8", "§47.10", "指向 性能"),
    ("docs/chapters/48-mcal-can.md", "§48.9", "§47.11", "指向 坑"),
    ("docs/chapters/48-mcal-can.md", "§48.10", "§47.12", "指向 对比"),
    ("docs/chapters/48-mcal-can.md", "§48.11", "§47.14", "指向 待确认项与风险（参考来源插在其前）"),
]


# 表号去重（Lead 授权，R8）：(旧相对路径, 旧文件行号, 旧表号)。后出现的那张改成"章内下一个未占用号"，
# 并把该行之后同章的 "表 <章>-<旧号>" 引用同步改成新号（逐条写进报告）。
TABLE_FIX = [
    ("docs/chapters/18-adc16.md", 223, 6),
    ("docs/chapters/19-dma.md", 288, 7),
    ("docs/chapters/19-dma.md", 357, 6),
    ("docs/chapters/22-wdg-power-temp.md", 250, 7),
    ("docs/chapters/22-wdg-power-temp.md", 301, 6),
    ("docs/chapters/34-canopen.md", 424, 12),
]

# 逐条文本改写（Lead 授权，R8）：(旧相对路径, 旧文本, 新文本, 说明)。按整词替换，逐条进报告。
TEXT_FIX = [
    ("docs/chapters/29-app-bench.md", "§29.5", "§28.6", "ch29->28：§29.5 实指'上板实测'节（第 6 个 H2）"),
    ("docs/chapters/29-app-bench.md", "§29.2", "§28.4", "ch29->28：§29.2 实指'工作原理/cannelloni 字段表'（第 4 个 H2）"),
    ("docs/chapters/14-pwmv2-hrpwm.md", "§2.8", "§14.4.4", "ch14：原 §2.8 悬空，应用场景'多计数器同步+移相'对应 2.4 移相 -> 新号 §14.4.4"),
    ("components/app_can/README.md", "§29.5", "§28.4", "指第 29 章'寄存器与驱动 API 对照'（工作原理节）-> 新号 §28.4"),
    ("components/app_lwip/README.md", "§29.3", "§28.4", "指第 29 章 Bench C（工作原理节）-> 新号 §28.4"),
    ("components/app_can_gw/README.md", "§29.6", "§28.8", "指第 29 章'坑与注意事项'（第 8 个 H2）-> 新号 §28.8"),
    ("host/gs_usb/README.md", "图 40-4", "图 41-4", "R6 旧章号：gs_usb 现为第 42 章（新 41），枚举耗时对应 图 42-4 -> 图 41-4"),
    ("host/gs_usb/README.md", "图 40-5", "图 41-5", "R6 旧章号：单帧延迟/吞吐对应 图 42-5 -> 图 41-5"),
]


# 人工逐条判定（③类抽查后写入；键为 (相对路径, 旧引用)）。
# verdict/action/owner 会直接写进报告的"判定清单"。
JUDGE = {
    ("docs/chapters/14-pwmv2-hrpwm.md", "§2.8"): ("③ 待确认", "本章 §2 只有 2.1~2.7，§2.8 悬空；按实际节改写或删引用", "tp-timer"),
    ("docs/chapters/29-app-bench.md", "§29.5"): ("③ 待确认", "本书第 29 章内引用，旧编号残留；疑指'上板实测'节（新号 §28.6），需 tp-can 按实际节标题改写", "tp-can"),
    ("docs/chapters/29-app-bench.md", "§29.2"): ("③ 待确认", "本书第 29 章内引用，旧编号残留；疑指'工作原理/cannelloni 字段表'（新号 §28.4）", "tp-can"),
    ("docs/chapters/38-mqtt.md", "§4.7.1"): ("② 外部文档（保持原样）", "MQTT 3.1.1 规范 §4.7.1.2（主题通配符），不是本书节号", "tp-app3"),
    ("docs/chapters/45-mcal-core.md", "§6"): ("② 外部文档（保持原样）", "疑指 docs/MCAL开发约定.md §6（例程结构约定）；若确为本章节号请改写为 §44.x", "mcal-core"),
    ("docs/chapters/50-mcal-eth.md", "§8.4.7"): ("② 外部文档（保持原样）", "DesignWare ENET 数据手册节号（DMA 寄存器章）", "eth-core"),
    ("docs/chapters/50-mcal-eth.md", "第 12 章"): ("① 本书内部引用（无需改）", "目标第 12 章号未变（12->12），保持原样", "eth-core"),
    ("docs/chapters/49-mcal-mem-crc.md", "第 21 章"): ("② 外部文档（保持原样）", "HPM5E00 RM 第 21 章（XPI/Flash），不是本书章号", "mcal-sys"),
    ("docs/chapters/51-mcal-compliance.md", "第 07 章"): ("② 外部文档（保持原样）", "SWS 文档章号（AUTOSAR CP R25-11）", "mcal-ref"),
    ("docs/chapters/51-mcal-compliance.md", "§0"): ("② 外部文档（保持原样）", "docs/mcal/MCAL_API_TRACEABILITY.md 的 §0（判定口径），不是本书节号", "mcal-ref"),
    ("tools/figs/mcal_eth_irq.py", "图 50-8"): ("① 本书内部引用（已重映射）", "本书第 50 章图，已按章号映射为 图 49-8（新图由 eth-quant 补）", "eth-quant"),
    ("tools/figs/mcal_eth_irq.py", "图 50-9"): ("① 本书内部引用（已重映射）", "本书第 50 章图，已按章号映射为 图 49-9（新图由 eth-quant 补）", "eth-quant"),
    ("samples/mcal/crc_bench/README.md", "第 48 章"): ("① 本书内部引用（无需改）", "SWS 是同行另一句，第 48 章指本书 MCAL Can 章（新号第 47 章）", "mcal-can"),
    ("samples/can/mcan_quad_load/README.md", "第 08/09 章"): ("① 本书内部引用（无需改）", "第 8/9 章号未变（8->8、9->9）", "tp-can"),
    ("tools/figs/app39_fig2_perf.py", "第 29~40 章"): ("① 本书内部引用（无需改）", "'本手册上板实测值'指本书章节区间，区间两端 -1 后为 28~39", "Lead（工具）"),
    ("tools/figs/ch27-feature-matrix.py", "第 27 章"): ("① 本书内部引用（无需改）", "第 27 章号未变（27->27）", "Lead（工具）"),
    ("components/app_can/README.md", "§29.5"): ("③ 待确认", "本书第 29 章引用，但旧节号无法解析；需按 ch29 实际节改写为新章号 §28.x", "tp-can"),
    ("components/app_lwip/README.md", "§29.3"): ("③ 待确认", "同上：需按 ch29 实际节改写为新章号 §28.x", "tp-can"),
    ("components/app_can_gw/README.md", "§29.6"): ("③ 待确认", "同上：需按 ch29 实际节改写为新章号 §28.x", "tp-can"),
    ("host/gs_usb/README.md", "图 40-4"): ("③ 待确认", "第 40 章只有 图 40-1/40-2，图号悬空，需修 README 或补图", "tp-gs"),
    ("host/gs_usb/README.md", "图 40-5"): ("③ 待确认", "第 40 章只有 图 40-1/40-2，图号悬空，需修 README 或补图", "tp-gs"),
}


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


def scan_tokens(chapters):
    """扫描章节正文里的残留占位符/不合规标记（代码块内也扫，因为 PDF 会原样印出）。"""
    hits = []
    for c in chapters:
        in_fence = False
        for i, line in enumerate(c.lines, 1):
            if FENCE.match(line):
                in_fence = not in_fence
                continue
            for name, tok in find_suspicious(line, in_fence):
                hits.append((c.path.name, i, name, tok[:50], in_fence))
    return hits


def load_owners():
    """从 tools/gen_toc.py 的章节表读 文件名 -> 负责人，用于给"待判断"条目指人。"""
    p = ROOT / "tools" / "gen_toc.py"
    out = {}
    if not p.exists():
        return out
    for line in read_text(p).splitlines():
        m = re.search(r'"([0-9A-Za-z_.\-]+\.md)",\s*"([^"]+)"', line)
        if m:
            out[m.group(1)] = m.group(2)
    return out
README_DIRS = ["samples", "components", "host"]


def tool_glob_files(root):
    out = []
    d = root / "tools" / "figs"
    if d.is_dir():
        for pat in ("*.py", "*.dot", "*.sh", "*.md", "*.json"):
            for p in sorted(d.glob(pat)):
                out.append(p)
    return out


def doc_prose_files(root):
    out = []
    for name in ["写作规范.md", "MCAL开发约定.md", "应用层开发约定.md", "gs_usb开发约定.md"]:
        p = root / "docs" / name
        if p.exists():
            out.append(p)
    p = root / "docs" / "mcal"
    if p.is_dir():
        for name in ["MCAL_API_TRACEABILITY.md", "gen_traceability.py",
                     "assemble_ch49.py", "traceability_manual.json"]:
            q = p / name
            if q.exists():
                out.append(q)
    g = root / "docs" / "generated"
    if g.is_dir():
        for q in sorted(g.glob("*.md")):
            out.append(q)
    return out


def readme_files(root):
    out = []
    for d in README_DIRS:
        p = root / d
        if not p.is_dir():
            continue
        for q in p.rglob("README.md"):
            out.append(q)
    return out


SPECIAL_GEN_TOC = re.compile(r'^(\s*\(\s*")(\d{2})(",\s*"[^"]*",\s*")(\d{2})-([A-Za-z0-9_.-]+\.md)(")')


def rewrite_tool_file(p, cmap, rw, report):
    rel = str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p)
    text = read_text(p)
    if p.name == "gen_toc.py":
        out = []
        for i, line in enumerate(text.splitlines(), 1):
            m = SPECIAL_GEN_TOC.match(line)
            if m:
                num = int(m.group(2))
                filenum = int(m.group(4))
                new_num = cmap.get(num, num)
                new_file = cmap.get(filenum, filenum)
                if new_num != num or new_file != filenum:
                    old = line
                    line = "%s%02d%s%02d-%s%s" % (m.group(1), new_num, m.group(3),
                                                  new_file, m.group(5), m.group(6))
                    report.change(rel, i, "gen_toc 章表", old.strip(), line.strip())
            out.append(_rewrite_generic_line(rw, line, i, rel, None))
        new_text = "\n".join(out) + ("\n" if text.endswith("\n") else "")
    elif p.name == "check_docs.py":
        def repl(m):
            nums = re.findall(r'"(\d{2})"', m.group(0))
            items = ['"%02d"' % cmap.get(int(n), int(n)) for n in nums]
            return "REVIEW_CHAPTERS = {" + ", ".join(items) + "}"
        new_text = re.sub(r"REVIEW_CHAPTERS = \{[^}]*\}", repl, text)
        if new_text != text:
            report.change(rel, 0, "check_docs REVIEW_CHAPTERS",
                          re.search(r"REVIEW_CHAPTERS = \{[^}]*\}", text).group(0),
                          re.search(r"REVIEW_CHAPTERS = \{[^}]*\}", new_text).group(0))
        new_text = _rewrite_generic_text(rw, new_text, rel, None)
    else:
        new_text = _rewrite_generic_text(rw, text, rel, None)
        if p.suffix == ".py" and p.parent.name == "figs" and "tools" in p.parts:
            new_text = rewrite_figs_numbers(p, new_text, rw.cmap, report, rel)
    return new_text


RE_SAVE_CH = re.compile(r"(\bsave\(\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*)(\d{1,2})(\s*,)")
RE_CONST_CH = re.compile(r"^(\s*(?:CH|CHAPTER|CH_NO|CH_NUM|CHAP|CHAPTER_NO|CHAP_NO)\s*=\s*)(\d{1,2})(\s*(?:#.*)?)$")
RE_CONST_CH_STR = re.compile(r"^(\s*(?:CH|CHAPTER|CH_NO|CH_NUM|CHAP|CHAPTER_NO|CHAP_NO)\s*=\s*[\"'])(\d{1,2})([\"'])")
RE_QUOTED_CHDIR = re.compile(r"([\"'])(\d{2})-([A-Za-z][A-Za-z0-9_-]*)([\"'])")


def rewrite_figs_numbers(p, text, cmap, report, rel):
    """tools/figs/*.py 里 save(fig, NN, ...) 的章号实参与 CH=NN 常量也要跟着改。"""
    out = []
    for i, line in enumerate(text.splitlines(), 1):
        new = line

        def fix_save(m):
            n = int(m.group(2))
            if n in cmap and cmap[n] != n:
                newv = "%s%02d%s" % (m.group(1), cmap[n], m.group(3))
                report.change(rel, i, "figs 章号实参", m.group(0), newv)
                return newv
            return m.group(0)

        def fix_const(m):
            n = int(m.group(2))
            if n in cmap and cmap[n] != n:
                newv = "%s%02d%s" % (m.group(1), cmap[n], m.group(3))
                report.change(rel, i, "figs 章号常量", m.group(0), newv)
                return newv
            return m.group(0)

        def fix_quoted_dir(m):
            n = int(m.group(2))
            if n in cmap and cmap[n] != n:
                newv = "%s%02d-%s%s" % (m.group(1), cmap[n], m.group(3), m.group(4))
                report.change(rel, i, "figs 章号字符串", m.group(0), newv)
                return newv
            return m.group(0)

        new = RE_SAVE_CH.sub(fix_save, new)
        new = RE_CONST_CH.sub(fix_const, new)
        new = RE_CONST_CH_STR.sub(fix_const, new)
        new = RE_QUOTED_CHDIR.sub(fix_quoted_dir, new)
        out.append(new)
    return "\n".join(out) + ("\n" if text.endswith("\n") else "")


def _rewrite_generic_line(rw, line, lineno, rel, ch):
    if rw.wl.match(rel, lineno, line):
        return line
    return rw.refs_line(line, ch, rel, lineno)


def _rewrite_generic_text(rw, text, rel, ch):
    out = "\n".join(_rewrite_generic_line(rw, l, i, rel, ch)
                    for i, l in enumerate(text.splitlines(), 1))
    return out + ("\n" if text.endswith("\n") else "")


def scan_unmanaged(root, cmap, handled=None):
    pat = re.compile(r"第\s*(\d{1,2})\s*章|[图表]\s*(\d{1,2})-\d{1,2}")
    hits = {}
    handled = handled or set()
    skip = ("docs/chapters/", "docs/figures/", "tools/", "docs/mcal/",
            ".git/", "docs/build/", "logs/", "build/", "samples/", "components/", "host/")
    for p in sorted(root.rglob("*.md")):
        rel = p.relative_to(root).as_posix()
        if rel in handled:
            continue
        if any(rel.startswith(s) for s in skip):
            continue
        if rel.startswith("docs/ROUND") or rel.startswith("docs/drafts"):
            continue
        try:
            text = read_text(p)
        except (UnicodeDecodeError, OSError):
            continue
        nums = set()
        for m in pat.finditer(text):
            for g in m.groups():
                if g:
                    n = int(g)
                    if n in cmap and cmap[n] != n:
                        nums.add(n)
        if nums:
            hits[rel] = sorted(nums)
    return hits


def apply_renames(renames, figdirs, figfiles, cmap):
    for src, dst in renames:
        if dst.exists():
            raise SystemExit("ERROR: 目标章节文件已存在: %s" % dst)
        src.rename(dst)
    # 升序改图目录名（旧 29->28, 30->29 ... 目标已被腾空）
    for src, dst in sorted(figdirs, key=lambda x: int(x[0].name[:2])):
        src.rename(dst)
    # 目录改名后再改目录内文件名（用旧目录名反查新目录名）
    for src, new_no, num, suf in figfiles:
        old_dir = src.parent.name
        nm = re.match(r"^(\d{2})(-.+)?$", old_dir)
        new_dir = old_dir
        if nm:
            new_dir = "%02d%s" % (cmap.get(int(nm.group(1)), int(nm.group(1))), nm.group(2) or "")
        base = FIG_DIR / new_dir
        sp = base / src.name
        dp = base / ("fig-%02d-%s%s" % (new_no, num, suf))
        if dp.exists():
            raise SystemExit("ERROR: 目标图文件已存在: %s" % dp)
        if not sp.exists():
            raise SystemExit("ERROR: 源图文件不存在: %s" % sp)
        sp.rename(dp)


def baseline_numbers():
    """改前基线：直接跑一次 tools/check_numbering.py -q，取最后一行统计。"""
    p = ROOT / "tools" / "check_numbering.py"
    if not p.exists():
        return "（未找到 tools/check_numbering.py）"
    try:
        r = subprocess.run([sys.executable, str(p), "-q"], cwd=str(ROOT),
                           capture_output=True, text=True, timeout=300)
        out = [l for l in (r.stdout or "").strip().splitlines() if l.strip()]
        return out[-1] if out else "?"
    except Exception as e:  # noqa: BLE001
        return "（基线检查失败：%s）" % e


def judge(rel, ln, kind, ref, reason, line, owners):
    """把复核条目判成 ① 本书内部引用 / ② 外部文档 / ③ 待确认，并给出处理建议与责任人。"""
    ov = JUDGE.get((rel, ref))
    if ov:
        return ov
    ext = EXTERNAL_LEFT.search(line) is not None or EXTERNAL_CH.search(line) is not None
    owner = owners.get(Path(rel).name, "")
    if not owner:
        if rel.endswith("README.md"):
            owner = "对应模块负责人"
        elif rel.startswith("tools/") or rel.startswith("docs/"):
            owner = "Lead（工具/约定文档）"
        else:
            owner = "Lead"
    if "外部文档标记" in reason:
        if kind == "第N章" and ext:
            return "② 外部文档（保持原样）", "行内是手册/规范的章号，不是本书章号", owner
        return "③ 待确认", "上下文有外部文档标记，但编号形态像本书引用", owner
    if "跨章 § 无就近章号提示" in reason:
        if "http" in line or "RFC" in line:
            return "② 外部文档（保持原样）", "行内是外部规范的节号（如 MQTT/AUTOSAR/DesignWare）", owner
        return "③ 待确认", "无章号提示的跨章 §，需确认是本书引用还是外部规范", owner
    if "图" in kind or "表" in kind:
        return "③ 待确认", "图表号在目标章不存在（悬空），需修内容或补图", owner
    if "旧节号解析不到" in reason:
        return "③ 待确认", "本书风格引用但旧节号无法解析（正文旧编号残留），需按实际节标题重写", owner
    return "③ 待确认", "其他", owner


def write_report(path, chapters, changed_files, tool_changes, renames, figdirs,
                 figfiles, report, unmanaged, applied, baseline="", tokens=None):
    p = Path(path)
    if not p.is_absolute():
        p = ROOT / p
    p.parent.mkdir(parents=True, exist_ok=True)
    owners = load_owners()
    out = []
    a = out.append
    a("# renumber 明细报告（%s）" % ("已落地" if applied else "dry-run"))
    a("")
    a("> 由 tools/renumber.py 生成；每一条都是 old -> new 逐条替换记录，供人工抽查。")
    a("")
    a("## 0. 编号基线与占位符扫描")
    a("")
    a("- 占位符/可疑标记扫描：**%d 处**（renumber 本身不使用 @XX@ 占位符，纯正则改写，代码块原样保留）。" % len(tokens))
    for nm, ln, kind, tok, inf in tokens:
        a("  - %s:%d [%s] %r" % (nm, ln, kind, tok))
    a("")
    a("- 本次运行前的基线：**%s**" % (baseline or "（未采集）"))
    a("- 验收目标：重编号后 tools/check_numbering.py **0 错误**；警告逐类解释或清零。")
    a("")
    a("## 1. 汇总")
    a("")
    a("| 项 | 数量 |")
    a("|---|---:|")
    a("| 章节文件改动 | %d |" % len(changed_files))
    a("| tools/docs/README 文本改动 | %d |" % len(tool_changes))
    a("| 章节文件重命名 | %d |" % len(renames))
    a("| 图目录重命名 | %d |" % len(figdirs))
    a("| 图文件重命名 | %d |" % len(figfiles))
    a("| 替换条目（old -> new） | %d |" % len(report.changes))
    a("| 外部文档引用（跳过，保持原样） | %d |" % len(report.skips))
    a("| 待人工确认条目 | %d |" % len(report.review))
    a("")
    a("## 2. 章号映射")
    a("")
    a("| 旧 | 新 | 文件 |")
    a("|---|---|---|")
    for c in chapters:
        a("| %02d | %02d | %s -> %s |" % (c.old_no, c.new_no, c.path.name, c.new_path.name))
    a("")
    n_over = len([x for x in report.changes if x[2].startswith("§(override)")])
    a("## 3. 判定清单（①本书内部引用 / ②外部文档 / ③待确认）")
    a("")
    a("### ① 本书内部引用 -> 新号（显式判定 %d 条）" % n_over)
    a("")
    a("| 文件 | 旧引用 | 新引用 | 理由 |")
    a("|---|---|---|---|")
    for rel, ln, kind, old, new in report.changes:
        if kind.startswith("§(override)"):
            reason = ""
            for r_, o_, n_, rs_ in OVERRIDES:
                if o_ == old:
                    reason = rs_
            a("| %s | %s | %s | %s |" % (rel, old, new, reason))
    a("")
    n_short = len([x for x in report.changes if x[2] == "§简写"])
    a("另有 %d 条章内简写引用（如 §8 -> §<新章>.<新节>）已在明细里标为 [§简写]；" % n_short)
    a("其余本书内部引用（§/第N章/图/表/路径/标题）共 %d 条，已按映射表自动改写。"
      % (len(report.changes) - n_short))
    a("")
    a("### ② 外部文档引用（保持原样，%d 条，去重）" % len(set((r, f) for r, l, f, x in report.skips)))
    a("")
    a("| 文件 | 引用 | 原因 |")
    a("|---|---|---|")
    seen = set()
    for rel, ln, ref, reason in report.skips:
        key = (rel, ref)
        if key in seen:
            continue
        seen.add(key)
        a("| %s | %s | %s |" % (rel, ref, reason))
    a("")
    grouped = {}
    for rel, ln, kind, ref, reason in report.review:
        line = ""
        pth = ROOT / rel
        if pth.exists():
            ls = read_text(pth).splitlines()
            if 0 < ln <= len(ls):
                line = ls[ln - 1]
        verdict, action, owner = judge(rel, ln, kind, ref, reason, line, owners)
        grouped.setdefault((rel, kind, ref, reason, verdict, action, owner), []).append(ln)
    a("### ③ 待确认（%d 条，去重）" % len(grouped))
    a("")
    a("| # | 文件 | 行 | 引用 | 判定 | 处理建议 | 责任人 |")
    a("|---:|---|---:|---|---|---|---|")
    for i, ((rel, kind, ref, reason, verdict, action, owner), lns) in enumerate(grouped.items(), 1):
        a("| %d | %s | %s | %s | %s | %s | %s |"
          % (i, rel, ",".join(str(x) for x in lns[:12]), ref.replace("|", "/"),
             verdict, action, owner))
    a("")
    a("## 4. 替换明细（按文件分组，old -> new）")
    a("")
    cur = None
    for rel, ln, kind, old, new in report.changes:
        if rel != cur:
            a("")
            a("### %s" % rel)
            a("")
            cur = rel
        a("- L%d [%s] %s  ->  %s" % (ln, kind, old.replace("|", "/"), new.replace("|", "/")))
    a("")
    a("## 5. 跳过明细（外部文档引用，%d 条）" % len(report.skips))
    a("")
    a("| 文件 | 行 | 引用 | 原因 |")
    a("|---|---:|---|---|")
    for rel, ln, ref, reason in report.skips:
        a("| %s | %d | %s | %s |" % (rel, ln, ref.replace("|", "/"), reason))
    a("")
    a("## 6. 未纳入重映射的文件")
    a("")
    for rel, nums in sorted(unmanaged.items()):
        a("- %s（旧章号 %s）" % (rel, nums))
    p.write_text("\n".join(out) + "\n", encoding="utf-8")
    return p


def plan_chapters(only_arg):
    chapters = discover_chapters(ROOT)
    if not chapters:
        sys.stderr.write("ERROR: docs/chapters 下没有 NN-*.md\n")
        sys.exit(2)
    cmap = build_map(chapters)
    only = None
    if only_arg:
        only = set(int(t.strip()) for t in only_arg.split(",") if t.strip())
    for c in chapters:
        scan_chapter(c)
    return chapters, cmap, only


def main(argv=None):
    ap = argparse.ArgumentParser(description="全书章节/小节编号统一 + 引用重映射")
    ap.add_argument("--dry-run", action="store_true", help="只统计与样例 diff（默认行为）")
    ap.add_argument("--apply", action="store_true", help="落地写盘")
    ap.add_argument("--plan", action="store_true", help="打印章号/节号映射表")
    ap.add_argument("--only", help="只处理指定章（逗号分隔，如 30,31）")
    ap.add_argument("--diff", type=int, default=6, help="dry-run 打印的 unified diff 段数（默认 6）")
    ap.add_argument("--diff-file", help="打印指定文件的完整 diff（相对仓库根）")
    ap.add_argument("--report", default="docs/drafts/renumber-report.md", help="明细报告路径（空则不写）")
    ap.add_argument("--extra", action="store_true", help="连仓库根 README.md 一起改（只改 README；交接文档不在公开仓库）")
    ap.add_argument("--no-readmes", action="store_true", help="不处理 samples/components/host 下的 README.md")
    ap.add_argument("--force", action="store_true", help="已执行过也强制重跑")
    ap.add_argument("--root", help="在镜像根上运行（默认仓库根）")
    args = ap.parse_args(argv)

    global ROOT, DOCS_DIR, CH_DIR, FIG_DIR, TOOLS_DIR, WHITELIST
    if args.root:
        ROOT = Path(args.root).resolve()
        DOCS_DIR = ROOT / "docs"
        CH_DIR = DOCS_DIR / "chapters"
        FIG_DIR = DOCS_DIR / "figures"
        TOOLS_DIR = ROOT / "tools"
        WHITELIST = TOOLS_DIR / "renumber_whitelist.txt"

    chapters, cmap, only = plan_chapters(args.only)
    report = Report()
    rw = Rewriter(cmap, chapters, report)

    if args.plan:
        print("章号映射（旧 -> 新）：")
        for c in chapters:
            mark = "" if c.new_no == c.old_no else "   <- 顺延"
            print("  %02d %-40s -> %02d %s%s" % (c.old_no, c.path.name, c.new_no, c.new_path.name, mark))
        print("\n共 %d 章，顺延 %d 章" % (len(chapters),
                                          sum(1 for c in chapters if c.new_no != c.old_no)))
        return 0

    applied = all(c.canonical() for c in chapters) and \
        [c.old_no for c in chapters] == list(range(len(chapters)))
    if applied and not args.force:
        print("检测到全书编号已规范（章号连续 0..%d 且 H2/H3 符合 <章>.<节>[.<小节>]），"
              % (len(chapters) - 1))
        print("判定已执行过本次重编号，无需改动（幂等）。需要强制重跑加 --force。")
        return 0

    changed_files = []
    for c in chapters:
        if only and c.old_no not in only:
            continue
        raw = read_text(c.path)
        new_lines = rebuild_chapter(c, cmap, rw)
        tail = "\n" if raw.endswith("\n") else ""
        old_text = "\n".join(c.lines) + tail
        new_text = "\n".join(new_lines) + tail
        if new_text != old_text:
            changed_files.append((c, old_text, new_text))
            report.bump("章节正文改写")

    tool_changes = []
    if not only:
        files = [ROOT / x for x in TOOL_FILES] + tool_glob_files(ROOT) + doc_prose_files(ROOT)
        if not args.no_readmes:
            files += readme_files(ROOT)
        if args.extra:
            files += [ROOT / x for x in EXTRA_FILES]
        seen = set()
        for p in files:
            if not p.exists() or p in seen:
                continue
            seen.add(p)
            if p.name in ("renumber.py", "check_numbering.py", "renumber_whitelist.txt"):
                continue
            if "drafts" in p.parts:
                continue
            old = read_text(p)
            new = rewrite_tool_file(p, cmap, rw, report)
            if new != old:
                tool_changes.append((p, old, new))
                report.bump("tools/docs 文本改写")

    renames, figdirs, figfiles = collect_renames(chapters, cmap, only, report)
    baseline = baseline_numbers()

    print("== 章号映射 ==")
    for c in chapters:
        if c.new_no != c.old_no:
            print("  %02d %-38s -> %02d %s" % (c.old_no, c.path.name, c.new_no, c.new_path.name))
    print("  （其余 %d 章号不变）" % sum(1 for c in chapters if c.new_no == c.old_no))

    print("\n== 章节正文 ==")
    total_h2 = total_h3 = 0
    for c, old, new in changed_files:
        d = list(difflib.unified_diff(old.splitlines(), new.splitlines(), lineterm="", n=0))
        plus = sum(1 for l in d if l.startswith("+") and not l.startswith("+++"))
        total_h2 += c.h2_rewritten
        total_h3 += c.h3_rewritten
        print("  %-34s H2 %2d  H3 %2d  改动行 %3d" % (c.path.name, c.h2_rewritten, c.h3_rewritten, plus))
    print("  合计：%d 个章节文件，H2 %d 条，H3 %d 条" % (len(changed_files), total_h2, total_h3))

    print("\n== tools / docs 散文 ==")
    for p, old, new in tool_changes:
        d = list(difflib.unified_diff(old.splitlines(), new.splitlines(), lineterm="", n=0))
        plus = sum(1 for l in d if l.startswith("+") and not l.startswith("+++"))
        print("  %-52s 改动行 %3d" % (str(p.relative_to(ROOT)), plus))

    print("\n== 重命名 ==")
    print("  章节文件 %d 个，图目录 %d 个，图文件 %d 个" % (len(renames), len(figdirs), len(figfiles)))
    for a, b in renames[:6]:
        print("    %s -> %s" % (a.name, b.name))
    if len(renames) > 6:
        print("    ...（其余 %d 个）" % (len(renames) - 6))
    for a, b in figdirs[:7]:
        print("    %s/ -> %s/" % (a.name, b.name))
    if len(figdirs) > 7:
        print("    ...（图目录其余 %d 个）" % (len(figdirs) - 7))

    kinds = {}
    for k in report.counts:
        if k.startswith("需复核:") or k.startswith("跳过:"):
            kinds[k] = report.counts[k]
    print("\n== 引用处理统计 ==")
    print("  替换条目（old->new）：%d" % len(report.changes))
    print("  人工复核条目：%d" % len(report.review))
    print("  跳过（外部文档引用）：%d" % len(report.skips))
    for k, v in sorted(kinds.items(), key=lambda x: -x[1]):
        print("    %-40s %d" % (k, v))
    for rel, ln, kind, ref, reason in report.review[:12]:
        print("    [复核] %s:%d %s %s" % (rel, ln, ref, reason))

    handled = set()
    for p, old, new in tool_changes:
        handled.add(str(p.relative_to(ROOT)))
    tokens = scan_tokens(chapters)
    print("\n== 占位符/可疑标记扫描（renumber 不使用 @XX@ 占位符，纯正则替换） ==")
    print("  命中 %d 处（应为 0；非 0 需内容负责人修）" % len(tokens))
    for name, ln, kind, tok, inf in tokens[:15]:
        print("    %s:%d [%s] %r%s" % (name, ln, kind, tok, "（代码块内）" if inf else ""))
    unmanaged = scan_unmanaged(ROOT, cmap, handled)
    if unmanaged:
        print("\n== 未纳入重映射的文件（需人工决定，%d 个） ==" % len(unmanaged))
        for rel, nums in list(unmanaged.items())[:10]:
            print("  %-52s 涉及旧章号 %s" % (rel, nums))
        if len(unmanaged) > 10:
            print("  ...（其余 %d 个）" % (len(unmanaged) - 10))

    if args.diff_file:
        target = (ROOT / args.diff_file).resolve()
        for p, old, new in [(c.path, o, n) for c, o, n in changed_files] + tool_changes:
            if p.resolve() == target:
                print("\n== %s 完整 diff ==" % args.diff_file)
                print("\n".join(difflib.unified_diff(old.splitlines(), new.splitlines(),
                                                     fromfile=str(p), tofile=str(p) + " (new)",
                                                     lineterm="")))
                break
        else:
            print("\n（--diff-file: %s 没有改动）" % args.diff_file)
    elif args.diff > 0 and changed_files:
        print("\n== 样例 diff（前 %d 段） ==" % args.diff)
        shown = 0
        for c, old, new in changed_files:
            d = list(difflib.unified_diff(old.splitlines(), new.splitlines(),
                                          fromfile=c.path.name, tofile=c.new_path.name,
                                          lineterm="", n=1))
            if not d:
                continue
            print("\n".join(d[:26]))
            print("  ...")
            shown += 1
            if shown >= args.diff:
                break

    print("\n编号基线（tools/check_numbering.py -q）：%s" % baseline)
    if not args.apply:
        print("\n（--dry-run：未写盘。加 --apply 落地；落地前先 git tag pre-r8-renumber）")
        if args.report:
            write_report(args.report, chapters, changed_files, tool_changes,
                         renames, figdirs, figfiles, report, unmanaged, applied=False,
                         baseline=baseline, tokens=tokens)
            print("明细报告：%s" % args.report)
        return 0

    for p, old, new in [(c.path, o, n) for c, o, n in changed_files] + tool_changes:
        write_text(p, new)
    apply_renames(renames, figdirs, figfiles, cmap)
    print("\n已改写 %d 个文件，重命名 %d 章 + %d 图目录 + %d 图文件"
          % (len(changed_files) + len(tool_changes), len(renames), len(figdirs), len(figfiles)))

    if not only:
        for script in ("tools/gen_toc.py", "tools/gen_index.py"):
            sp = ROOT / script
            if sp.exists():
                r = subprocess.run([sys.executable, str(sp)], cwd=str(ROOT),
                                   capture_output=True, text=True)
                if r.returncode != 0:
                    sys.stderr.write("WARNING: %s 失败: %s\n" % (script, r.stderr.strip()[:300]))
                else:
                    print("  %s: %s" % (script, r.stdout.strip()[:140]))
                    report.regenerated.append(script)
    if args.report:
        write_report(args.report, chapters, changed_files, tool_changes,
                     renames, figdirs, figfiles, report, unmanaged, applied=True,
                     baseline=baseline, tokens=tokens)
        print("明细报告：%s" % args.report)
    print("\n下一步：python3 tools/check_numbering.py && python3 tools/check_docs.py --toc "
          "&& python3 tools/check_refs.py && python3 tools/check_figs.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())

