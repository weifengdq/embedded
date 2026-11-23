#!/usr/bin/env python3
"""gen_a2l.py —— 由 ELF 符号表 + 变量描述表生成 ASAM MCD-2 MC (A2L 1.7.1) 文件

为什么要自己生成：A2L 里的 MEASUREMENT/CHARACTERISTIC 地址必须和烧进 Flash 的 ELF
里的变量绝对地址**逐字节一致**，手工抄地址是错一个字节就"标定无效"的经典事故。
本脚本直接解析 ELF32 的 .symtab，把符号名映射成地址，再按变量描述表（JSON）
生成 MEASUREMENT / CHARACTERISTIC / AXIS_PTS / RECORD_LAYOUT / COMPU_METHOD
以及 IF_DATA XCP 的 PROTOCOL_LAYER / DAQ / EVENT 描述。

用法：
  # 1) 生成 A2L（地址直接来自 ELF）
  python3 tools/gen_a2l.py --elf build/xcp_can_daq-debug/output/demo.elf \
          --vars tools/a2l/xcp_can_daq.json \
          --out samples/app/xcp_can_daq/xcp_can_daq.a2l

  # 2) 校验（DanielT/a2ltool，见 tools/a2l/README.md）
  a2ltool --check xcp_can_daq.a2l

  # 3) 只查符号地址/大小（调试用）
  python3 tools/gen_a2l.py --elf ... --list-syms g_adc_raw g_tick_cnt

选项：
  --no-ifdata    不生成 IF_DATA XCP 块（有些老工具对 IF_DATA 挑剔）
  --strict       符号缺失或大小不符时报错退出（默认只告警）
"""
from __future__ import annotations

import argparse
import json
import struct
import sys

# --------------------------------------------------------------------------
# ELF32 符号表解析（RISC-V 小端；不依赖 readelf/nm，任何机器都能跑）
# --------------------------------------------------------------------------

SHT_SYMTAB = 2


class Elf32:
    def __init__(self, path: str) -> None:
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:4] != b"\x7fELF":
            raise SystemExit(f"{path}: 不是 ELF 文件")
        if d[4] != 1:
            raise SystemExit(f"{path}: 只支持 ELF32（当前是 class={d[4]}）")
        if d[5] != 1:
            raise SystemExit(f"{path}: 只支持小端 ELF")
        (self.e_shoff,) = struct.unpack_from("<I", d, 0x20)
        (self.e_shentsize,) = struct.unpack_from("<H", d, 0x2E)
        (self.e_shnum,) = struct.unpack_from("<H", d, 0x30)
        (self.e_shstrndx,) = struct.unpack_from("<H", d, 0x32)

    def _sections(self):
        out = []
        for i in range(self.e_shnum):
            off = self.e_shoff + i * self.e_shentsize
            name, stype, flags, addr, offset, size, link, info, align, entsize = \
                struct.unpack_from("<IIIIIIIIII", self.data, off)
            out.append(dict(idx=i, name_off=name, type=stype, addr=addr,
                            offset=offset, size=size, link=link, entsize=entsize))
        return out

    def _str(self, base: int, off: int) -> str:
        end = self.data.index(b"\x00", base + off)
        return self.data[base + off:end].decode("utf-8", "replace")

    def symbols(self) -> dict:
        """返回 {name: (value, size, shndx)}"""
        secs = self._sections()
        strtab_base = None
        for s in secs:
            if s["type"] == SHT_SYMTAB:
                strtab_base = secs[s["link"]]["offset"]
                syms = {}
                ent = s["entsize"] or 16
                for i in range(s["size"] // ent):
                    off = s["offset"] + i * ent
                    name_off, value, size, info, other, shndx = \
                        struct.unpack_from("<IIIBBH", self.data, off)
                    if name_off == 0:
                        continue
                    name = self._str(strtab_base, name_off)
                    if shndx == 0:      # SHN_UNDEF
                        continue
                    syms[name] = (value, size, shndx)
                return syms
        raise SystemExit("ELF 里没有 .symtab（是否 strip 过？）")


# --------------------------------------------------------------------------
# A2L 数据类型 -> (RECORD_LAYOUT 名, 字节数)
# --------------------------------------------------------------------------

TYPES = {
    "UBYTE": ("RL_UBYTE", 1),
    "SBYTE": ("RL_SBYTE", 1),
    "UWORD": ("RL_UWORD", 2),
    "SWORD": ("RL_SWORD", 2),
    "ULONG": ("RL_ULONG", 4),
    "SLONG": ("RL_SLONG", 4),
    "A_UINT64": ("RL_A_UINT64", 8),
    "A_INT64": ("RL_A_INT64", 8),
    "FLOAT32_IEEE": ("RL_FLOAT32", 4),
    "FLOAT64_IEEE": ("RL_FLOAT64", 8),
}

# COMPU_METHOD：raw（恒等）、线性缩放、枚举表
def cm_raw(name):
    return ('/begin COMPU_METHOD %s "raw value" RAT_FUNC "%%8.0" "" '
            'COEFFS 0 1 0 0 0 1 /end COMPU_METHOD' % name)


def cm_linear(name, factor, offset, unit, fmt="%8.3"):
    return ('/begin COMPU_METHOD %s "linear" RAT_FUNC "%s" "%s" '
            'COEFFS 0 %g %g 0 0 1 /end COMPU_METHOD' % (name, fmt, unit, factor, offset))


def cm_tab(name, unit, pairs):
    lines = ['/begin COMPU_METHOD %s "enumerated" TAB_VERB "%%3.0" "%s"' % (name, unit),
             '  COMPU_TAB_REF %s.TAB' % name,
             '/end COMPU_METHOD',
             '/begin COMPU_TAB %s.TAB "enumerated" TAB_VERB %d' % (name, len(pairs))]
    for (k, v) in pairs:
        lines.append('  %g "%s"' % (k, v))
    lines.append('/end COMPU_TAB')
    return "\n".join(lines)


# --------------------------------------------------------------------------
# 生成器
# --------------------------------------------------------------------------

def num(v) -> int:
    """把 JSON 里的 12 / "0x01200000" 统一转成整数"""
    if isinstance(v, int):
        return v
    return int(str(v), 0)


class A2LGen:
    def __init__(self, doc: dict, syms: dict, strict: bool, with_ifdata: bool):
        self.doc = doc
        self.syms = syms
        self.strict = strict
        self.with_ifdata = with_ifdata
        self.errors = 0
        self.warnings = 0
        self.lines = []

    def emit(self, s=""):
        self.lines.append(s)

    def addr_of(self, var: dict) -> int:
        sym = var.get("symbol") or var["name"]
        if "address" in var:                 # 允许手工钉死地址（特殊场景）
            return int(var["address"], 0)
        if sym not in self.syms:
            self.errors += 1
            raise SystemExit("错误：ELF 里找不到符号 %r（generate 中止）" % sym)
        addr, size, _ = self.syms[sym]
        typ = var.get("type", "ULONG")
        want = TYPES[typ][1] * int(var.get("points", 1))
        if size and size != want:
            msg = "符号 %s 大小 %d != 声明类型 %s(%d)" % (sym, size, typ, want)
            if self.strict:
                self.errors += 1
                print("错误：" + msg, file=sys.stderr)
            else:
                self.warnings += 1
                print("告警：" + msg, file=sys.stderr)
        return addr

    # ---------------- 各段 ----------------

    def header(self):
        d = self.doc
        self.emit("/* ------------------------------------------------------------------")
        self.emit(" * 本文件由 tools/gen_a2l.py 自动生成：地址来自 ELF 符号表，不要手工修改。")
        self.emit(" *   生成：%s" % d.get("generated_by", "tools/gen_a2l.py"))
        self.emit(" *   工程：%s" % d["project"]["name"])
        self.emit(" *   传输：XCP on CAN  CMD=0x%03X  RES/DAQ=0x%03X" %
                  (d["xcp"]["can_cmd_id"], d["xcp"]["can_res_id"]))
        self.emit(" * ------------------------------------------------------------------ */")
        self.emit("ASAP2_VERSION 1 71")
        p = d["project"]
        self.emit('/begin PROJECT %s "%s"' % (p["name"], p.get("comment", "")))
        # A2L 1.7.1：PROJECT_NO 的值是标识符（bare word），VERSION 是字符串
        self.emit('  /begin HEADER "%s" VERSION "%s" PROJECT_NO %s'
                  % (p.get("comment", ""), p.get("version", "1.0"), p.get("project_no", "PRJ1")))
        self.emit("  /end HEADER")

    def module_begin(self):
        d = self.doc
        m = d["module"]
        self.emit('  /begin MODULE %s "%s"' % (m["name"], m.get("comment", "")))
        self.emit('    /begin MOD_COMMON ""')
        self.emit("      BYTE_ORDER MSB_LAST            /* RISC-V 小端：低字节在前 */")
        self.emit("      ALIGNMENT_BYTE 1")
        self.emit("      ALIGNMENT_WORD 2")
        self.emit("      ALIGNMENT_LONG 4")
        self.emit("      ALIGNMENT_FLOAT32_IEEE 4")
        self.emit("      ALIGNMENT_FLOAT64_IEEE 8")
        self.emit("      ALIGNMENT_INT64 8")
        self.emit("    /end MOD_COMMON")
        # MOD_PAR：给出内存段，方便工具做地址合法性检查
        self.emit('    /begin MOD_PAR ""')
        for seg in d.get("memory_segments", []):
            # 注意：MEMORY_SEGMENT 的位置参数到 SIZE 为止；再往后是可选块
            # （IF_DATA / 扩展偏移量），多写数字会被 a2ltool 判语法错。
            self.emit('      /begin MEMORY_SEGMENT %s "%s" %s %s %s 0x%08X 0x%X 0 0 0 0 0'
                      % (seg["name"], seg.get("comment", ""), seg.get("prg_type", "DATA"),
                         seg.get("memory_type", "RAM"), seg.get("attribute", "INTERN"),
                         int(str(seg["start"]), 0), int(str(seg["size"]), 0)))
            self.emit("      /end MEMORY_SEGMENT")
        self.emit("    /end MOD_PAR")

    def record_layouts(self):
        used = set()
        for v in self.doc.get("measurements", []):
            used.add(v.get("type", "ULONG"))
        for v in self.doc.get("characteristics", []):
            used.add(v.get("type", "ULONG"))
        for v in self.doc.get("axes", []):
            used.add(v.get("type", "UWORD"))
        for t in sorted(used):
            rl, _ = TYPES[t]
            self.emit("    /begin RECORD_LAYOUT %s" % rl)
            self.emit("      FNC_VALUES 1 %s COLUMN_DIR DIRECT" % t)
            self.emit("    /end RECORD_LAYOUT")
        # AXIS_PTS 必须引用带 AXIS_PTS_X 的 RECORD_LAYOUT（a2ltool 一致性检查强制）
        axis_types = sorted({v.get("type", "UWORD") for v in self.doc.get("axes", [])})
        if axis_types:
            for t in axis_types:
                self.emit("    /begin RECORD_LAYOUT RL_AXIS_%s" % t)
                self.emit("      AXIS_PTS_X 1 %s INDEX_INCR DIRECT" % t)
                self.emit("    /end RECORD_LAYOUT")

    def compu_methods(self):
        self.emit("    " + cm_raw("cm_ident") + "   /* 恒等换算 */")
        self.emit("    " + cm_linear("cm_adc_volt", 3300.0 / 65535.0, 0.0, "mV", "%8.1"))
        self.emit("    " + cm_linear("cm_permille", 0.1, 0.0, "%", "%6.1"))
        for name, spec in self.doc.get("enumerations", {}).items():
            self.emit("    " + cm_tab(name, spec.get("unit", ""), spec["pairs"]))

    def measurements(self):
        for v in self.doc.get("measurements", []):
            addr = self.addr_of(v)
            rl, _ = TYPES[v.get("type", "ULONG")]
            self.emit('    /begin MEASUREMENT %s "%s"' % (v["name"], v.get("comment", "")))
            self.emit("      %s %s 0 0 %g %g"
                      % (v.get("type", "ULONG"), v.get("conv", "cm_ident"),
                         float(v.get("min", 0)), float(v.get("max", 0))))
            self.emit("      ECU_ADDRESS 0x%08X" % addr)
            if v.get("format"):
                self.emit('      FORMAT "%s"' % v["format"])
            if v.get("unit"):
                self.emit('      UNIT "%s"' % v["unit"])
            if v.get("event") is not None:
                self.emit("      /* 建议采集事件：EVT(%d) */" % v["event"])
            self.emit("    /end MEASUREMENT")

    def characteristics(self):
        for v in self.doc.get("characteristics", []):
            addr = self.addr_of(v)
            rl, _ = TYPES[v.get("type", "ULONG")]
            ctype = v.get("kind", "VALUE")
            if ctype == "VALUE":
                self.emit('    /begin CHARACTERISTIC %s "%s" VALUE 0x%08X %s 0 %s %g %g'
                          % (v["name"], v.get("comment", ""), addr, rl,
                             v.get("conv", "cm_ident"), float(v.get("min", 0)),
                             float(v.get("max", 0))))
                if v.get("format"):
                    self.emit('      FORMAT "%s"' % v["format"])
                if v.get("unit"):
                    self.emit('      UNIT "%s"' % v["unit"])
                self.emit("    /end CHARACTERISTIC")
            elif ctype == "CURVE":
                self.emit('    /begin CHARACTERISTIC %s "%s" CURVE 0x%08X %s 0 %s %g %g'
                          % (v["name"], v.get("comment", ""), addr, rl,
                             v.get("conv", "cm_ident"), float(v.get("min", 0)),
                             float(v.get("max", 0))))
                self.emit("      /begin AXIS_DESCR COM_AXIS NO_INPUT_QUANTITY %s %d 0.0 0.0"
                          % (v.get("axis_conv", "cm_ident"), int(v.get("points", 8))))
                self.emit("        AXIS_PTS_REF %s" % v["axis_ref"])
                self.emit("      /end AXIS_DESCR")
                if v.get("format"):
                    self.emit('      FORMAT "%s"' % v["format"])
                self.emit("    /end CHARACTERISTIC")
            else:
                raise SystemExit("不支持的 CHARACTERISTIC 类型 %r" % ctype)

    def axes(self):
        for v in self.doc.get("axes", []):
            addr = self.addr_of(v)
            rl, _ = TYPES[v.get("type", "UWORD")]
            # 位置参数：name 标识, address, input_quantity, deposit(record_layout),
            # max_diff, conversion, max_axis_points, lower_limit, upper_limit
            axis_rl = "RL_AXIS_%s" % v.get("type", "UWORD")
            self.emit('    /begin AXIS_PTS %s "%s" 0x%08X %s %s 0 %s %d %g %g'
                      % (v["name"], v.get("comment", ""), addr,
                         v.get("input_quantity", "NO_INPUT_QUANTITY"), axis_rl,
                         v.get("conv", "cm_ident"), int(v.get("points", 8)),
                         float(v.get("min", 0)), float(v.get("max", 0))))
            self.emit('      FORMAT "%s"' % v.get("format", "%6.3"))
            self.emit("    /end AXIS_PTS")

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
        self.emit("        0x%X" % int(daq.get("max_odt_entry_size", 7)))
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
        self.emit("      /begin XCP_ON_CAN")
        self.emit("        0x%X" % int(x.get("version", 0x100)))
        self.emit("        CAN_ID_MASTER 0x%X" % int(x["can_cmd_id"]))
        self.emit("        CAN_ID_SLAVE 0x%X" % int(x["can_res_id"]))
        self.emit("        BAUDRATE 0x%X" % int(x.get("baudrate", 500000)))
        self.emit("        SAMPLE_POINT 0x%X" % int(x.get("sample_point", 0x57)))
        self.emit("        SAMPLE_RATE SINGLE")
        self.emit("        BTL_CYCLES 0x%X" % int(x.get("btl_cycles", 0xA0)))
        self.emit("        SJW 0x1")
        self.emit("      /end XCP_ON_CAN")
        self.emit("    /end IF_DATA")

    def footer(self):
        self.emit("  /end MODULE")
        self.emit("/end PROJECT")

    def generate(self) -> str:
        self.header()
        self.module_begin()
        self.record_layouts()
        self.compu_methods()
        self.measurements()
        self.characteristics()
        self.axes()
        if self.with_ifdata:
            self.ifdata_xcp()
        self.footer()
        return "\n".join(self.lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description="由 ELF 符号表生成 A2L（ASAM MCD-2 MC 1.7.1）")
    ap.add_argument("--elf", required=True, help="编译产物 ELF（含 .symtab）")
    ap.add_argument("--vars", help="变量描述表 JSON")
    ap.add_argument("--out", help="输出 A2L 路径（缺省打印到 stdout）")
    ap.add_argument("--no-ifdata", action="store_true", help="不生成 IF_DATA XCP 块")
    ap.add_argument("--strict", action="store_true", help="符号大小不符时直接报错")
    ap.add_argument("--list-syms", nargs="*", help="只打印这些符号的地址/大小")
    args = ap.parse_args()

    elf = Elf32(args.elf)
    syms = elf.symbols()

    if args.list_syms is not None:
        names = args.list_syms or sorted(syms)[:50]
        print("%-32s %10s %6s" % ("symbol", "address", "size"))
        for n in names:
            if n in syms:
                a, s, _ = syms[n]
                print("%-32s 0x%08X %6d" % (n, a, s))
            else:
                print("%-32s %10s" % (n, "(missing)"))
        return 0

    if not args.vars:
        ap.error("生成 A2L 需要 --vars 变量描述表")
    with open(args.vars, "r", encoding="utf-8") as f:
        doc = json.load(f)

    gen = A2LGen(doc, syms, args.strict, not args.no_ifdata)
    text = gen.generate()
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
        n_meas = len(doc.get("measurements", []))
        n_char = len(doc.get("characteristics", []))
        n_axis = len(doc.get("axes", []))
        n_evt = len(doc.get("events", []))
        print("已生成 %s：MEASUREMENT %d / CHARACTERISTIC %d / AXIS_PTS %d / EVENT %d，"
              "告警 %d 条" % (args.out, n_meas, n_char, n_axis, n_evt, gen.warnings))
    else:
        sys.stdout.write(text)
    return 0 if gen.errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
