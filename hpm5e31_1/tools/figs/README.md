# tools/figs/ —— 全书插图生成脚本

**规矩**：书稿里的每张图都必须由这里的脚本生成，产物放 `docs/figures/<章号两位>/<文件名>.png`
（300 dpi、白底、无标题文字，图题由 Markdown 图注负责，格式 `图 X-Y`）。

## 快速开始（给各章 owner）

1. 复制模板（任选）：

```python
#!/usr/bin/env python3
"""tools/figs/chXX-<name>.py —— 图 XX-Y：<说明>"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS   # 公共库

def main():
    setup()                                # 中文字体 + 全书统一样式
    fig, ax = save_and_plot()              # 你的绘图逻辑
    save(fig, 26, "fig-26-3-xxx")          # 自动存 docs/figures/26/fig-26-3-xxx.png

def save_and_plot():
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    ax.bar(["A", "B"], [1, 2], color=[COLORS["blue"], COLORS["orange"]])
    return fig, ax

if __name__ == "__main__":
    main()
```

2. 运行并检查：

```bash
cd hpm5e31_1
python3 tools/figs/chXX-<name>.py
ls -la docs/figures/<章号>/
```

3. 在章节 Markdown 里引用（路径**相对 docs/**）：

```markdown
![图 26-3 XXX 说明](figures/26/fig-26-3-xxx.png)
```

## 公共库 `_common.py` 提供什么

| 函数/常量 | 说明 |
|---|---|
| `setup(font_size=10.5)` | 设置中文字体（自动挑 Noto Sans CJK）、统一配色、字号、网格、白底。每个脚本调用一次 |
| `COLORS` / `CYCLE` | 全书统一配色（蓝/橙/绿/红/紫/青/灰），色盲友好、打印灰度可区分 |
| `new_fig(w, h)` | 等价 `plt.subplots(figsize=(w,h))` |
| `style_axes(ax, ...)` | 去上/右边框、虚线网格、中文轴标签 |
| `save(fig, chapter, name)` | 保存到 `docs/figures/<chapter>/<name>.png`，300 dpi、紧凑边距；chapter 可传 `26` 或 `"26"` |
| `render_dot(dot_source, chapter, name)` | 把 graphviz DOT 字符串渲染成 PNG，并把 `.dot` 源码一起放在图目录（可追溯） |

自检（会生成 `docs/figures/_common/font-check.png`，中文变方框说明字体没选对）：

```bash
python3 tools/figs/_common.py --check
python3 tools/figs/_common.py --fonts    # 列出候选中文字体是否可用
```

## 命名约定

- 脚本：`ch<章号>-<短名>.py`，例如 `ch26-memory-map.py`、`ch27-ecosystem.py`；
  其他人已经画好的图不要改（例如 Lead 的 `lead_fig_*.dot`）。
- 产物：`docs/figures/<章号>/fig-<章号>-<序号>-<短名>.png`，序号与该章图注一致。
- DOT 渲染器会把源码写到同目录 `*.dot`，提交时一起保留（便于复现/改动）。

## 现有脚本一览

| 脚本 | 产物 | 用途 |
|---|---|---|
| `gen_cover_art.py` | `docs/figures/_common/cover-board.png` | PDF 封面用的板卡框图 |
| `ch26-memory-map.py` | `docs/figures/26/fig-26-1-memory-map.png` | HPM5E31 外设基地址分布（数据来自 SDK 头文件） |
| `ch26-crash-tree.py` | `docs/figures/26/fig-26-2-debug-tree.png` | 上电/运行异常排查决策树（graphviz） |
| `ch27-ecosystem.py` | `docs/figures/27/fig-27-1-ecosystem.png` | HPM5E00 生态与软件栈全景（graphviz） |
| `ch27-feature-matrix.py` | `docs/figures/27/fig-27-2-feature-matrix.png` | 同类 MCU 能力矩阵热图 |
| `lead_fig_*.dot` | `docs/figures/01|02/*.png` | Lead 的第 1/2 章图（已渲染，勿改） |

## 注意

- 图里**不要写字号过小的说明文字**（PDF 里会看不清）；说明放图注。
- 不要把截图/网络图片直接放进书稿；必须可脚本复现（写作规范 §4）。
- 生成后检查文件大小：`ls -la`，正常 PNG 几十 KB~几百 KB；几 KB 说明图是空白/字体没加载。
