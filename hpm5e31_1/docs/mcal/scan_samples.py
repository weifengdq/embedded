#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""扫描 samples/mcal/*/README.md 与 logs/，汇总每个例程的可引用实测数据（人工复核用）。"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
samples = sorted(p for p in (ROOT / "samples" / "mcal").glob("*") if p.is_dir())
logs = sorted((ROOT / "logs").glob("*.log"), key=lambda p: p.stat().st_mtime)

def latest(sample):
    hits = [p for p in logs if sample in p.name]
    return hits[-1] if hits else None

key = re.compile(r"(PASS|FAIL|fps|frame/s|Mbit/s|KB/s|MB/s|us\b|ns\b|Hz|%|errors?=|ok=|delta=|jitter|抖动|周期)")
out = []
for s in samples:
    log = latest(s.name)
    readme = s / "README.md"
    lines = []
    if readme.exists():
        txt = readme.read_text(encoding="utf-8", errors="replace")
        for ln in txt.splitlines():
            if "logs/" in ln or key.search(ln):
                lines.append(ln.strip())
    out.append("== %s | README=%s | log=%s" % (s.name, bool(readme.exists()),
                                                log.name if log else "-"))
    if log:
        tail = [ln.strip() for ln in log.read_text(encoding="utf-8", errors="replace").splitlines()
                if ln.strip() and key.search(ln)]
        out += ["   LOG " + t for t in tail[-4:]]
    out += ["   DOC " + t for t in lines[:6]]
(ROOT / "docs" / "mcal" / "sample_scan.txt").write_text("\n".join(out) + "\n", encoding="utf-8")
print("\n".join(out))
