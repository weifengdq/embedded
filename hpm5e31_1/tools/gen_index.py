#!/usr/bin/env python3
"""tools/gen_index.py —— 生成 docs/README.md（章节索引 / 进度表 / 使用说明）

数据来源：docs/00-目录.md（顺序、标题、负责人、状态）+ docs/chapters/*.md（实际统计）。
输出：docs/README.md（自动生成，请勿手改）。

用法：
  python3 tools/gen_index.py            # 生成并写 docs/README.md
  python3 tools/gen_index.py --check    # 只检查是否与生成结果一致（CI 用，不一致退出 1）
  python3 tools/gen_index.py --stdout   # 打印到标准输出，不写文件
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
CHAPTERS = DOCS / "chapters"
TOC = DOCS / "00-目录.md"
OUT = DOCS / "README.md"
VER = DOCS / "VERSION"

RE_FENCE = re.compile(r"^\s*(\x60{3,})")
RE_TABLE = re.compile(r"^\s*\|[\s:|-]+\|\s*$")
RE_IMG = re.compile(r"!\[[^\]]*\]\([^)]+\)")
RE_H2 = re.compile(r"^##\s+\S", re.M)


def parse_toc():
    rows = []
    for line in TOC.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^\|\s*(\d{2})\s*\|\s*([^|]+?)\s*\|\s*([0-9A-Za-z_.\-]+\.md)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|", line)
        if m:
            rows.append(dict(ch=m.group(1), title=m.group(2).strip(), file=m.group(3).strip(),
                             owner=m.group(4).strip(), status=m.group(5).strip()))
    return rows


def stats(p):
    if not p.exists():
        return None
    text = p.read_text(encoding="utf-8")
    lines = text.splitlines()
    return dict(
        lines=len(lines),
        sections=len(RE_H2.findall(text)),
        tables=sum(1 for l in lines if RE_TABLE.match(l)),
        figures=len(RE_IMG.findall(text)),
        code=sum(1 for l in lines if RE_FENCE.match(l)) // 2,
        bytes=len(text.encode("utf-8")),
    )


def build(rows):
    version = VER.read_text(encoding="utf-8").strip() if VER.exists() else "0.0.0"
    out = []
    w = out.append
    w("# 《HPM5E31 实战手册》文档索引")
    w("")
    w("> 本文件由 \x60tools/gen_index.py\x60 自动生成，**请勿手工编辑**；改目录请改 [00-目录.md](00-目录.md)。")
    w("> 当前版本：**v%s** ｜ 合成命令：\x60tools/build_pdf.sh\x60 ｜ 质量检查：\x60python3 tools/check_docs.py\x60" % version)
    w("")
    w("## 一、章节目录与进度")
    w("")
    w("| 章 | 标题 | 文件 | 作者 | 状态 | 行数 | 二级标题 | 表 | 图 | 代码块 |")
    w("|---|---|---|---|---|---:|---:|---:|---:|---:|")
    done = exist = 0
    total_lines = 0
    for r in rows:
        st = stats(CHAPTERS / r["file"])
        if st is None:
            w("| %s | %s | %s | %s | %s | - | - | - | - | - |"
              % (r["ch"], r["title"], r["file"], r["owner"], r["status"]))
        else:
            exist += 1
            total_lines += st["lines"]
            if st["lines"] >= 250 and st["sections"] >= 8 and st["tables"] >= 3 and st["figures"] >= 2:
                done += 1
            w("| %s | %s | %s | %s | %s | %d | %d | %d | %d | %d |"
              % (r["ch"], r["title"], r["file"], r["owner"], r["status"],
                 st["lines"], st["sections"], st["tables"], st["figures"], st["code"]))
    w("")
    w("**进度**：已提交 %d/%d 章，达到质量门槛 %d 章，累计 %d 行。"
      % (exist, len(rows), done, total_lines))
    w("")
    w("## 二、目录结构")
    w("")
    w("| 路径 | 内容 |")
    w("|---|---|")
    w("| \x60docs/chapters/\x60 | 书稿正文（每章一个 Markdown，文件名 \x60NN-*.md\x60） |")
    w("| \x60docs/figures/<章号>/\x60 | 章内插图 PNG（300 dpi，由 \x60tools/figs/\x60 下脚本生成） |")
    w("| \x60docs/figures/_common/\x60 | 全书公用图（封面、字体自检等） |")
    w("| \x60docs/build/\x60 | 合成产物（PDF，已 gitignore） |")
    w("| \x60docs/_templates/chapter.md\x60 | 章节模板，新章从它复制 |")
    w("| \x60docs/写作规范.md\x60 | 写作规范与质量门槛（**动笔前必读**） |")
    w("| \x60tools/build_pdf.sh\x60 | 按目录顺序合成单本 PDF（封面/版权/目录/页眉页脚） |")
    w("| \x60tools/check_docs.py\x60 | 质量门槛检查（行数/标题/表/图/引用/链接/占位） |")
    w("| \x60tools/gen_index.py\x60 | 生成本索引 |")
    w("| \x60tools/gen_regmap.py\x60 | 从 SDK 头文件生成外设基地址/IRQ 速查表（第 26 章用） |")
    w("| \x60tools/figs/\x60 | 所有插图的生成脚本 + 公共绘图库 |")
    w("")
    w("## 三、构建与检查")
    w("")
    w("\x60\x60\x60bash")
    w("cd hpm5e31_1")
    w("bash tools/build_pdf.sh                 # 合成 PDF（缺失章节自动跳过并告警）")
    w("bash tools/build_pdf.sh --only 26 --no-cover   # 只合成第 26 章，快速看版式")
    w("python3 tools/check_docs.py             # 全量质量检查（退出码非 0 = 不合格）")
    w("python3 tools/check_docs.py --file docs/chapters/26-registers-errata.md")
    w("python3 tools/gen_index.py              # 重新生成本索引")
    w("\x60\x60\x60")
    w("")
    w("## 四、写作约定（摘要，详见 [写作规范](写作规范.md)）")
    w("")
    w("1. 每章 ≥250 行、≥8 个二级标题、≥3 张表、≥2 张图、≥1 段代码。")
    w("2. 图必须由 \x60tools/figs/\x60 下脚本生成（graphviz/matplotlib），产物 300 dpi PNG。")
    w("3. 所有数字给出来源：\x60RM §x.y (p.N)\x60 / \x60DS §x.y (p.N)\x60 / \x60Errata E000xx\x60 / 日志路径。")
    w("4. 必须包含“参考来源”与“待确认项与风险”两节；未实测的内容明确标注。")
    w("5. 章节文件命名 \x60NN-短横线英文名.md\x60，与 00-目录.md 保持一致。")
    w("")
    w("## 五、章节状态说明")
    w("")
    w("| 状态 | 含义 |")
    w("|---|---|")
    w("| 待写 | 尚未开始 |")
    w("| 进行中 | 正在写/正在实测 |")
    w("| 待评审 | 已提交，等待 QA |")
    w("| 完成 | 通过 \x60tools/check_docs.py\x60 且已进入 PDF |")
    w("")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="生成 docs/README.md 章节索引")
    ap.add_argument("--check", action="store_true", help="只检查文件是否为最新，不一致退出 1")
    ap.add_argument("--stdout", action="store_true", help="输出到 stdout，不写文件")
    args = ap.parse_args(argv)
    if not TOC.exists():
        sys.stderr.write("ERROR: 找不到 %s\n" % TOC)
        return 2
    text = build(parse_toc())
    if args.stdout:
        print(text)
        return 0
    old = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
    if args.check:
        if old != text:
            sys.stderr.write("docs/README.md 不是最新的，请运行 python3 tools/gen_index.py\n")
            return 1
        print("docs/README.md 已是最新")
        return 0
    OUT.write_text(text, encoding="utf-8")
    print("已生成 %s（%d 行）" % (OUT.relative_to(ROOT), len(text.splitlines())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
