#!/usr/bin/env python3
"""tools/gen_regmap.py —— 从 HPM SDK 头文件生成"外设基地址 / 中断号"速查表

数据源（只读，不修改 SDK）：
  <SDK>/soc/HPM5E00/HPM5E31/hpm_soc_ip.h    外设实例基地址（HPM_xxx_BASE）
  <SDK>/soc/HPM5E00/HPM5E31/hpm_soc_irq.h   外部中断号（IRQn_xxx）

用法：
  python3 tools/gen_regmap.py --md                    # Markdown 表格（第 26 章直接引用）
  python3 tools/gen_regmap.py --md --sort addr        # 按地址排序（默认按 IP 名）
  python3 tools/gen_regmap.py --json                  # JSON（供绘图脚本/工具消费）
  python3 tools/gen_regmap.py --check                 # 校验 SDK 头文件可解析、统计数量
  python3 tools/gen_regmap.py --sdk /path/to/hpm_sdk  # 指定 SDK 根目录
  python3 tools/gen_regmap.py --md-irq                # 只输出中断号表

说明：基地址表只列 *_BASE 宏（不含指针宏）、按 IP 名去重合并实例；
      区域（region）按地址段分类，仅用于阅读分组，不代表总线拓扑。
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

DEFAULT_SDK = os.environ.get("HPM_SDK_BASE", "$HOME/sdk_env_v1.13.0/hpm_sdk")
SOC = "HPM5E00/HPM5E31"

RE_BASE = re.compile(r"^#define\s+HPM_([A-Za-z0-9_]+)_BASE\s+\((0x[0-9A-Fa-f]+)U?L?\)")
RE_BASE_ALT = re.compile(r"^#define\s+HPM_([A-Za-z0-9_]+)_BASE\s+(0x[0-9A-Fa-f]+)U?L?")
RE_IRQ = re.compile(r"^#define\s+IRQn_([A-Za-z0-9_]+)\s+(\d+)\s*(?:/\*\s*(.*?)\s*\*/)?")

REGIONS = [
    (0x00000000, 0x000FFFFF, "片上 ROM / ILM / DLM"),
    (0x01000000, 0x01FFFFFF, "CPU 子系统从接口"),
    (0x30000000, 0x3FFFFFFF, "AHB SRAM / DM"),
    (0x80000000, 0x9FFFFFFF, "XPI0 外部 NOR"),
    (0xE4000000, 0xE6FFFFFF, "系统控制 / PLIC / 机器定时器"),
    (0xF0000000, 0xF0FFFFFF, "外设总线（低速域）"),
    (0xF1000000, 0xF1FFFFFF, "外设总线（NTMR/其它）"),
    (0xF4000000, 0xF5FFFFFF, "外设总线（电源域/GPIO 等）"),
]


def region_of(addr):
    for lo, hi, name in REGIONS:
        if lo <= addr <= hi:
            return name
    return "其它/保留"


def sdk_paths(sdk):
    root = Path(sdk) / "soc" / SOC
    return root / "hpm_soc_ip.h", root / "hpm_soc_irq.h"


def parse_bases(ip_header):
    items = []
    seen = set()
    for line in Path(ip_header).read_text(encoding="utf-8", errors="replace").splitlines():
        m = RE_BASE.match(line) or RE_BASE_ALT.match(line)
        if not m:
            continue
        name, addr = m.group(1), int(m.group(2), 16)
        key = (name, addr)
        if key in seen:
            continue
        seen.add(key)
        items.append(dict(name=name, base=addr, base_hex="0x%08X" % addr, region=region_of(addr)))
    return items


def parse_irqs(irq_header):
    items = []
    for line in Path(irq_header).read_text(encoding="utf-8", errors="replace").splitlines():
        m = RE_IRQ.match(line)
        if m:
            items.append(dict(name=m.group(1), irq=int(m.group(2)), desc=(m.group(3) or "").strip()))
    return sorted(items, key=lambda x: x["irq"])


def group_by_ip(bases):
    """把 HPM_UART0/HPM_UART1... 归并为 UART: 0xF0040000, 0xF0044000。"""
    groups = {}
    for b in bases:
        m = re.match(r"([A-Za-z][A-Za-z0-9]*?)(\d+)$", b["name"])
        ip = m.group(1) if m else b["name"]
        groups.setdefault(ip, []).append(b)
    return groups


def md_bases(bases, sort="name"):
    rows = sorted(bases, key=(lambda x: (x["name"], x["base"])) if sort == "name" else (lambda x: (x["base"], x["name"])))
    out = ["| 实例宏 | 基地址 | 区域 |", "|---|---|---|"]
    for b in rows:
        out.append("| \x60HPM_%s_BASE\x60 | \x60%s\x60 | %s |" % (b["name"], b["base_hex"], b["region"]))
    return "\n".join(out)


def md_irq(irqs):
    out = ["| 中断宏 | 中断号 | 说明 |", "|---|---:|---|"]
    for i in irqs:
        out.append("| \x60IRQn_%s\x60 | %d | %s |" % (i["name"], i["irq"], i["desc"] or "-"))
    return "\n".join(out)


def md_summary(groups):
    out = ["| IP | 实例数 | 基地址（从低到高） |", "|---|---:|---|"]
    for ip in sorted(groups):
        inst = sorted(groups[ip], key=lambda x: x["base"])
        out.append("| %s | %d | %s |" % (ip, len(inst), ", ".join("\x60%s\x60" % x["base_hex"] for x in inst)))
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="从 HPM SDK 头文件生成外设基地址/IRQ 速查表")
    ap.add_argument("--sdk", default=DEFAULT_SDK, help="HPM SDK 根目录（默认 $HPM_SDK_BASE 或 $HOME/sdk_env_v1.13.0/hpm_sdk）")
    ap.add_argument("--md", action="store_true", help="输出全部 Markdown 表（基地址明细 + IP 汇总 + IRQ）")
    ap.add_argument("--md-irq", action="store_true", help="只输出中断号 Markdown 表")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--check", action="store_true", help="只做可解析性检查")
    ap.add_argument("--sort", choices=["name", "addr"], default="name", help="基地址表排序：name（默认）/ addr")
    ap.add_argument("--out", help="把输出写入文件（默认 stdout）")
    args = ap.parse_args(argv)

    ip_h, irq_h = sdk_paths(args.sdk)
    for p in (ip_h, irq_h):
        if not p.exists():
            sys.stderr.write("ERROR: 找不到头文件 %s（用 --sdk 指定 SDK 根目录）\n" % p)
            return 2
    bases = parse_bases(ip_h)
    irqs = parse_irqs(irq_h)
    if not bases or not irqs:
        sys.stderr.write("ERROR: 解析结果为空（头文件格式是否变化？）\n")
        return 1
    groups = group_by_ip(bases)

    if args.check:
        print("OK  %d 个实例基地址、%d 个中断号、%d 类 IP（%s）"
              % (len(bases), len(irqs), len(groups), ip_h))
        return 0

    if args.json:
        text = json.dumps(dict(source=str(ip_h), bases=bases, irqs=irqs), ensure_ascii=False, indent=2)
    elif args.md_irq:
        text = md_irq(irqs)
    elif args.md:
        text = "\n\n".join([
            "### 外设实例基地址（共 %d 个）\n\n%s" % (len(bases), md_bases(bases, args.sort)),
            "### 按 IP 汇总（共 %d 类）\n\n%s" % (len(groups), md_summary(groups)),
            "### 外部中断号（共 %d 个）\n\n%s" % (len(irqs), md_irq(irqs)),
        ])
    else:
        text = "\n".join("%-24s %s  %s" % (b["name"], b["base_hex"], b["region"]) for b in bases)
        text += "\n\n" + "\n".join("IRQn_%-28s %3d  %s" % (i["name"], i["irq"], i["desc"]) for i in irqs)

    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
        print("已写入 %s" % args.out)
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
