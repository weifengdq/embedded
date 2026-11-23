#!/usr/bin/env python3
"""xcp_common.py —— host/xcp 公共部分：A2L 地址解析 + pyxcp Master 构造

为什么要解析 A2L：XCP 从站不维护符号表，主机要读哪个变量必须知道它的绝对地址，
这个地址只能来自 A2L（而 A2L 的地址由 tools/gen_a2l.py 从 ELF 符号表生成，
与烧进 Flash 的镜像逐字节一致）。所以上位机的工作流是：
    ELF -> gen_a2l.py -> A2L -> pyxcp -> 地址 -> SET_MTA/SHORT_UPLOAD/DAQ

只依赖标准库 + pyxcp + python-can。
"""
from __future__ import annotations

import os
import re
import sys
import time

# pyxcp 的日志/时间相关性代码会用 datetime 的本地时区；容器/CI 里 TZ 常是 "CST"，
# zoneinfo 不认（UnknownTimeZoneError: 'CST'），所以导入 pyxcp 之前先把 TZ 固定成 UTC。
os.environ.setdefault("TZ", "UTC")
try:
    time.tzset()
except AttributeError:      # 非 POSIX 平台没有 tzset
    pass

MEAS_RE = re.compile(
    r"/begin\s+MEASUREMENT\s+(\w+)\s+\"[^\"]*\"\s+(\w+)\s+(\w+)\s+[-0-9.eE+]+\s+[-0-9.eE+]+\s+[-0-9.eE+]+\s+[-0-9.eE+]+(.*?)/end\s+MEASUREMENT",
    re.S,
)
CHAR_RE = re.compile(
    r"/begin\s+CHARACTERISTIC\s+(\w+)\s+\"[^\"]*\"\s+\w+\s+0x([0-9A-Fa-f]+)\s+(\w+)(.*?)/end\s+CHARACTERISTIC",
    re.S,
)
ECU_ADDR_RE = re.compile(r"ECU_ADDRESS\s+0x([0-9A-Fa-f]+)")

TYPE_SIZE = {
    "UBYTE": 1, "SBYTE": 1, "UWORD": 2, "SWORD": 2,
    "ULONG": 4, "SLONG": 4, "A_UINT64": 8, "A_INT64": 8,
    "FLOAT32_IEEE": 4, "FLOAT64_IEEE": 8,
}

# CHARACTERISTIC 的 A2L 语法里紧跟地址的是 RECORD_LAYOUT（RL_xxx），不是数据类型；
# 查 sizeof 前要映射回类型（映射表与 tools/gen_a2l.py 的 TYPES 一致）。
LAYOUT2TYPE = {
    "RL_UBYTE": "UBYTE", "RL_SBYTE": "SBYTE", "RL_UWORD": "UWORD", "RL_SWORD": "SWORD",
    "RL_ULONG": "ULONG", "RL_SLONG": "SLONG", "RL_A_UINT64": "A_UINT64",
    "RL_A_INT64": "A_INT64", "RL_FLOAT32": "FLOAT32_IEEE", "RL_FLOAT64": "FLOAT64_IEEE",
}


def load_a2l(path):
    """返回 {变量名: {address, type, size, kind}}"""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    out = {}
    for m in MEAS_RE.finditer(text):
        name, typ = m.group(1), m.group(2)
        addr_m = ECU_ADDR_RE.search(m.group(4))
        if addr_m:
            out[name] = {"address": int(addr_m.group(1), 16), "type": typ,
                         "size": TYPE_SIZE.get(typ, 4), "kind": "measurement"}
    for m in CHAR_RE.finditer(text):
        name = m.group(1)
        addr = int(m.group(2), 16)
        layout = m.group(3)
        typ = LAYOUT2TYPE.get(layout, "ULONG")
        out[name] = {"address": addr, "type": typ,
                     "size": TYPE_SIZE.get(typ, 4), "kind": "characteristic"}
    return out


def make_master(interface="socketcan", channel="vcan0",
                cmd_id=0x555, res_id=0x556, bitrate=500000):
    """按 pyxcp 0.30 的 traitlets 配置 API 建一个同步 Master（不读配置文件）"""
    try:
        from pyxcp.config import create_application_from_config
        from pyxcp import Master
    except ImportError as exc:
        sys.exit("缺少 pyxcp：请用 $HOME/pyenv/bin/python 运行（%s）" % exc)

    cfg = {
        "Transport": {
            "CAN": {
                "interface": interface,
                "channel": channel,
                "can_id_master": cmd_id,
                "can_id_slave": res_id,
                "max_dlc_required": True,
                "fd": False,
                "bitrate": bitrate,
                "use_default_listener": True,   # 用 pyxcp 自带的 CAN 监听线程（否则 request() 永远等不到 RES）
            }
        },
        "General": {"loglevel": "ERROR"},
    }
    app = create_application_from_config(cfg)
    app.transport.layer = "CAN"        # 关键一：不设这个 pyxcp 会报 "No transport-layer selected"

    # 关键二：把 python-can 的 Bus 作为"用户提供的传输接口"交给 pyxcp。
    # 让 pyxcp 自己 new Bus 时，其 PythonCanWrapper 在没有默认 listener 的场景下
    # 会抛 AttributeError: 'PythonCanWrapper' object has no attribute 'can_interface'；
    # 传入现成 Bus 后 pyxcp 直接用它收发（set_filters 由 pyxcp 调用），实测可用。
    import can
    bus = can.Bus(interface=interface, channel=channel)
    return Master(app.transport.layer, config=app, transport_layer_interface=bus)


def check_dependencies():
    missing = []
    for mod in ("can", "pyxcp"):
        try:
            __import__(mod)
        except ImportError:
            missing.append(mod)
    if missing:
        sys.exit("缺少依赖 %s，请用 $HOME/pyenv/bin/python 运行" % ", ".join(missing))


def fmt_addr(v):
    return "0x%08X" % v
