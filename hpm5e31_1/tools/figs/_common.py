#!/usr/bin/env python3
"""tools/figs/_common.py —— 全书插图公共库（中文字体 / 统一配色 / 尺寸 / 300dpi 保存）

所有章内插图脚本都应 import 本模块，保证全书风格一致：

    from _common import setup, save, COLORS
    import matplotlib.pyplot as plt
    setup()
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    ...
    save(fig, 26, "fig-26-1-memory-map")

约定：
  * 输出路径 docs/figures/<章号两位>/<文件名>.png，300 dpi、白底、紧凑边距；
  * 图题编号"图 X-Y"由 Markdown 图注负责，PNG 内不放标题（避免与图注重复）；
  * 需要 graphviz 时用 render_dot()，同样输出到 docs/figures/。

自检：
    python3 tools/figs/_common.py --check      # 检查字体 + 生成字体自检图
    python3 tools/figs/_common.py --fonts      # 列出可选中文字体
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
FIGURES = DOCS / "figures"

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager

# ---- 统一配色（全书共用；色盲友好、打印灰度也能区分） ----
COLORS = {
    "blue": "#1F6FEB",
    "orange": "#D29922",
    "green": "#2EA043",
    "red": "#DA3633",
    "purple": "#8957E5",
    "teal": "#1F9EA8",
    "gray": "#6E7781",
    "dark": "#24292F",
    "light": "#F6F8FA",
}
CYCLE = [COLORS[k] for k in ("blue", "orange", "green", "red", "purple", "teal", "gray")]

_FONT_CANDIDATES = [
    "Noto Sans CJK SC", "Noto Sans CJK JP", "Noto Sans CJK TC",
    "WenQuanYi Micro Hei", "WenQuanYi Zen Hei", "Source Han Sans SC",
    "Microsoft YaHei", "SimHei", "Arial Unicode MS", "Noto Serif CJK SC",
]
FONT = None


def pick_font():
    """返回系统中第一个可用的中文字体名；没有则返回 None（此时中文会显示为方框）。"""
    global FONT
    try:
        names = {f.name for f in font_manager.fontManager.ttflist}
    except Exception:
        names = set()
    for cand in _FONT_CANDIDATES:
        if cand in names:
            FONT = cand
            return cand
    FONT = None
    return None


def setup(font_size=10.5):
    """设置 matplotlib 全局样式。每个脚本开头调用一次。"""
    pick_font()
    sans = [FONT] if FONT else []
    sans += ["DejaVu Sans", "sans-serif"]
    plt.rcParams.update({
        "font.family": "sans-serif",
        "font.sans-serif": sans,
        "font.size": font_size,
        "axes.titlesize": font_size + 1.5,
        "axes.labelsize": font_size,
        "xtick.labelsize": font_size - 1,
        "ytick.labelsize": font_size - 1,
        "legend.fontsize": font_size - 1,
        "axes.unicode_minus": False,
        "figure.dpi": 150,
        "savefig.dpi": 300,
        "axes.edgecolor": "#57606A",
        "axes.linewidth": 0.8,
        "grid.color": "#D0D7DE",
        "grid.linewidth": 0.6,
        "grid.linestyle": "--",
        "figure.facecolor": "white",
        "axes.facecolor": "white",
    })
    return FONT


def new_fig(width=6.4, height=4.0):
    """新建 (fig, ax)，返回二元组。"""
    return plt.subplots(figsize=(width, height))


def style_axes(ax, xlabel=None, ylabel=None, title=None, grid=True, axis="y"):
    """统一坐标轴样式：去掉上/右边框、虚线网格、中文标签。"""
    if xlabel:
        ax.set_xlabel(xlabel)
    if ylabel:
        ax.set_ylabel(ylabel)
    if title:
        ax.set_title(title, loc="left", fontweight="bold", color=COLORS["dark"])
    if grid:
        ax.grid(True, axis=axis, zorder=0)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    return ax


def save(fig, chapter, name, dpi=300):
    """保存到 docs/figures/<chapter>/<name>.png；chapter 可传 26 或 '26' 或 '26-registers-errata'。"""
    chapter = str(chapter)
    if chapter.isdigit():
        chapter = "%02d" % int(chapter)
    out_dir = FIGURES / chapter
    out_dir.mkdir(parents=True, exist_ok=True)
    if not name.lower().endswith(".png"):
        name += ".png"
    out = out_dir / name
    fig.savefig(out, dpi=dpi, bbox_inches="tight", pad_inches=0.05, facecolor="white")
    plt.close(fig)
    try:
        rel = out.relative_to(ROOT)
    except ValueError:
        rel = out
    print("saved %s" % rel)
    return out


def render_dot(dot_source, chapter, name, dpi=300):
    """把 graphviz DOT 源码渲染成 docs/figures/<chapter>/<name>.png。

    需要系统安装 graphviz（dot 命令）。DOT 源码可放在脚本内做字符串，避免两个文件。
    """
    if shutil.which("dot") is None:
        raise RuntimeError("找不到 graphviz 的 dot 命令（apt-get install graphviz）")
    chapter = str(chapter)
    if chapter.isdigit():
        chapter = "%02d" % int(chapter)
    out_dir = FIGURES / chapter
    out_dir.mkdir(parents=True, exist_ok=True)
    tmp = out_dir / (name + ".dot")
    tmp.write_text(dot_source, encoding="utf-8")
    out = out_dir / (name + ".png")
    cmd = ["dot", "-Tpng", "-Gdpi=%d" % dpi, "-o", str(out), str(tmp)]
    subprocess.run(cmd, check=True)
    print("saved %s (dot: %s)" % (out.relative_to(ROOT), tmp.relative_to(ROOT)))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description="绘图公共库自检")
    ap.add_argument("--check", action="store_true", help="检查字体并生成字体自检图")
    ap.add_argument("--fonts", action="store_true", help="列出候选字体是否可用")
    args = ap.parse_args(argv)
    if args.fonts:
        pick_font()
        for c in _FONT_CANDIDATES:
            print("%-24s %s" % (c, "OK" if c == FONT else "-"))
        return 0
    if args.check:
        setup()
        fig, ax = new_fig(6.4, 2.6)
        ax.axis("off")
        ax.text(0.02, 0.78, "中文字体自检：HPM5E31 实战手册 300dpi 白底", fontsize=14,
                color=COLORS["dark"], transform=ax.transAxes)
        ax.text(0.02, 0.52, "图 0-1 示例：外设基地址 0xF0040000 / 中断号 18 / 误差 ±1.5%",
                fontsize=11, color=COLORS["blue"], transform=ax.transAxes)
        ax.text(0.02, 0.26, "字体：%s" % (FONT or "未找到中文字体！"), fontsize=10,
                color=COLORS["gray"], transform=ax.transAxes)
        save(fig, "_common", "font-check")
        print("字体: %s" % (FONT or "未找到中文字体（中文会显示为方框）"))
        return 0
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
