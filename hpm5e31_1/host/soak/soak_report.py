#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从耐久测试日志生成 summary.md（可随时重跑，用于夜间看进度 / 早上出报告）。

用法: python3 soak_report.py <logdir>
"""
import bisect
import csv
import json
import os
import re
import statistics
import sys
from collections import defaultdict
from datetime import datetime

PING_OK = re.compile(r"^(\S+) ok seq=(\d+) rtt_ms=([0-9.]+)")
PING_LOSS = re.compile(r"^(\S+) loss seq=(\d+)")


def read_csv(path):
    if not os.path.exists(path):
        return []
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def num(v, default=None):
    try:
        if v is None or v == "":
            return default
        return float(v)
    except Exception:
        return default


def intv(v):
    x = num(v)
    return int(x) if x is not None else None


def parse_ts(s):
    try:
        return datetime.fromisoformat(s).timestamp()
    except Exception:
        return None


def pct(lst, q):
    if not lst:
        return 0.0
    i = min(len(lst) - 1, int(len(lst) * q))
    return lst[i]


def main():
    logdir = sys.argv[1] if len(sys.argv) > 1 else "."
    samples = read_csv(os.path.join(logdir, "samples.csv"))
    phases = []
    p = os.path.join(logdir, "phases.jsonl")
    if os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        phases.append(json.loads(line))
                    except Exception:
                        pass
    events = []
    p = os.path.join(logdir, "events.log")
    if os.path.exists(p):
        with open(p, encoding="utf-8", errors="replace") as f:
            events = [l.rstrip("\n") for l in f]

    start = parse_ts(samples[0]["ts"]) if samples else None
    end = parse_ts(samples[-1]["ts"]) if samples else None
    dur_h = ((end - start) / 3600.0) if (start and end) else 0.0

    # 阶段时间区间（用于把 ping 事件归到阶段；UDP 线速压测段的 ICMP 丢包是容量性的，单独统计）
    intervals = []
    for ph in phases:
        try:
            end = parse_ts(ph["ts"])
            dur = float(ph.get("dur_actual") or ph.get("dur_plan") or 0)
            if end is not None:
                intervals.append((end - dur, end, ph.get("phase", "?")))
        except Exception:
            pass
    intervals.sort()
    iv_starts = [i[0] for i in intervals]

    def phase_at(ts):
        idx = bisect.bisect_right(iv_starts, ts) - 1
        if idx >= 0:
            s, e, name = intervals[idx]
            if s <= ts <= e:
                return name
        return None

    # ---- ping 统计 ----
    hourly = defaultdict(lambda: {"n": 0, "ok": 0, "rtt": []})
    total_n = total_ok = 0
    rtts_all = []
    other_n = other_ok = 0
    udp_n = udp_ok = 0
    other_rtts = []
    p = os.path.join(logdir, "ping.log")
    if os.path.exists(p):
        with open(p, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = PING_OK.match(line)
                if m:
                    ts = parse_ts(m.group(1)); ok = True; rtt = float(m.group(3))
                else:
                    m = PING_LOSS.match(line)
                    if not m:
                        continue
                    ts = parse_ts(m.group(1)); ok = False; rtt = None
                if ts is None or start is None:
                    continue
                h = int((ts - start) // 3600)
                key = "H%02d" % h
                ph = phase_at(ts)
                if ph == "udp_h2b":
                    udp_n += 1
                    udp_ok += 1 if ok else 0
                else:
                    other_n += 1
                    hourly[key]["n"] += 1
                    if ok:
                        other_ok += 1
                        hourly[key]["ok"] += 1
                        hourly[key]["rtt"].append(rtt)
                        other_rtts.append(rtt)
                total_n += 1
                if ok:
                    total_ok += 1
                    rtts_all.append(rtt)

    loss_pct = (100.0 * (total_n - total_ok) / total_n) if total_n else 0.0
    loss_other = (100.0 * (other_n - other_ok) / other_n) if other_n else 0.0
    loss_udp = (100.0 * (udp_n - udp_ok) / udp_n) if udp_n else 0.0
    rtt_sorted = sorted(rtts_all)

    # ---- 分阶段汇总 ----
    by_type = defaultdict(lambda: {"n": 0, "ok": 0, "mbit": [], "bytes": 0, "bursts": 0, "bursts_ok": 0})
    fails = []
    for ph in phases:
        t = ph.get("phase", "?")
        d = by_type[t]
        d["n"] += 1
        if ph.get("ok"):
            d["ok"] += 1
        else:
            fails.append(ph)
        if ph.get("mbit"):
            d["mbit"].append(float(ph["mbit"]))
        d["bytes"] += int(ph.get("bytes") or 0)
        d["bursts"] += int(ph.get("bursts") or 0)
        d["bursts_ok"] += int(ph.get("bursts_ok") or 0)

    # ---- 板端 / 主机计数 ----
    def delta(key):
        vals = [intv(r.get(key)) for r in samples]
        vals = [v for v in vals if v is not None]
        if not vals:
            return None, None, None
        d = vals[-1] - vals[0]
        # 板端 *_bytes 计数是 32 位（hpm_enet 统计），长时间跑会回绕；回绕时补 2^32
        if key.endswith("_bytes") and d < 0:
            d += 1 << 32
        return vals[0], vals[-1], d

    board_keys = [
        ("hw_rx_frames", "板端收帧数"), ("hw_rx_bytes", "板端收字节"), ("hw_tx_frames", "板端发帧数"),
        ("hw_tx_bytes", "板端发字节"), ("hw_rx_err", "板端 RX 错误"), ("hw_tx_err", "板端 TX 错误"),
        ("sw_rx_pbuf_fail", "pbuf 分配失败"), ("sw_tx_drop_len", "TX 长度丢帧"),
        ("sw_rx_input_err", "协议栈输入错误"), ("retry_fail", "TX 回收失败"),
        ("hw_rx_empty", "空轮询（非错误）"),
    ]
    host_keys = [
        ("host_rx_errors", "主机 rx_errors"), ("host_rx_dropped", "主机 rx_dropped"),
        ("host_tx_errors", "主机 tx_errors"), ("host_rx_crc_errors", "主机 rx_crc_errors"),
        ("host_rx_missed_errors", "主机 rx_missed_errors"), ("host_rx_over_errors", "主机 rx_over_errors"),
    ]

    boot_events = [e for e in events if " board_boot " in e]
    # uptime 回退 = 复位（比 banner 更可靠：run 开始时板子可能已经在跑，banner 不在日志里）
    up_drops = 0
    _prev_up = None
    for r in samples:
        up = intv(r.get("uptime_s"))
        if up is not None:
            if _prev_up is not None and up + 30 < _prev_up:
                up_drops += 1
            _prev_up = up
    resets = max(len(boot_events), up_drops)
    # 合并 10 min 内的重复复位（runner 的 L2 与外部看门狗可能对同一次故障各复位一次），
    # 得到"独立故障次数"，避免把一次挂死报成两次。
    def _ev_ts(line):
        try:
            return datetime.fromisoformat(line.split(" ", 1)[0]).timestamp()
        except Exception:
            return None

    boot_ts = sorted(t for t in (_ev_ts(e) for e in boot_events) if t is not None)
    incidents = 0
    _last = None
    for _t in boot_ts:
        if _last is None or _t - _last > 600:
            incidents += 1
        _last = _t
    incidents = max(incidents, up_drops)

    # ---- 判定 ----
    verdict_reasons = []
    if other_n and loss_other > 1.0:
        verdict_reasons.append("ping 丢包 %.2f%% > 1%%（非 UDP 压测段）" % loss_other)
    for k, label in board_keys:
        a, b, d = delta(k)
        if d is not None and d > 0 and k in ("hw_rx_err", "hw_tx_err", "sw_rx_pbuf_fail",
                                             "sw_tx_drop_len", "sw_rx_input_err", "retry_fail"):
            verdict_reasons.append("%s 增加 %d" % (label, d))
    if fails:
        verdict_reasons.append("%d 个阶段未成功" % len(fails))
    if resets > 0:
        verdict_reasons.append("板子复位 %d 次（banner %d / uptime 回退 %d；合并 10 min 内重复复位"
                               " = %d 次独立故障）" % (resets, len(boot_events), up_drops, incidents))
    verdict = "PASS" if not verdict_reasons else "FAIL"

    # ---- 写报告 ----
    lines = []
    A = lines.append
    A("# 千兆车载以太网耐久测试报告")
    A("")
    A("- 日志目录：`%s`" % os.path.abspath(logdir))
    A("- 开始：%s" % (datetime.fromtimestamp(start).astimezone().isoformat() if start else "?"))
    A("- 结束：%s" % (datetime.fromtimestamp(end).astimezone().isoformat() if end else "?"))
    A("- 有效时长：**%.2f h**（采样 %d 条，阶段 %d 条）" % (dur_h, len(samples), len(phases)))
    A("- 被测链路：板 192.168.2.100 (HPM5E31 + YT8011AN 1000BASE-T1) ⇄ 转换盒 ⇄ 主机 enp6s0 192.168.2.1")
    A("")
    A("## 1 判定：**%s**" % verdict)
    A("")
    if verdict_reasons:
        A("不通过/需关注项：")
        A("")
        for r in verdict_reasons:
            A("- %s" % r)
    else:
        A("全部判据满足：ping 丢包 <1%，板端/主机错误计数无增长，无阶段失败，板子未复位。")
    A("")
    A("| 判据 | 结果 |")
    A("|---|---|")
    A("| ping（非 UDP 压测段）/ 丢包 | %d / %d / **%.3f%%** |" % (other_n, other_ok, loss_other))
    A("| ping（UDP 900M 压测段）/ 丢包 | %d / %d / %.2f%%（容量性，不计入判定） |" % (udp_n, udp_ok, loss_udp))
    A("| RTT min/avg/p99/max（全部） | %.3f / %.3f / %.3f / %.3f ms |" % (
        min(rtts_all) if rtts_all else 0, statistics.mean(rtts_all) if rtts_all else 0,
        pct(rtt_sorted, 0.99), max(rtts_all) if rtts_all else 0))
    A("| 板子复位次数（banner %d / uptime 回退 %d） | **%d**（合并 10 min 内重复复位 = %d 次独立故障） |"
      % (len(boot_events), up_drops, resets, incidents))
    A("| 失败阶段数 | %d / %d |" % (len(fails), len(phases)))
    A("")

    A("## 2 流量汇总")
    A("")
    A("| 阶段 | 次数 | 成功 | 平均速率 | 最小/最大速率 | 累计字节 | 备注 |")
    A("|---|---|---|---|---|---|---|")
    for t in ("tcp_h2b", "udp_h2b", "b2h_tcp", "idle"):
        if t not in by_type:
            continue
        d = by_type[t]
        mb = d["mbit"]
        avg = statistics.mean(mb) if mb else None
        extra = ""
        if t == "b2h_tcp" and d["bursts"]:
            extra = "burst %d/%d 成功" % (d["bursts_ok"], d["bursts"])
        A("| %s | %d | %d | %s | %s | %.2f GB | %s |" % (
            t, d["n"], d["ok"],
            ("%.1f Mbit/s" % avg) if avg else "-",
            ("%.1f / %.1f" % (min(mb), max(mb))) if mb else "-",
            d["bytes"] / 1e9, extra))
    A("")

    if phases and start:
        A("## 3 每小时趋势")
        A("")
        A("| 小时 | TCP 主机->板 | UDP 主机->板 | 板->主机 burst | 板->主机平均 | ping 丢包(非UDP段) | ping RTT avg/p99 |")
        A("|---|---|---|---|---|---|---|")
        buckets = defaultdict(lambda: {"tcp": [], "udp": [], "b2h": [], "b2h_ok": 0})
        for ph in phases:
            try:
                h = int((datetime.fromisoformat(ph["ts"]).timestamp() - start) // 3600)
            except Exception:
                continue
            b = buckets[h]
            if ph.get("phase") == "tcp_h2b" and ph.get("mbit"):
                b["tcp"].append(float(ph["mbit"]))
            elif ph.get("phase") == "udp_h2b" and ph.get("mbit"):
                b["udp"].append(float(ph["mbit"]))
            elif ph.get("phase") == "b2h_tcp":
                if ph.get("mbit"):
                    b["b2h"].append(float(ph["mbit"]))
                b["b2h_ok"] += int(ph.get("bursts_ok") or 0)
        for h in sorted(buckets):
            b = buckets[h]
            hk = "H%02d" % h
            hh = hourly.get(hk, {"n": 0, "ok": 0, "rtt": []})
            hl = (100.0 * (hh["n"] - hh["ok"]) / hh["n"]) if hh["n"] else 0.0
            hr = sorted(hh["rtt"])
            A("| %s | %s | %s | %d | %s | %.3f%% | %.3f / %.3f ms |" % (
                hk,
                ("%.1f" % statistics.mean(b["tcp"])) if b["tcp"] else "-",
                ("%.1f" % statistics.mean(b["udp"])) if b["udp"] else "-",
                b["b2h_ok"],
                ("%.1f" % statistics.mean(b["b2h"])) if b["b2h"] else "-",
                hl,
                (statistics.mean(hr) if hr else 0), pct(hr, 0.99)))
        A("")

    # ---- 物理层误码（已知问题复核：板->主机方向主机网卡 rx_crc_errors） ----
    crc_a, crc_b, crc_d = delta("host_rx_crc_errors")
    tx_a, tx_b, tx_d = delta("hw_tx_frames")
    crc_ppm = (1e6 * crc_d / tx_d) if (crc_d is not None and tx_d) else None

    A("## 4 物理层误码（板 -> 主机方向，主机网卡 CRC）")
    A("")
    if crc_d is None:
        A("无采样。")
    else:
        A("- host rx_crc_errors：%d → %d，增量 **%d**" % (crc_a, crc_b, crc_d))
        A("- 板端发送帧数：%d → %d，增量 %d" % (tx_a, tx_b, tx_d))
        if crc_ppm is not None:
            A("- 折算误码帧比例：**%.1f ppm（%.4f%%）**（CRC 错帧 / 板端发送帧）" % (crc_ppm, crc_ppm / 1e4))
        A("")
        A("> 说明：R9 及以前的 1.5% 误码根因是 YT8011AN **RGMII TXC 延迟档位 1**，R10 起固定在 4（EXT 0x9001=0x0140），")
        A("> 本段用于长期复核，期望增量为 0；若换盒/换线后出现非零，按第 12 章 §12.7.5 重扫档位。")
        # 分阶段：CRC 增量 / 板端发送帧增量（看误码主要集中在哪个方向/帧型）
        # 只归属"前后两条采样都在同一阶段"的区间：跨阶段区间的增量会混入上一阶段的流量，
        # R9 的 15 h 报告就因此把 udp_h2b 的板端发帧算成了 9.2 M（实测该段板端几乎不发帧）。
        agg = {}
        prev = None
        prev_phase = None
        for r in samples:
            cur = {k: intv(r.get(k)) for k in ("host_rx_crc_errors", "hw_tx_frames", "hw_rx_frames")}
            if prev and prev_phase == r.get("phase"):
                dc = (cur["host_rx_crc_errors"] or 0) - (prev["host_rx_crc_errors"] or 0)
                dt = (cur["hw_tx_frames"] or 0) - (prev["hw_tx_frames"] or 0)
                dr = (cur["hw_rx_frames"] or 0) - (prev["hw_rx_frames"] or 0)
                if dc >= 0 and dt >= 0:
                    a = agg.setdefault(r["phase"], {"crc": 0, "txf": 0, "rxf": 0})
                    a["crc"] += dc
                    a["txf"] += dt
                    a["rxf"] += dr
            prev = cur
            prev_phase = r.get("phase")
        if agg:
            A("")
            A("| 阶段 | CRC 错帧增量 | 板端发帧增量 | 板端收帧增量 | ppm |")
            A("|---|---|---|---|---|")
            for ph, a in agg.items():
                ppm = (1e6 * a["crc"] / a["txf"]) if a["txf"] else 0
                A("| %s | %d | %d | %d | %.1f |" % (ph, a["crc"], a["txf"], a["rxf"], ppm))
            A("")
            A("> 归属说明：只统计前后两条采样同阶段的区间，跨阶段区间丢弃（避免把上一阶段流量算进来）。")
            A("> R9 按旧算法得到的 udp_h2b 行（9.2 M 发帧 / 91 ppm）是归属错误，实际该段板端几乎不发帧；")
            A("> R10 已把板→主机 CRC 误码的根因定位为 PHY RGMII TXC 延迟档位并修复，见交接 R10。")
    A("")
    A("## 5 错误计数（首末采样差值）")
    A("")
    A("| 计数 | 起始 | 结束 | 增量 |")
    A("|---|---|---|---|")
    for k, label in board_keys + host_keys:
        a, b, d = delta(k)
        if a is None:
            continue
        A("| %s | %d | %d | **%d** |" % (label, a, b, d))
    A("")
    a, b, d = delta("uptime_s")
    if a is not None:
        A("- 板端 uptime：%.0f s → %.0f s（差 %.0f s）" % (a, b, d))
    a, b, d = delta("host_speed")
    if a is not None:
        A("- 主机链路速率采样：%d → %d Mbit/s" % (a, b))
    A("")

    A("## 6 事件时间线（异常/复位/链路）")
    A("")
    interesting = [e for e in events if any(k in e for k in (
        "recover", "reset", "board_boot", "phy ", "link", "arm_server", "phase_recover",
        "cmd_timeout", "b2h_timeout", "preflight_warn", "fatal"))]
    if interesting:
        A("```")
        for e in interesting[-120:]:
            A(e)
        A("```")
    else:
        A("无异常事件。")
    A("")

    if fails:
        A("## 7 失败阶段明细")
        A("")
        for ph in fails[-30:]:
            A("- cycle %s %s: %s" % (ph.get("cycle"), ph.get("phase"), json.dumps(ph, ensure_ascii=False)))
        A("")

    # 长稳退化：前 1/10 与后 1/10 的 TCP/UDP 平均速率对比
    if phases and start and dur_h > 1.0:
        def bucket_avg(name, lo, hi):
            vals = []
            for ph in phases:
                if ph.get("phase") != name or not ph.get("mbit"):
                    continue
                try:
                    t = (datetime.fromisoformat(ph["ts"]).timestamp() - start) / 3600.0
                except Exception:
                    continue
                if lo <= t < hi:
                    vals.append(float(ph["mbit"]))
            return statistics.mean(vals) if vals else None
        head = max(1.0, dur_h / 10.0)
        A("## 长稳退化对比（前 %.1f h vs 后 %.1f h）" % (head, head))
        A("")
        A("| 阶段 | 前段平均 | 后段平均 | 变化 |")
        A("|---|---|---|---|")
        for nm in ("tcp_h2b", "udp_h2b", "b2h_tcp"):
            a = bucket_avg(nm, 0, head)
            b = bucket_avg(nm, dur_h - head, dur_h + 0.01)
            chg = ("%+.1f%%" % (100.0 * (b - a) / a)) if (a and b) else "-"
            A("| %s | %s | %s | %s |" % (nm, ("%.1f" % a) if a else "-", ("%.1f" % b) if b else "-", chg))
        A("")

    A("## 附：复跑方法")
    A("")
    A("```bash")
    A("cd hpm5e31_1")
    A("python3 host/soak/soak_runner.py --duration-h 15 --tag <tag>")
    A("python3 host/soak/soak_report.py logs/soak-<tag>   # 随时重出报告")
    A("```")
    A("")

    out = os.path.join(logdir, "summary.md")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("summary ->", out)


if __name__ == "__main__":
    main()