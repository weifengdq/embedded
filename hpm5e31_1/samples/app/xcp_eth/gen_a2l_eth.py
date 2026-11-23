#!/usr/bin/env python3
"""gen_a2l_eth.py —— 为 samples/app/xcp_eth 生成 XCPonEth 版 A2L

为什么不直接用 tools/gen_a2l.py：那个脚本的 IF_DATA XCP 传输层块固定写
XCP_ON_CAN（CAN_ID_MASTER/CAN_ID_SLAVE/BAUDRATE...），对以太网是错的。
这里 import 它、复用 ELF 符号解析和 MEASUREMENT/CHARACTERISTIC 生成，
只覆盖两处：
  1) header()：文件头注释写成 XCP on Ethernet(UDP)，不列 CAN ID；
  2) ifdata_xcp()：传输层块用 A2L 1.7.1 的 XCP_ON_UDP_IP
     （字段定义见 DanielT/a2ltool/src/ifdata.rs:695 struct UDP_IP_Parameters）。
其余（地址、类型、换算、EVENT）与 CAN 版完全同源，避免"地址抄错"。

用法：
  python3 samples/app/xcp_eth/gen_a2l_eth.py \
      --elf build/xcp_eth-debug/output/demo.elf \
      --vars samples/app/xcp_eth/xcp_eth_vars.json \
      --out samples/app/xcp_eth/xcp_eth.a2l
"""
from __future__ import annotations

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(REPO, "tools"))

import gen_a2l  # noqa: E402

IP = "192.168.2.100"
PORT = 5555


class EthA2LGen(gen_a2l.A2LGen):
    def header(self):
        d = self.doc
        p = d["project"]
        self.emit("/* ------------------------------------------------------------------")
        self.emit(" * 本文件由 samples/app/xcp_eth/gen_a2l_eth.py 自动生成：")
        self.emit(" *   地址来自 ELF 符号表（复用 tools/gen_a2l.py），不要手工修改。")
        self.emit(" *   工程：%s" % p["name"])
        self.emit(" *   传输：XCP on Ethernet (UDP)  %s:%u" % (IP, PORT))
        self.emit(" * ------------------------------------------------------------------ */")
        self.emit("ASAP2_VERSION 1 71")
        self.emit('/begin PROJECT %s "%s"' % (p["name"], p.get("comment", "")))
        self.emit('  /begin HEADER "%s" VERSION "%s" PROJECT_NO %s'
                  % (p.get("comment", ""), p.get("version", "1.0"), p.get("project_no", "PRJ1")))
        self.emit("  /end HEADER")

    def ifdata_xcp(self):
        d = self.doc
        x = d["xcp"]
        daq = d.get("daq", {})
        ts = x.get("timestamp", {})
        self.emit("    /begin IF_DATA XCP")
        self.emit("      /begin PROTOCOL_LAYER")
        self.emit("        0x0104 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x00 0x00 0x00"
                  % (x["max_cto"], x["max_dto"] & 0xFF, x["max_dto"] >> 8,
                     x["max_cto"], x["max_dto"] & 0xFF, x["max_dto"] >> 8))
        self.emit("        BYTE_ORDER_MSB_LAST")
        self.emit("        ADDRESS_GRANULARITY_BYTE")
        for c in x.get("optional_cmds", []):
            self.emit("        OPTIONAL_CMD %s" % c)
        self.emit("        COMMUNICATION_MODE_SUPPORTED BLOCK")
        self.emit("      /end PROTOCOL_LAYER")
        self.emit("      /begin DAQ")
        self.emit("        %s" % daq.get("config", "DYNAMIC"))
        self.emit("        0x%X" % int(daq.get("max_daq", 4)))
        self.emit("        0x%X" % int(daq.get("min_daq", 0)))
        self.emit("        OPTIMISATION_TYPE_DEFAULT")
        self.emit("        ADDRESS_EXTENSION_FREE")
        self.emit("        IDENTIFICATION_FIELD_TYPE_ABSOLUTE")
        self.emit("        GRANULARITY_ODT_ENTRY_SIZE_DAQ_BYTE")
        self.emit("        0x%X" % int(daq.get("max_odt_entry_size", 255)))
        self.emit("        OVERLOAD_INDICATION_EVENT")
        if daq.get("prescaler_supported", True):
            self.emit("        PRESCALER_SUPPORTED")
        if ts:
            self.emit("        /begin TIMESTAMP_SUPPORTED")
            self.emit("          0x%X" % int(ts.get("ticks", 1)))
            self.emit("          %s" % ts.get("size", "SIZE_DWORD"))
            self.emit("          %s" % ts.get("unit", "UNIT_1US"))
            self.emit("        /end TIMESTAMP_SUPPORTED")
        for e in d.get("events", []):
            self.emit('        /begin EVENT "%s" "%s" 0x%X %s 0x%X 0x%X %s 0x%X'
                      % (e["name"], e.get("comment", ""), int(e["number"]),
                         e.get("direction", "DAQ"), int(e.get("max_daq_list", 4)),
                         int(e.get("cycle", 1)), e.get("unit", "1MS"),
                         int(e.get("priority", 0))))
            self.emit("        /end EVENT")
        self.emit("      /end DAQ")
        self.emit("      /begin XCP_ON_UDP_IP")
        self.emit("        0x%X" % int(x.get("version", 0x100)))
        self.emit("        PORT %u" % int(x.get("port", PORT)))
        self.emit('        ADDRESS "%s"' % x.get("address", IP))
        self.emit("        MAX_BUS_LOAD %u" % int(x.get("max_bus_load", 100)))
        self.emit("        MAX_BIT_RATE %u" % int(x.get("max_bit_rate", 1000)))
        self.emit("      /end XCP_ON_UDP_IP")
        self.emit("    /end IF_DATA")


def main() -> int:
    ap = argparse.ArgumentParser(description="生成 XCPonEth 版 A2L（地址来自 ELF）")
    ap.add_argument("--elf", required=True, help="编译产物 ELF（含 .symtab）")
    ap.add_argument("--vars", required=True, help="变量描述表 JSON")
    ap.add_argument("--out", required=True, help="输出 A2L 路径")
    ap.add_argument("--strict", action="store_true", help="符号大小不符时报错")
    args = ap.parse_args()

    elf = gen_a2l.Elf32(args.elf)
    syms = elf.symbols()
    with open(args.vars, "r", encoding="utf-8") as f:
        doc = json.load(f)
    gen = EthA2LGen(doc, syms, args.strict, True)
    text = gen.generate()
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(text)
    print("已生成 %s：MEASUREMENT %d / CHARACTERISTIC %d / EVENT %d，告警 %d 条"
          % (args.out, len(doc.get("measurements", [])),
             len(doc.get("characteristics", [])), len(doc.get("events", [])), gen.warnings))
    return 0 if gen.errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
