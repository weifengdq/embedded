# tools/ —— 构建、文档、图表与 PDF 合成工具

本目录是"例程 + 书稿"工程的自动化工具集。所有脚本都要求**幂等、可重复运行**，失败时有清晰错误信息。

## 一键出 PDF（最常用）

```bash
cd hpm5e31_1
bash tools/build_pdf.sh
```

产物：`docs/build/HPM5E31-Practical-Guide-v<版本>.pdf`（版本来自 `docs/VERSION`），
构建日志：`tools/build_pdf.log`。

常用参数：

| 参数 | 作用 |
|---|---|
| `--version X.Y.Z` | 覆盖版本号（不修改 docs/VERSION） |
| `--only 04,05,26` | 只合成指定章（快速看版式） |
| `--no-cover --no-toc` | 无封面/目录（最快速自检） |
| `--lof` | 追加插图目录（默认已开启） |
| `--strict` | 目录中登记的章节缺失时直接失败（默认跳过并告警） |
| `--keep-temp` | 保留 `docs/build/.tmp/` 中间文件（排查 LaTeX 错误） |
| `--quiet` | 安静模式 |

合成流程（`build_pdf.sh` 内部）：

1. 解析 `docs/00-目录.md` 得到章节顺序（只收录已存在的 `docs/chapters/NN-*.md`）；
2. 预处理 Markdown：去掉 HTML 注释、把本地图片/链接改成绝对路径、缺图用"【缺图】"占位并告警；
3. 调用可选的 `tools/gen_appendix_tables.py --write`（刷新第 41 章表）与 `tools/gen_soc_tables.py`（刷新 `docs/generated/soc-peripherals.md`）；
4. `latex_breaks.py` 给长路径/长标识符插入断行点，防止表格出界；
5. pandoc 生成 LaTeX（导言区模板见 `tools/latex/preamble.tex`，代码换行规则见 `tools/latex/verbatim.tex`）；
6. XeLaTeX 编译两遍（解析目录/交叉引用）；
7. XeLaTeX 生成封面+版权页；`pdf_toc.py`（PyMuPDF）合并并写入**三级书签**（章/节/小节 + 封面/版权页/目录/插图目录）与元数据；`pdf_outline.py` 二次校验书签条目数/层级（不足直接失败）；
8. 打印页数/大小，写日志。

## 质量检查（提交前必须跑）

```bash
python3 tools/check_docs.py                  # 全量（外设章完整门槛 + 综述章简化门槛）
python3 tools/check_docs.py --file docs/chapters/26-registers-errata.md
python3 tools/check_docs.py --toc            # 额外检查 00-目录.md 登记文件是否存在
python3 tools/check_docs.py --json           # 机器可读（CI/汇总用）
python3 tools/check_docs.py --strict         # 警告也算失败
```

检查项：行数、二级标题数、表/图/代码块数量、必备章节、"参考来源"/"待确认项与风险"、
相对链接与图片是否存在、占位词、连续空行、中文标点、标题层级、图号/章号一致性等。
退出码非 0 表示不合格。章节类型规则见脚本头部注释与 `docs/写作规范.md` 第 6 节。

## 其它脚本

| 脚本 | 用途 |
|---|---|
| `tools/gen_index.py` | 生成 `docs/README.md` 章节索引/进度表；`--check` 校验是否最新 |
| `tools/gen_regmap.py` | 从 SDK 头文件生成外设基地址/中断号表（Markdown/JSON），供第 26 章使用 |
| `tools/gen_appendix_tables.py` | 刷新第 41 章附录的引脚复用表与日志索引（Lead 维护） |
| `tools/gen_soc_tables.py` | 生成 `docs/generated/soc-peripherals.md`（Lead 维护） |
| `tools/latex_breaks.py` | PDF 合成辅助：长路径断行（一般由 build_pdf.sh 调用） |
| `tools/figs/_common.py` | 全书插图公共库：中文字体/统一配色/300dpi 保存/graphviz 渲染 |
| `tools/figs/*.py` | 各章插图生成脚本（命名 `ch<章号>-*.py` 或 `lead_*.dot`），用法见 `tools/figs/README.md` |
| `tools/pdf_toc.py` | 合并封面/正文 + 写入三级 PDF 书签与元数据（build_pdf.sh 调用；替代 pdfunite，避免丢 outline） |
| `tools/pdf_outline.py` | PDF 书签验收：`--check` 打印条目数/层级/前 N 条，`--dump` 导出全部，`--json` 机器可读 |

## 依赖（远程 Ubuntu）

```bash
# 一次性安装（走代理，约 1~2 GB）
export http_proxy=http://<proxy>:7890 https_proxy=http://<proxy>:7890
sudo -E apt-get install -y pandoc texlive-xetex texlive-latex-recommended \
  texlive-lang-chinese texlive-fonts-recommended fonts-noto-cjk graphviz poppler-utils lmodern
```

- pandoc >= 3（实测 3.7.0.2）、XeTeX 3.95、Noto Serif/Sans CJK SC 字体、graphviz dot、poppler-utils。
- 图表脚本需要 python3 + matplotlib（系统已装）。

## PDF 书签验收（强制）

每次合成后 `build_pdf.sh` 会自动校验；手工验收命令：

```bash
PDF=docs/build/HPM5E31-Practical-Guide-v0.4.0.pdf
python3 tools/pdf_outline.py --check "$PDF" --min-entries 58 --require-level 3
python3 tools/pdf_outline.py --dump "$PDF" | head -30     # 导出全部书签
python3 tools/pdf_outline.py --json "$PDF"                # 机器可读
```

判定标准：
- 书签条目数 **≥ 章节数 × 2**（当前 29 章 → ≥58，实际约 660 条）；
- 最大层级 = **3**（章 / 节 / 小节三级；某章无 `###` 时该章只到 2 级，允许）；
- 前置书签齐全：封面 p.1、版权页 p.2、目录、插图目录，且页码正确；
- PDF Title = `HPM5E31 实战手册 vX.Y.Z`、Author = `hpm5e31_1 项目组`。

常见问题：
| 现象 | 原因/解决 |
|---|---|
| 书签 0 条 | 用了 `pdfunite` 合并（会丢 outline）；确认走 `pdf_toc.py` |
| 最大层级 2 | `preamble.tex` 缺 `bookmarksdepth=3`，或本版无 `###` 小节 |
| 前置书签页码错 | 封面页数变化时 `pdf_toc.py` 的 shift 自动按 front.pdf 页数计算，若手工改过合并逻辑需复查 |

## 排查

| 现象 | 原因/解决 |
|---|---|
| `ERROR: 没有任何可合成的章节` | `docs/chapters/` 为空或 `00-目录.md` 行格式不对（应为 `| NN | 标题 | 文件.md | 负责人 | 状态 |`） |
| `XeLaTeX 编译失败` | 看 `tools/build_pdf.log`；常见原因：链接/图片路径含未转义下划线、表格列数不齐、缺图（新版本会用占位文本） |
| 表格右侧被裁切 | 已由 `latex_breaks.py` + `tabcolsep` 处理；仍有问题请把该表拆成两张或缩短单元格内容 |
| 代码块出界 | 已启用 `fvextra breaklines`（`tools/latex/verbatim.tex`） |
| 章节未进 PDF | 检查 `docs/00-目录.md` 是否登记、文件名是否一致；默认跳过缺失章节并打印清单 |
