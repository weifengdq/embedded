#!/usr/bin/env python3
"""tools/figs/app33_parse_logs.py —— 从 logs/*canopen*.log 提取第 33 章实测数据

输出 tools/figs/app33_data.json，供 app33_perf.py 画图。
用法：python3 tools/figs/app33_parse_logs.py [--write]
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
LOGS = ROOT / "logs"
OUT = HERE / "app33_data.json"

NUM = r"([0-9]+)"

PATTERNS = {
    # canopen_pdo 例程：SYNC 周期 -> TPDO1 统计
    "lat": re.compile(r"端到端延迟 min/avg/p99/max\s+" + NUM + r" / " + NUM + r" / " + NUM + r" / " + NUM + r" us"),
    "per": re.compile(r"TPDO 周期 min/avg/max\s+" + NUM + r" / " + NUM + r" / " + NUM + r" us"),
    "jit": re.compile(r"周期抖动 sigma / maxdev\s+([0-9.]+) / " + NUM + r" us"),
    "tpdo": re.compile(r"SYNC 帧数\s+" + NUM + r"[\s\S]{0,120}?TPDO1 帧数\s+" + NUM),
    "result": re.compile(r"=== RESULT: " + NUM + r" PASS / " + NUM + r" FAIL ==="),
    # canopen_sdo_seg 例程
    "sdo_row": re.compile(r"^\s+(0x200[12])\s+(\d+) B (upload|download)\s+(\d+)\s+([0-9.]+) ms\s+([0-9]+) B/s\s+(\w+)",
                          re.MULTILINE),
    "expedited": re.compile(r"expedited read 0x1017 x200\s+PASS\s+" + NUM + r"/(\d+) 次, ([0-9.]+) ms/次, " + NUM + r" 次/s"),
}


def newest(pattern):
    files = sorted(LOGS.glob(pattern), key=lambda p: p.stat().st_mtime)
    return files[-1] if files else None


def main():
    data = {"pdo": {}, "sdo": {}, "sources": {}}
    pdo_log = newest("*canopen_pdo.log")
    if pdo_log:
        text = pdo_log.read_text(encoding="utf-8", errors="replace")
        data["sources"]["pdo"] = str(pdo_log.relative_to(ROOT))
        m = PATTERNS["lat"].search(text)
        if m:
            data["pdo"]["lat_min_us"], data["pdo"]["lat_avg_us"], data["pdo"]["lat_p99_us"], data["pdo"]["lat_max_us"] = \
                [int(v) for v in m.groups()]
        m = PATTERNS["per"].search(text)
        if m:
            data["pdo"]["per_min_us"], data["pdo"]["per_avg_us"], data["pdo"]["per_max_us"] = [int(v) for v in m.groups()]
        m = PATTERNS["jit"].search(text)
        if m:
            data["pdo"]["jit_std_us"] = float(m.group(1))
            data["pdo"]["jit_max_us"] = int(m.group(2))
        m = PATTERNS["tpdo"].search(text)
        if m:
            data["pdo"]["sync_frames"], data["pdo"]["tpdo1_frames"] = int(m.group(1)), int(m.group(2))
        m = PATTERNS["result"].search(text)
        if m:
            data["pdo"]["pass"], data["pdo"]["fail"] = int(m.group(1)), int(m.group(2))

    sdo_log = newest("*canopen_sdo_seg.log")
    if sdo_log:
        text = sdo_log.read_text(encoding="utf-8", errors="replace")
        data["sources"]["sdo"] = str(sdo_log.relative_to(ROOT))
        rows = []
        for m in PATTERNS["sdo_row"].finditer(text):
            rows.append({
                "obj": m.group(1), "bytes": int(m.group(2)), "dir": m.group(3),
                "iters": int(m.group(4)), "best_ms": float(m.group(5)),
                "throughput_Bps": int(m.group(6)), "check": m.group(7),
            })
        data["sdo"]["rows"] = rows
        m = PATTERNS["expedited"].search(text)
        if m:
            data["sdo"]["exp_ops"] = int(m.group(1))
            data["sdo"]["exp_ms_per_op"] = float(m.group(3))
            data["sdo"]["exp_ops_per_s"] = int(m.group(4))
        m = PATTERNS["result"].search(text)
        if m:
            data["sdo"]["pass"], data["sdo"]["fail"] = int(m.group(1)), int(m.group(2))

    text = json.dumps(data, ensure_ascii=False, indent=2)
    if "--write" in sys.argv:
        OUT.write_text(text + "\n", encoding="utf-8")
        print("wrote %s" % OUT.relative_to(ROOT))
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
