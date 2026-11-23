#!/usr/bin/env python3
"""tools/check_smoke_coverage.py —— 全量上板冒烟"模式表"覆盖与陈旧项检查

背景：scripts/smoke_all.sh 按 tools/smoke_patterns.txt 的顺序烧写+校验；
新增例程时若忘了加模式行，冒烟回归会静默漏测；例程改名后旧行又会导致脚本失败。

检查：
  1) 陈旧项（ERROR）：模式表里的 sample 路径在 samples/ 下不存在；
  2) 缺失项（WARN）：samples/**/ 有 CMakeLists.txt 但没有模式行；
  3) 格式（ERROR）：字段数 <2 或正则编译失败；
  4) 重复项（WARN）：同一 sample 出现多次。

用法：
  python3 tools/check_smoke_coverage.py            # 人类可读
  python3 tools/check_smoke_coverage.py --json     # 机器可读
  python3 tools/check_smoke_coverage.py --strict   # 缺失项也算失败
退出码：0 通过；1 有 ERROR（或 --strict 下有 WARN）
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = ROOT / "tools" / "smoke_patterns.txt"
SAMPLES = ROOT / "samples"


def collect_samples() -> list[str]:
    out = []
    for cm in SAMPLES.rglob("CMakeLists.txt"):
        d = cm.parent
        rel = d.relative_to(ROOT).as_posix()
        if Path(rel).name.startswith("_"):
            continue
        out.append(rel)
    return sorted(out)


def parse_patterns() -> tuple[list[dict], list[str]]:
    rows, errs = [], []
    for i, line in enumerate(PATTERNS.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        f = line.split("\t")
        if len(f) < 2:
            errs.append("第 %d 行字段不足（需要 sample<TAB>正则[<TAB>发送命令]）" % i)
            continue
        sample, regex = f[0].strip(), f[1].strip()
        try:
            re.compile(regex)
        except re.error as e:
            errs.append("第 %d 行正则无法编译（%s）：%s" % (i, e, regex))
        rows.append({"line": i, "sample": sample, "regex": regex,
                     "send": f[2].strip() if len(f) > 2 else ""})
    return rows, errs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args()

    rows, errs = parse_patterns()
    samples = collect_samples()
    known = {r["sample"] for r in rows}
    stale = [r for r in rows if not (ROOT / r["sample"]).is_dir()]
    missing = [s for s in samples if s not in known]
    dups = {}
    for r in rows:
        dups.setdefault(r["sample"], []).append(r["line"])
    dups = {k: v for k, v in dups.items() if len(v) > 1}

    if args.json:
        print(json.dumps({"patterns": len(rows), "samples": len(samples),
                          "errors": errs, "stale": [r["sample"] for r in stale],
                          "missing": missing, "duplicates": dups},
                         ensure_ascii=False, indent=1))
    else:
        print("== 冒烟模式表覆盖 ==")
        print("  模式行 %d 条｜例程 %d 个｜覆盖率 %.1f%%" %
              (len(rows), len(samples), 100.0 * (len(samples) - len(missing)) / max(1, len(samples))))
        for e in errs:
            print("  ERROR", e)
        for r in stale:
            print("  ERROR 陈旧项（例程已不存在）：%s（第 %d 行）" % (r["sample"], r["line"]))
        for s in missing:
            print("  WARN  缺少模式行：%s" % s)
        for k, v in dups.items():
            print("  WARN  重复：%s（行 %s）" % (k, ",".join(map(str, v))))
        bad = len(errs) + len(stale)
        print("结果：%d 错误 / %d 警告 -> %s" % (bad, len(missing) + len(dups), "PASS" if bad == 0 else "FAIL"))

    bad = len(errs) + len(stale)
    if bad:
        return 1
    if args.strict and (missing or dups):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
