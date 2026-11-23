#!/usr/bin/env python3
"""tools/gen_app_matrix.py —— 应用层例程证据矩阵生成器

扫描 samples/app/*/（只覆盖应用层例程，不碰 mcal/）：
  * README.md（优先：「实测 / 关键实测结论」章节里的表格，其次该章节的项目符号列表）
  * src/*.c（README 缺失时兜底）
  * logs/*-<例程名>.log（最近一份日志：结论行与关键指标）
生成 docs/generated/app-matrix.md，供第 40 章与仓库 README 引用。

抽取规则（2026-11 修版，按交叉复查反馈加固）：
  1) 关键指标优先级：README「实测/关键实测结论」表格 → 该章节的项目符号列表 →
     最新日志里带 PASS/Hz/Mbit/KB/s/msg/s/frame/s/ms/us 的结论行；
     跳过 boot/banner/配置回显行、调试转储行，以及数值全为 0 的伪结论行。
  2) CAN ID 只认带上下文的 0x 十六进制（can send / CAN ID / id= / tx_id / rx_id），
     长度 3~8 位、值 <= 0x1FFFFFFF；以太网类例程不抽 CAN ID，只报 IP/端口。
  3) 端口从「UDP/TCP nnnn」「端口: nnnn」「:nnnn」上下文抽，避免把毫秒数当端口。

用法：
  python3 tools/gen_app_matrix.py            # 写入 docs/generated/app-matrix.md
  python3 tools/gen_app_matrix.py --check    # 只打印统计
  python3 tools/gen_app_matrix.py --stdout   # 打印 Markdown
"""
from __future__ import annotations

import argparse
import re
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APPS = ROOT / "samples" / "app"
LOGS = ROOT / "logs"
OUT = ROOT / "docs" / "generated" / "app-matrix.md"

PROTOCOL = [
    ("j1939_ecu", ("J1939", "J1939-21/-71/-73/-81")),
    ("j1939_tp", ("J1939", "J1939-21 TP")),
    ("j1939_dm", ("J1939", "J1939-73 诊断")),
    ("canopen_node", ("CANopen", "CiA 301")),
    ("canopen_pdo", ("CANopen", "CiA 301 PDO")),
    ("canopen_sdo_seg", ("CANopen", "CiA 301 SDO")),
    ("isotp_echo", ("ISO-TP", "ISO 15765-2")),
    ("uds_ecu", ("UDS", "ISO 14229")),
    ("uds_flash", ("UDS", "ISO 14229 刷写")),
    ("xcp_can_cal", ("XCP", "ASAM XCP on CAN")),
    ("xcp_can_daq", ("XCP", "ASAM XCP on CAN")),
    ("xcp_eth", ("XCP", "ASAM XCP on UDP（XCPonEth）")),
    ("someip_rpc", ("SOME/IP", "vsomeip 兼容")),
    ("someip_event", ("SOME/IP", "SOME/IP-SD + 事件")),
    ("someip_tp", ("SOME/IP", "SOME/IP-TP")),
    ("xrce_pubsub", ("XRCE-DDS", "Micro-XRCE-DDS")),
    ("xrce_ros2_bridge", ("XRCE-DDS", "ROS 2 桥接")),
    ("ptp_master", ("PTP", "IEEE 1588 E2E")),
    ("ptp_slave", ("PTP", "IEEE 1588 E2E")),
    ("gptp_demo", ("gPTP", "IEEE 802.1AS 风格")),
    ("can_gateway", ("CAN 网关", "cannelloni v2")),
    ("can_logger", ("CAN 记录", "candump 回放")),
    ("dlt_log", ("DLT", "COVESA DLT 日志")),
    ("vss_signals", ("VSS", "COVESA VSS 信号")),
    ("doip_server", ("DoIP", "ISO 13400-2（UDP 车辆发现 + TCP 13400）")),
    ("mqtt_telemetry", ("MQTT", "MQTT 3.1.1（lwIP TCP）")),
    ("gs_usb_candle", ("GS-USB", "USB↔CAN（gs_usb/candleLight）")),
    ("multiproto_coexist", ("多协议共存", "J1939+CANopen+网关")),
]

HOST_DIR = {
    "J1939": "host/j1939",
    "CANopen": "host/canopen",
    "ISO-TP": "host/uds",
    "UDS": "host/uds",
    "XCP": "host/xcp",
    "SOME/IP": "host/someip",
    "XRCE-DDS": "host/xrce",
    "PTP": "host/ptp",
    "gPTP": "host/ptp",
    "CAN 网关": "host/can_bridge",
    "CAN 记录": "host/canlog",
    "DLT": "host/dlt",
    "VSS": "host/vss",
    "DoIP": "host/doip",
    "MQTT": "host/mqtt",
    "GS-USB": "host/gs_usb",
    "多协议共存": "host/can_bridge",
}

# 以太网类协议：不抽 CAN ID，只报 IP/端口
ETH_PROTOS = {"DoIP", "MQTT", "SOME/IP", "XRCE-DDS", "PTP", "gPTP", "DLT", "GS-USB"}

RE_HEX = re.compile(r"0[xX]([0-9A-Fa-f]{3,8})")
RE_CANID_SYM = re.compile(r"(?<![A-Za-z_])(id|tx_?id|rx_?id)\s*[=:]", re.I)
RE_CANID_WORD = re.compile(r"(can\s*send|can[\s\-_]*id|canid|can_id|tx_?id|rx_?id)", re.I)
RE_PORT_CTX = re.compile(
    r"(?:UDP|TCP)\s*[:：]?\s*(\d{2,5})|端口\s*[:：]\s*(\d{2,5})|"
    r"\d{1,3}(?:\.\d{1,3}){3}:(\d{2,5})\b")
RE_PORT = re.compile(r"\b(4000|4002|4003|2018|30490|30501|3490|319|320|5001|5002|8888|13400|1883|5555)\b")
RE_HOST_PATH = re.compile(r"host/[A-Za-z0-9_./-]+")
RE_IP = re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}(?:/\d+)?\b")

UNIT_SCORE = [("Mbit/s", 70), ("Mbps", 70), ("MB/s", 65), ("KB/s", 60), ("B/s", 55),
              ("msg/s", 55), ("pkt/s", 50), ("frame/s", 50), ("帧/s", 50),
              ("Hz", 45), ("ppm", 30), ("ms", 25), ("µs", 20), ("us", 20), ("%", 12)]
GENERIC_HEAD = ("实测", "值", "结果", "数据", "实测值", "关键指标")
HEAD_WORDS = ("指标", "项目", "测试", "试验", "项", "用例", "检查项", "场景", "#")

SKIP_BOOT = re.compile(
    r"(^\[(lwip|phy|sys|boot|board)\]|link\s*(up|UP)|waiting link|Booting|board_init|"
    r"System Clock|Copyright|hpm_sdk|letter:|^usage:|^cmds:|^type '|^=====|^\s*$|"
    r"phy init|Enet phy|init passed|link down after|link UP after)", re.I)
SKIP_CONF = re.compile(
    r"(=\s*\d+\s*(ms|s)\s*OK\s*$|\bOK\s*$|^\[[a-z]+\]\s*(timer|listen|cfg|conn|route)\b|"
    r"\binit rc=|\bready\b)", re.I)
SKIP_DUMP = re.compile(r"(phase\d|arb=|name=0x|\bmaxloop=|\bcanerr=|===|\bdump\b|addr claim|seen_at)")


def read_text(path: Path, limit: int = 20000) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="ignore")[:limit]
    except OSError:
        return ""


def latest_log(name: str):
    cands = sorted(LOGS.glob("*-%s.log" % name), key=lambda p: p.stat().st_mtime)
    return cands[-1] if cands else None


def proto_of(name: str):
    for key, val in PROTOCOL:
        if name == key or name.startswith(key + "_"):
            return val
    return (name.split("_")[0].upper(), "-")


def is_eth(proto: str, name: str = "") -> bool:
    if proto in ETH_PROTOS:
        return True
    if proto == "XCP" and "eth" in name:
        return True
    return False


def transport_of(proto: str, name: str, text: str) -> str:
    low = text.lower()
    if proto == "DoIP":
        return "Ethernet（UDP/TCP 13400）"
    if proto == "MQTT":
        return "Ethernet（TCP 1883）"
    if proto == "DLT":
        return "Ethernet（UDP 3490）"
    if proto == "VSS":
        return "CAN over UDP（cannelloni）"
    if proto == "GS-USB":
        return "USB（gs_usb / candleLight）"
    if proto == "CAN 记录":
        return "CAN 经典帧 500 kbit/s"
    if proto == "多协议共存":
        return "CAN 多通道（4×500k，两段总线）+ UDP 网关"
    if proto in ("SOME/IP", "XRCE-DDS", "PTP", "gPTP"):
        return "Ethernet（lwIP）"
    if proto == "CAN 网关":
        return "CAN over UDP（cannelloni v2）"
    if proto == "XCP" and "eth" in name:
        return "Ethernet（UDP 5555）"
    if proto in ("J1939", "CANopen", "ISO-TP", "UDS", "XCP"):
        m = re.search(r"(250|500)\s*k", low)
        if m:
            return "CAN 经典帧 %s kbit/s" % m.group(1)
        if "can fd" in low or "canfd" in low:
            return "CAN FD"
        return "CAN 经典帧 %s kbit/s" % ("250" if proto == "J1939" else "500")
    if "can fd" in low or "canfd" in low:
        return "CAN FD"
    if "vcan0" in low or "cannelloni" in low:
        return "CAN over UDP（cannelloni）"
    m = re.search(r"(250|500)\s*k", low)
    if m:
        return "CAN 经典帧 %s kbit/s" % m.group(1)
    return "CAN" if "can" in low else "-"


def can_ids_of(text: str) -> list:
    """只抽带上下文的 CAN ID；挡掉 A2L 地址（0x12345678）、逻辑地址、例程 RID。"""
    out = []
    for m in RE_HEX.finditer(text):
        ctx = text[max(0, m.start() - 56):m.start()] + " " + text[m.end():m.end() + 56]
        if not (RE_CANID_WORD.search(ctx) or RE_CANID_SYM.search(ctx)):
            continue
        try:
            v = int(m.group(1), 16)
        except ValueError:
            continue
        if v > 0x1FFFFFFF or len(m.group(1)) < 3:
            continue
        s = "0x%X" % v
        if s not in out:
            out.append(s)
    return out


def ports_of(text: str) -> list:
    out = []
    for m in RE_PORT_CTX.finditer(text):
        p = m.group(1) or m.group(2) or m.group(3)
        if p and p not in out:
            out.append(p)
    if not out:                       # 有上下文端口时不再用静态表兜底，避免带进顺带提到的端口
        for m in RE_PORT.finditer(text):
            if m.group(1) not in out:
                out.append(m.group(1))
    return out


def ids_and_ports(readme: str, src: str, proto: str = "", name: str = "") -> str:
    text = readme + "\n" + src
    ports = ports_of(text)
    if is_eth(proto, name):
        ips, parts = [], []
        for m in RE_IP.finditer(text):
            if m.group(0) not in ips:
                ips.append(m.group(0))
        if ips:
            parts.append("IP: " + ", ".join(ips[:3]))
        if ports:
            parts.append("端口: " + ", ".join(ports[:4]))
        return " / ".join(parts) if parts else "-"
    ids = can_ids_of(text)
    parts = []
    if ids:
        parts.append("CAN ID: " + ", ".join(ids[:5]) + (" …" if len(ids) > 5 else ""))
    if ports:
        parts.append("端口: " + ", ".join(ports[:3]))
    return " / ".join(parts) if parts else "-"


def _strip_zero_fail(text: str) -> str:
    """把 "fail=0" / "0 FAIL" / "0 failed" 这类"零失败"表述抹掉，避免误判为 FAIL。"""
    t = re.sub(r"(?i)fail\w*\s*[=:]\s*0\b", "", text)
    t = re.sub(r"(?i)\b0\s*fail\w*\b", "", t)
    return t


def _num_mag(v: str) -> float:
    nums = [float(x) for x in re.findall(r"\d+(?:\.\d+)?", v)]
    return max(nums) if nums else 0.0


def score_value(v: str, extra: str = "") -> tuple:
    """(单位权重, 量级)；extra 传表头，让『单位在表头、数据格只有数字』的表也能打分。"""
    text = v + " " + extra
    sc = 0
    for unit, w in UNIT_SCORE:
        if unit in text:
            sc = max(sc, w)
    if sc and _num_mag(v) == 0:
        return (0, 0.0)
    if "PASS" in v.upper() and sc == 0:
        sc = 45                              # 纯结论格（没有硬单位）才按"结论"优先
    return (sc, _num_mag(v))


def _clean(s: str) -> str:
    s = s.replace("**", "").replace("`", "").replace("@@", "")
    return re.sub(r"\s+", " ", s).strip()


def _readme_table(sec: str):
    """解析 section 里的 markdown 表：返回 (最优指标, 结论)。"""
    best = ((0, 0.0), "-", "-", "", "")
    verdict = "-"
    header = []
    prev_pipe = False
    for ln in sec.splitlines():
        s = ln.strip()
        if not s.startswith("|"):
            header, prev_pipe = [], False
            continue
        cells = [_clean(c) for c in s.strip("|").split("|")]
        if not any(cells):
            prev_pipe = True
            continue
        if all(re.fullmatch(r":?-{2,}:?", c) for c in cells if c):
            prev_pipe = True
            continue
        if not prev_pipe:                     # 表格第一行 = 表头
            header = cells
            prev_pipe = True
            continue
        prev_pipe = True
        if not header:
            header = cells
            continue
        label = cells[0]
        if (header and header[0] in ("#", "序号", "No.", "no")) and len(cells) > 2:
            label = cells[1]                    # 首列是行号时用"项目"列当标签
        for c in cells[1:]:
            cu = _strip_zero_fail(c).upper()
            if "PASS" in cu and verdict == "-":
                verdict = "PASS"
            elif "FAIL" in cu and verdict == "-":
                verdict = "FAIL"
        if not label or label in HEAD_WORDS:
            continue
        for idx, val in enumerate(cells[1:], start=1):
            if not val:
                continue
            colname = header[idx] if idx < len(header) else ""
            sc = score_value(val, colname)
            if re.search(r"(?i)(t\s*\(ms\)|idx|index|序号|时间|^\s*#)", colname or ""):
                sc = (sc[0] - 60, sc[1])      # 时间戳/序号列不是指标
            if sc <= best[0]:
                continue
            best = (sc, label, val, colname, header[0] if header else "")
    sc, label, val, colname, first_head = best
    if sc[0] <= 0:
        return ("-", verdict)
    if first_head and first_head not in HEAD_WORDS:
        metric = "%s = %s（%s=%s）" % (colname or "指标", val, first_head, label)
    elif colname and colname not in GENERIC_HEAD:
        metric = "%s（%s）= %s" % (label, colname, val)
    else:
        metric = "%s = %s" % (label, val)
    return (_clean(metric)[:150], verdict)


def _readme_bullets(sec: str):
    """实测节不是表格时（项目符号列表）：挑一条信息量最大（优先带 PASS）的。"""
    best = ((0, 0.0), "-")
    verdict = "-"
    for ln in sec.splitlines():
        t = ln.strip()
        if not re.match(r"^([-*]|\d+\.)\s", t):
            continue
        t = _clean(re.sub(r"^([-*]|\d+\.)\s+", "", t))
        if len(t) < 8:
            continue
        tu = _strip_zero_fail(t).upper()
        if "PASS" in tu and verdict == "-":
            verdict = "PASS"
        elif "FAIL" in tu and verdict == "-":
            verdict = "FAIL"
        sc = score_value(t)
        if "PASS" in t.upper():
            sc = (sc[0] + 40, sc[1])
        if sc > best[0]:
            best = (sc, t[:150])
    return (best[1], verdict)


def readme_claim(readme: str):
    """从 README 的「实测/关键实测结论」章节抽 (指标, 结论)；表格优先，其次列表。"""
    if not readme:
        return ("-", "-")
    blocks = re.split(r"^##\s+", readme, flags=re.M)
    sec = None
    for b in blocks:
        head = b.splitlines()[0] if b.splitlines() else ""
        if ("实测" in head) or ("关键实测" in head) or ("结论" in head):
            sec = b
            break
    if sec is None:
        return ("-", "-")
    if chr(10) in sec:              # 去掉块首标题行（"6. 实测（…）" 不是结论）
        sec = sec.split(chr(10), 1)[1]
    t_metric, t_verdict = _readme_table(sec)
    if t_metric != "-":
        return (t_metric, t_verdict)
    b_metric, b_verdict = _readme_bullets(sec)
    v = t_verdict if t_verdict != "-" else b_verdict
    return (b_metric, v)


def log_claim(log):
    """最新日志里找"像结论"的一行：带单位/PASS、非 boot/banner、非全 0、非调试转储。"""
    if log is None:
        return ("-", "-")
    text = read_text(log, 80000)
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    best = ((0, 0.0), "-")
    verdict = "-"
    for ln in reversed(lines):
        if "PASS" in ln.upper() and verdict == "-":
            m = re.search(r"(\d+)\s*PASS\s*/\s*(\d+)\s*FAIL", ln)
            verdict = ("PASS %s/%s" % (m.group(1), m.group(2))) if m else "PASS"
        if len(ln) > 190 or SKIP_BOOT.search(ln) or SKIP_CONF.search(ln):
            continue
        if "letter:/$" in ln or "RESULT:" in ln or SKIP_DUMP.search(ln):
            continue
        if ln.count("=") >= 5:
            continue
        sc = score_value(ln)
        if sc <= best[0]:
            continue
        best = (sc, _clean(ln)[:150])
    if best[1] != "-" and "FAIL" in _strip_zero_fail(best[1]).upper():
        verdict = "FAIL"
    return (best[1], verdict)


def metric_of(log, readme: str = ""):
    """返回 (结论, 指标, 日志相对路径)；README 实测表优先，日志兜底。"""
    if log is None:
        m, v = readme_claim(readme)
        return (v if v != "-" else "无日志", m, "-")
    try:
        rel = str(log.relative_to(ROOT))
    except ValueError:
        rel = str(log)
    rm, rv = readme_claim(readme)
    lm, lv = log_claim(log)
    metric = rm if rm != "-" else lm
    if rv != "-":
        verdict = rv
    elif lv != "-":
        verdict = lv
    else:
        verdict = "-" if metric != "-" else "无结论行"
    return (verdict, metric, rel)


def host_of(proto: str, readme: str) -> str:
    for path in RE_HOST_PATH.findall(readme):
        p = ROOT / path
        if p.exists():
            bits = path.split("/")
            return bits[0] + "/" + bits[1] if len(bits) > 1 else path
    d = HOST_DIR.get(proto)
    if d and (ROOT / d).is_dir():
        return d + "/"
    return "—（Bench A 板内自测）"


def bench_of(proto: str, text: str) -> str:
    pos = [(text.find("Bench " + b), b) for b in "ABC" if ("Bench " + b) in text]
    if pos:
        return min(pos)[1]
    if proto in ("PTP", "gPTP", "SOME/IP", "XRCE-DDS", "DoIP", "MQTT", "GS-USB"):
        return "C"
    if proto in ("CAN 网关", "多协议共存"):
        return "B"
    low = text.lower()
    if "vcan0" in low and "cannelloni" in low and "bench b" in low:
        return "B"
    return "A"


def esc(s: str) -> str:
    return s.replace("|", "\\|").replace("\n", " ")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="生成应用层例程证据矩阵")
    ap.add_argument("--check", action="store_true", help="只打印统计")
    ap.add_argument("--stdout", action="store_true", help="打印 Markdown 到标准输出")
    args = ap.parse_args(argv)

    dirs = sorted([p for p in APPS.iterdir() if p.is_dir() and not p.name.startswith("_")])
    rows = []
    proto_count = {}
    for d in dirs:
        name = d.name
        readme = read_text(d / "README.md")
        src = "\n".join(read_text(p) for p in sorted((d / "src").glob("*.c")))
        proto, sub = proto_of(name)
        proto_count[proto] = proto_count.get(proto, 0) + 1
        log = latest_log(name)
        verdict, metric, logrel = metric_of(log, readme)
        trans = transport_of(proto, name, readme + src)
        rows.append(dict(name=name, path=str(d.relative_to(ROOT)),
                         proto="%s（%s）" % (proto, sub), transport=trans,
                         ids=ids_and_ports(readme, src, proto, name),
                         host=host_of(proto, readme), metric=metric, verdict=verdict,
                         log=logrel, bench=bench_of(proto, readme + "\n" + src),
                         has_readme=bool(readme)))

    if args.check:
        print("%d 个应用层例程，协议分布：" % len(rows))
        for k, v in sorted(proto_count.items()):
            print("  %s: %d" % (k, v))
        noreg = [r["name"] for r in rows if not r["has_readme"]]
        nolog = [r["name"] for r in rows if r["log"] == "-"]
        nometric = [r["name"] for r in rows if r["metric"] == "-"]
        print("缺 README: %s" % (noreg or "无"))
        print("缺日志: %s" % (nolog or "无"))
        print("指标为空: %s" % (nometric or "无"))
        for r in rows:
            print("  %-20s %-30s %-26s %s" % (r["name"], r["proto"][:30],
                                              r["transport"][:26], r["metric"][:78]))
        return 0

    ts = datetime.now().strftime("%Y-%m-%d %H:%M")
    bt = chr(96)

    def ib(x):
        return bt + x + bt

    out = []
    out.append("# 应用层例程证据矩阵（app-matrix）")
    out.append("")
    out.append("> 本文件由 " + ib("tools/gen_app_matrix.py") + " 自动生成，**请勿手改**；"
               "数据来源：" + ib("samples/app/*/README.md") + "（实测/关键实测结论表格优先）、"
               + ib("samples/app/*/src/*.c") + "、" + ib("logs/*-<例程>.log") + "（最新一份兜底）。")
    out.append("")
    out.append("| 项 | 值 |")
    out.append("|---|---|")
    out.append("| 生成时间 | %s |" % ts)
    out.append("| 例程数 | %d（" % len(rows) + ib("samples/app/") + "） |")
    out.append("| 协议分布 | " + "，".join("%s %d" % (k, v) for k, v in sorted(proto_count.items())) + " |")
    out.append("| bench 说明 | A=板内双节点自测；B=主机 vcan0 + cannelloni 网关；C=以太网直连上位机 |")
    out.append("")
    out.append("## 1. 证据矩阵")
    out.append("")
    out.append("| 例程 | 协议 | 传输 | CAN ID / IP:端口 | 上位机工具 | 关键实测指标 | 结论 | 证据日志 | bench |")
    out.append("|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        name = r["name"] + ("" if r["has_readme"] else "（缺 README）")
        out.append("| " + ib(name) + " | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            esc(r["proto"]), esc(r["transport"]), esc(r["ids"]), esc(r["host"]),
            esc(r["metric"]), esc(r["verdict"]), esc(r["log"]), r["bench"]))
    out.append("")
    out.append("## 2. 覆盖统计")
    out.append("")
    out.append("| 协议 | 例程 | README | 有实测日志 | bench A/B/C |")
    out.append("|---|---|---|---|---|")
    for k in sorted(proto_count):
        grp = [r for r in rows if r["proto"].startswith(k)]
        out.append("| %s | %d | %d | %d | %s |" % (
            k, len(grp), sum(1 for r in grp if r["has_readme"]),
            sum(1 for r in grp if r["log"] != "-"),
            "/".join(str(sum(1 for r in grp if r["bench"] == b)) for b in "ABC")))
    out.append("")
    out.append("## 3. 缺失与后续")
    out.append("")
    missing_readme = [r["path"] for r in rows if not r["has_readme"]]
    missing_log = [r["name"] for r in rows if r["log"] == "-"]
    missing_metric = [r["name"] for r in rows if r["metric"] == "-"]

    def joinc(items):
        return "、".join(ib(x) for x in items) if items else "无"

    out.append("- 缺 README 的例程：" + joinc(missing_readme))
    out.append("- 无实测日志的例程：" + joinc(missing_log))
    out.append("- 指标仍为空的例程（README 无实测表且日志无结论行）：" + joinc(missing_metric))
    out.append("- 证据日志取 " + ib("logs/") + " 下该例程最新一条；重跑后重新生成本文件即可刷新。")
    out.append("")
    text = "\n".join(out) + "\n"

    if args.stdout:
        print(text)
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text, encoding="utf-8")
    print("wrote %s (%d samples, %d lines)" % (OUT.relative_to(ROOT), len(rows), len(text.splitlines())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
