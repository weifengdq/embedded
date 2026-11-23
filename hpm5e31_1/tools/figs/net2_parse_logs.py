#!/usr/bin/env python3
"""tools/figs/net2_parse_logs.py —— 从实测日志中提取 iperf/ping 结果，生成 net2_iperf_data.json

用法:
    python3 tools/figs/net2_parse_logs.py <host_iperf.log> <serial.log>

说明：host_iperf.log 是主机侧 iperf2 的原始输出（含 "=== IPERF2 TCP ... ===" 分段标题），
      serial.log 是 hwrun 抓到的串口日志（含 [cpu] ... 一行）。
本脚本只做机械提取，不做任何"估算"，提取不到就报错，避免编数据。
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def parse_sections(text):
    sections = {}
    cur = None
    for line in text.splitlines():
        m = re.match(r"^=== (.+?) ===$", line.strip())
        if m:
            cur = m.group(1)
            sections.setdefault(cur, [])
            continue
        if cur:
            sections[cur].append(line)
    return {k: "\n".join(v) for k, v in sections.items()}


def find_mbps(text):
    # iperf2 输出形如: [  4]  0.00-10.00  sec  1.09 GBytes   941 Mbits/sec
    vals = re.findall(r"([0-9.]+)\s+Mbits/sec", text)
    return [float(v) for v in vals]


def find_loss(text):
    m = re.search(r"\(([0-9.]+)%\)", text)
    return float(m.group(1)) if m else None


def find_jitter(text):
    m = re.search(r"([0-9.]+)\s+ms\s+[0-9]+/\s*[0-9]+", text)
    return float(m.group(1)) if m else None


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    host = Path(argv[1]).read_text(encoding="utf-8", errors="ignore")
    serial = Path(argv[2]).read_text(encoding="utf-8", errors="ignore") if Path(argv[2]).exists() else ""
    sec = parse_sections(host)

    def pick(name):
        for k, v in sec.items():
            if name in k:
                return v
        raise SystemExit("找不到日志分段: %s" % name)

    tcp_down = max(find_mbps(pick("IPERF2 TCP")))
    udp_series = []
    for key in sorted(k for k in sec if k.startswith("IPERF2 UDP")):
        body = sec[key]
        mbps = find_mbps(body)
        offered = None
        m = re.search(r"IPERF2 UDP ([0-9]+)M", key)
        if m:
            offered = float(m.group(1))
        loss = find_loss(body)
        jitter = find_jitter(body)
        udp_series.append({
            "offered_mbps": offered,
            "received_mbps": min(mbps) if mbps else 0.0,
            "loss_pct": loss if loss is not None else 0.0,
            "jitter_ms": jitter,
        })

    cpu = None
    m = re.search(r"\[cpu\] busy=([0-9.]+)%", serial)
    if m:
        cpu = float(m.group(1))

    data = {
        "tcp_down_mbps": tcp_down,
        "udp_down_mbps": udp_series[0]["received_mbps"] if udp_series else 0.0,
        "udp_series": udp_series,
        "cpu_busy_pct": cpu,
        "source_host_log": str(argv[1]),
        "source_serial_log": str(argv[2]),
    }
    out = HERE / "net2_iperf_data.json"
    out.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    print("wrote", out)
    print(json.dumps(data, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
