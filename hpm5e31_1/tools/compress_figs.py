#!/usr/bin/env python3
"""tools/compress_figs.py —— 书稿插图压缩（公开发布用）

书稿插图由 tools/figs/ 下的脚本生成，默认是 300 dpi 的 RGBA PNG，全书 180+ 张、
合计 40 MB，直接放公开仓库偏大。本脚本做"无损观感"的等价压缩：

  * 宽度超过 --max-width（默认 1920 px）的图，按 LANCZOS 等比缩小（A4 版面
    1920 px ≈ 232 dpi，印刷与屏幕阅读都够）；
  * RGBA/RGB 图量化到 --colors（默认 256）色调色板，不加抖动——插图多是
    线条/色块，量化后肉眼无差别，体积降到 1/4；
  * 再以 PNG optimize 重写。结果比原图大时保留原图。

用法：
  python3 tools/compress_figs.py                # 原地压缩 docs/figures
  python3 tools/compress_figs.py --dry-run      # 只统计，不写盘
  python3 tools/compress_figs.py --max-width 1600 --colors 192
"""

import argparse
import io
import os
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FIGS = ROOT / "docs" / "figures"


def compress(path: Path, max_width: int, colors: int) -> tuple:
    src_size = path.stat().st_size
    with Image.open(path) as im:
        im.load()
        w, h = im.size
        img = im
        if w > max_width:
            img = im.resize((max_width, max(1, int(h * max_width / w))), Image.LANCZOS)
        if img.mode in ("RGBA", "RGB"):
            img = img.convert("RGBA").quantize(
                colors=colors, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE
            )
        buf = io.BytesIO()
        img.save(buf, "PNG", optimize=True)
    return src_size, buf.getvalue()


def main() -> int:
    ap = argparse.ArgumentParser(description="压缩 docs/figures 下的 PNG 插图")
    ap.add_argument("--max-width", type=int, default=1920, help="最大宽度（px），默认 1920")
    ap.add_argument("--colors", type=int, default=256, help="调色板颜色数，默认 256")
    ap.add_argument("--dry-run", action="store_true", help="只统计，不写盘")
    ap.add_argument("--dir", default=str(FIGS), help="图目录（默认 docs/figures）")
    args = ap.parse_args()

    files = sorted(Path(args.dir).rglob("*.png"))
    if not files:
        print("没有找到 PNG 插图", file=sys.stderr)
        return 1
    before = after = 0
    changed = 0
    for f in files:
        s0, data = compress(f, args.max_width, args.colors)
        before += s0
        if len(data) < s0:
            after += len(data)
            changed += 1
            if not args.dry_run:
                f.write_bytes(data)
        else:
            after += s0
    print(f"{len(files)} 张图：{before/1048576:.2f} MB → {after/1048576:.2f} MB"
          f"（压缩 {changed} 张，{100*(1-after/before):.1f}%）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
