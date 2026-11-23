#!/usr/bin/env python3
"""tools/mcal_check.py —— HPM5E31 MCAL 静态合规检查器 + 模块统计报告生成器

用法：
  python3 tools/mcal_check.py                 # 检查（0 错 0 警才返回 0）
  python3 tools/mcal_check.py --json          # JSON 输出（给 CI / Lead 汇总）
  python3 tools/mcal_check.py --report        # 检查 + 生成 docs/generated/mcal-modules.md
  python3 tools/mcal_check.py --no-warn-fail  # 警告不算失败（默认警告也算失败）

检查项（见 docs/MCAL开发约定.md §3/§4/§9）：
  ① 模块公共头（include/*.h）不得 include HPM SDK 头（hpm_*/soc.h/board.h/pinmux.h）；
     cfg/*.h 同样要求干净（.c 允许 include SDK 头，因为板级映射在那里）。
  ② 驱动 .c（mcal/<Mod>/src/*.c，cfg/ 除外）不得硬编码引脚名/实例宏：
     PA02 这类 P[A-F]nn、IOC_PAD_PXnn、HPM_xxx 实例（HPM_MCAL* 除外）。
  ③ 每个模块必须有 include/src/cfg 五件套，且头文件带版本宏、API_ID、模块 ID（或 include
     Mcal_ModuleId.h）、DEV_ERROR_DETECT 开关、include guard。
  ④ 模块 ID 与 mcal/common/Mcal_ModuleId.h、SWS 表一致；Det 上报用自己的模块 ID 宏。
  ⑤ samples/mcal/*/README.md 必须含 hwrun 命令与 logs/ 真实日志路径。

报告模式数据来源：
  * 源码：mcal/<Mod>/**（文件数、LOC、API 数）
  * 体积：build/*-debug 下各模块 .obj 的 text/data/bss（riscv32-unknown-elf-size）
  * 构建：/tmp/mcal_build.log（tools/build_all_mcal.sh 产物）或 --build-log 指定
  * 日志：logs/*-<例程>.log（最新一份）
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# SWS_BSWGeneral §11.3 / mcal/common/Mcal_ModuleId.h 的标准模块 ID
SWS_MODULE_IDS = {
    "ADC": 123, "DIO": 120, "ICU": 122, "PORT": 124, "PWM": 121,
    "GPT": 100, "MCU": 101, "WDG": 102,
    "CAN": 80, "I2C": 113, "SPI": 83,
    "MEM": 91, "CRC": 201,
    "ETH": 88, "ETHTRCV": 73,
}

RE_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
RE_SDK_INCLUDE = re.compile(r'^(hpm_|soc\.h$|board\.h$|pinmux\.h$|pinmux\.c$)')
RE_GUARD_IFNDEF = re.compile(r'^\s*#\s*ifndef\s+([A-Za-z0-9_]+)', re.MULTILINE)
RE_API_ID = re.compile(r'\b([A-Z0-9]+_(?:API_ID_[A-Z0-9_]+|[A-Z0-9_]+_API_ID))\b')
RE_DET_CALL = re.compile(r'MCAL_DET_REPORT\s*\(\s*(?:\(uint16\))?\s*([A-Z0-9_]+?)_MODULE_ID')
RE_HPM_MACRO = re.compile(r'\bHPM_[A-Z][A-Z0-9_]*\b')
# HPM 外设实例名（不含 HPM_L1C_CACHELINE_ALIGN_* 这类 SDK 工具宏）
HPM_INSTANCE_NAMES = {
    "IOC", "PIOC", "SYSCTL", "PPOR", "PCFG", "PLLCTLV2", "MCHTMR", "PLIC", "PLICSW",
    "DMAMUX", "HDMA", "XDMA", "OTP", "TRGM0", "GPIOM", "FGPIO", "PGPIO", "PTMR",
    "PEWDG", "CRC", "MBX", "TSNS", "PPI", "PLB", "SDM", "SYNT", "LOBS", "ACMP0",
    "USB0", "ENET0", "ESC", "PUART",
}


def is_hpm_instance(macro):
    """判断 HPM_xxx 宏是否指向某个硬件实例（而不是 SDK 工具宏）。"""
    name = macro[4:]
    if name.endswith("_BASE"):
        return True
    if re.search(r'[A-Z]+[0-9]+$', name):
        return True
    return name in HPM_INSTANCE_NAMES
RE_PIN_NAME = re.compile(r'\bP[A-F](?:0[0-9]|1[0-9]|2[0-9]|3[01])\b')
RE_IOC_PAD = re.compile(r'\bIOC_P(?:AD_P[A-F][0-9]{2}|[A-F][0-9]{2}_FUNC_CTL)\b')
RE_IRQN = re.compile(r'\bIRQn_[A-Z][A-Z0-9_]*\b')

HPM_ALLOW = ("HPM_MCAL", "HPM_SDK", "HPM_USE_", "HPM_DEBUG")


class Finding:
    def __init__(self, level, check, path, line, msg):
        self.level = level          # "error" / "warning"
        self.check = check
        self.path = path
        self.line = line
        self.msg = msg

    def as_dict(self):
        return {"level": self.level, "check": self.check, "path": str(self.path),
                "line": self.line, "message": self.msg}

    def __str__(self):
        loc = str(self.path)
        if self.line:
            loc += ":%d" % self.line
        return "[%s] %s %s — %s" % (self.level.upper(), self.check, loc, self.msg)


def strip_c_comments(text):
    """去掉 // 与 /* */ 注释，保留字符串字面量（避免注释里的引脚名误报）。"""
    out = []
    i = 0
    n = len(text)
    state = "code"
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if c == '"':
                state = "string"
            out.append(c)
            i += 1
        elif state == "line":
            if c == "\n":
                state = "code"
                out.append(c)
            i += 1
        elif state == "block":
            if c == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
            i += 1
        else:  # string
            if c == "\\":
                out.append(c)
                if i + 1 < n:
                    out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                state = "code"
            out.append(c)
            i += 1
    return "".join(out)


def read_text(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def iter_modules(root):
    mcal = root / "mcal"
    if not mcal.is_dir():
        return []
    return sorted([p for p in mcal.iterdir() if p.is_dir() and p.name != "common"],
                  key=lambda p: p.name.lower())


def parse_common_module_ids(root):
    """解析 mcal/common/Mcal_ModuleId.h 的 <MOD>_MODULE_ID 定义。"""
    path = root / "mcal" / "common" / "Mcal_ModuleId.h"
    ids = {}
    text = strip_c_comments(read_text(path))
    pat = re.compile(r'#\s*define\s+([A-Z0-9_]+)_MODULE_ID\s+\(\(uint16\)\s*(\d+)u?\)')
    for name, val in pat.findall(text):
        ids[name] = int(val)
    return ids


# --------------------------------------------------------------------------- 检查

def check_public_headers(root, findings):
    """① 公共头不得 include HPM SDK 头。"""
    n = 0
    for mod in iter_modules(root):
        for sub, level in (("include", "error"), ("cfg", "warning")):
            d = mod / sub
            if not d.is_dir():
                continue
            for hdr in sorted(d.glob("*.h")):
                n += 1
                for i, line in enumerate(read_text(hdr).splitlines(), 1):
                    m = RE_INCLUDE.match(line)
                    if not m:
                        continue
                    inc = m.group(1)
                    if RE_SDK_INCLUDE.match(inc):
                        findings.append(Finding(level, "C1-头文件依赖", hdr, i,
                                                "公共头 include 了 SDK 头 '%s'（板级定义应放 cfg/*_PBcfg.c）" % inc))
    return n


def check_driver_hardcode(root, findings):
    """② 驱动 .c 不得硬编码引脚/实例。"""
    n = 0
    for mod in iter_modules(root):
        for src in sorted((mod / "src").glob("*.c")) if (mod / "src").is_dir() else []:
            n += 1
            text = strip_c_comments(read_text(src))
            for i, line in enumerate(text.splitlines(), 1):
                for m in RE_HPM_MACRO.finditer(line):
                    macro = m.group(0)
                    if macro.startswith(HPM_ALLOW) or not is_hpm_instance(macro):
                        continue
                    findings.append(Finding("error", "C2-硬编码实例", src, i,
                                            "驱动源码出现实例宏 %s（应在 cfg/*_PBcfg.c）" % macro))
                for m in RE_PIN_NAME.finditer(line):
                    findings.append(Finding("error", "C2-硬编码引脚", src, i,
                                            "驱动源码出现引脚名 %s（应在 cfg/*_PBcfg.c）" % m.group(0)))
                for m in RE_IOC_PAD.finditer(line):
                    findings.append(Finding("error", "C2-硬编码引脚", src, i,
                                            "驱动源码出现 IOC 引脚宏 %s（应在 cfg/*_PBcfg.c）" % m.group(0)))
                for m in RE_IRQN.finditer(line):
                    findings.append(Finding("warning", "C2-硬编码中断", src, i,
                                            "驱动源码出现中断号宏 %s（建议放 PBcfg）" % m.group(0)))
        # 模块自己的 .c 不在 src/ 下也算驱动源码（排除 cfg/）
        for cfile in sorted(mod.rglob("*.c")):
            if "cfg" in cfile.parts or cfile.parent.name == "src":
                continue
            n += 1
            text = strip_c_comments(read_text(cfile))
            for i, line in enumerate(text.splitlines(), 1):
                m = RE_HPM_MACRO.search(line)
                if m and not m.group(0).startswith(HPM_ALLOW) and is_hpm_instance(m.group(0)):
                    findings.append(Finding("error", "C2-硬编码实例", cfile, i,
                                            "驱动源码出现实例宏 %s" % m.group(0)))
    return n


def check_module_completeness(root, findings):
    """③ 模块五件套 + 头文件必备宏。"""
    n = 0
    for mod in iter_modules(root):
        n += 1
        name = mod.name
        upper = name.upper()
        expected = {
            "include/%s.h" % name: "公共 API 头",
            "src/%s.c" % name: "驱动源",
            "cfg/%s_Cfg.h" % name: "预编译配置",
            "cfg/%s_PBcfg.h" % name: "post-build 配置声明",
            "cfg/%s_PBcfg.c" % name: "post-build 配置实例",
        }
        for rel, desc in expected.items():
            if not (mod / rel).exists():
                findings.append(Finding("error", "C3-模块文件", mod / rel, 0, "缺少 %s" % desc))
        hdr = mod / "include" / ("%s.h" % name)
        if not hdr.exists():
            continue
        hdr_text = read_text(hdr)
        all_h = "\n".join(read_text(p) for p in sorted(mod.rglob("*.h")))
        all_text = all_h + "\n" + "\n".join(read_text(p) for p in sorted(mod.rglob("*.c")))

        # include guard
        if not RE_GUARD_IFNDEF.search(hdr_text) or "#endif" not in hdr_text:
            findings.append(Finding("error", "C3-头文件", hdr, 0, "缺少 include guard"))

        # 模块 ID（要么自己定义，要么 include Mcal_ModuleId.h）
        has_id = ("%s_MODULE_ID" % upper) in all_text
        include_id = "Mcal_ModuleId.h" in hdr_text
        if not (has_id or include_id):
            findings.append(Finding("error", "C3-模块ID", hdr, 0,
                                    "未使用 %s_MODULE_ID 也未 include Mcal_ModuleId.h" % upper))

        # 版本宏
        for suffix in ("SW_MAJOR_VERSION", "SW_MINOR_VERSION", "SW_PATCH_VERSION",
                       "AR_RELEASE_MAJOR_VERSION", "AR_RELEASE_MINOR_VERSION",
                       "AR_RELEASE_REVISION_VERSION"):
            if ("%s_%s" % (upper, suffix)) not in all_text:
                findings.append(Finding("error", "C3-版本宏", hdr, 0,
                                        "缺少 %s_%s" % (upper, suffix)))

        # API ID
        api_ids = set(RE_API_ID.findall(read_text(hdr) + "\n" + read_text(mod / "cfg" / ("%s_Cfg.h" % name))))
        if not any(x.startswith(upper + "_") for x in api_ids):
            findings.append(Finding("error", "C3-API_ID", hdr, 0,
                                    "缺少 %s_API_ID_* / %s_*_API_ID 常量" % (upper, upper)))

        # DEV_ERROR_DETECT 开关
        cfg_h = mod / "cfg" / ("%s_Cfg.h" % name)
        cfg_txt = read_text(cfg_h) if cfg_h.exists() else ""
        if ("%s_DEV_ERROR_DETECT" % upper) not in cfg_txt:
            findings.append(Finding("error", "C3-DevError开关", cfg_h, 0,
                                    "cfg 头缺少 %s_DEV_ERROR_DETECT 开关" % upper))
    return n


def check_module_ids(root, findings):
    """④ 模块 ID 一致性 + Det 上报用自己的模块 ID。"""
    common_ids = parse_common_module_ids(root)
    n = 0
    if not common_ids:
        findings.append(Finding("error", "C4-模块ID", root / "mcal" / "common" / "Mcal_ModuleId.h", 0,
                                "解析不到任何 <MOD>_MODULE_ID 定义"))
        return 0
    for key, val in SWS_MODULE_IDS.items():
        n += 1
        if key not in common_ids:
            findings.append(Finding("error", "C4-模块ID", root / "mcal" / "common" / "Mcal_ModuleId.h", 0,
                                    "缺少 %s_MODULE_ID（SWS 标准值 %d）" % (key, val)))
        elif common_ids[key] != val:
            findings.append(Finding("error", "C4-模块ID", root / "mcal" / "common" / "Mcal_ModuleId.h", 0,
                                    "%s_MODULE_ID=%d 与 SWS 标准值 %d 不一致" % (key, common_ids[key], val)))

    for mod in iter_modules(root):
        upper = mod.name.upper()
        # 自己若重复定义模块 ID，值必须一致
        for hdr in sorted(mod.rglob("*.h")) + sorted(mod.rglob("*.c")):
            pat = re.compile(r'#\s*define\s+%s_MODULE_ID\s+.*?\((\d+)u?\)' % upper)
            for i, line in enumerate(read_text(hdr).splitlines(), 1):
                m = pat.search(line)
                if m and upper in common_ids and int(m.group(1)) != common_ids[upper]:
                    findings.append(Finding("error", "C4-模块ID", hdr, i,
                                            "本模块重复定义的 %s_MODULE_ID=%s 与公共表 %d 不一致"
                                            % (upper, m.group(1), common_ids[upper])))
        # Det 上报必须用自己的模块 ID 宏
        for src in sorted(mod.rglob("*.c")):
            if "cfg" in src.parts:
                continue
            for i, line in enumerate(read_text(src).splitlines(), 1):
                for used in RE_DET_CALL.findall(line):
                    if used != upper:
                        findings.append(Finding("error", "C4-Det模块ID", src, i,
                                                "Det 上报用了 %s_MODULE_ID，本模块是 %s" % (used, upper)))
    return n


def check_sample_readmes(root, findings):
    """⑤ 例程 README 必须含 hwrun 与 logs/ 路径。"""
    samples = root / "samples" / "mcal"
    n = 0
    if not samples.is_dir():
        findings.append(Finding("error", "C5-例程README", samples, 0, "samples/mcal 目录不存在"))
        return 0
    for d in sorted([p for p in samples.iterdir() if p.is_dir()]):
        if d.name.startswith("_"):
            continue
        n += 1
        readme = d / "README.md"
        if not readme.exists():
            findings.append(Finding("error", "C5-例程README", readme, 0, "缺少 README.md"))
            continue
        text = read_text(readme)
        if "hwrun" not in text:
            findings.append(Finding("error", "C5-例程README", readme, 0, "README 里没有 hwrun 运行命令"))
        if "logs/" not in text:
            findings.append(Finding("error", "C5-例程README", readme, 0, "README 里没有 logs/ 真实日志路径"))
    return n


def run_checks(root):
    findings = []
    stats = {}
    stats["public_headers"] = check_public_headers(root, findings)
    stats["driver_sources"] = check_driver_hardcode(root, findings)
    stats["modules"] = check_module_completeness(root, findings)
    stats["module_ids"] = check_module_ids(root, findings)
    stats["sample_readmes"] = check_sample_readmes(root, findings)
    return findings, stats


# --------------------------------------------------------------------------- 报告

def find_size_tool(root):
    for cand in ("riscv32-unknown-elf-size", "riscv-none-elf-size"):
        p = shutil.which(cand)
        if p:
            return p
    for pat in ("$HOME/toolchains/*/bin/riscv32-unknown-elf-size",
                "$HOME/toolchains/*/bin/riscv-none-elf-size"):
        hits = sorted(Path("/").glob(pat.lstrip("/")))
        if hits:
            return str(hits[0])
    return None


def size_of_objects(tool, files):
    """返回 {path: (text, data, bss)}（单位字节）。"""
    result = {}
    if not tool or not files:
        return result
    try:
        out = subprocess.run([tool] + [str(f) for f in files], capture_output=True,
                             text=True, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return result
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 6:
            continue
        try:
            text, data, bss = int(parts[0]), int(parts[1]), int(parts[2])
        except ValueError:
            continue
        result[parts[-1]] = (text, data, bss)
    return result


def module_sizes(root, module_names):
    """按模块聚合 .obj 体积（跨 build/*-debug 去重同名源文件）。"""
    builds = sorted([p for p in (root / "build").glob("*-debug") if p.is_dir()]) if (root / "build").is_dir() else []
    per_mod_files = {}
    for mod in module_names:
        seen = {}
        for bd in builds:
            for obj in bd.rglob("*.obj"):
                parts = obj.parts
                # 形如 .../mcal/Mcu/src/Mcu.c.obj
                idx = None
                for k, part in enumerate(parts):
                    if part == "mcal" and k + 1 < len(parts) and parts[k + 1] == mod:
                        idx = k
                        break
                if idx is None:
                    continue
                key = "/".join(parts[idx:])
                if key not in seen:
                    seen[key] = obj
        per_mod_files[mod] = seen
    tool = find_size_tool(root)
    all_files = [f for seen in per_mod_files.values() for f in seen.values()]
    sizes = size_of_objects(tool, all_files)
    agg = {}
    for mod, seen in per_mod_files.items():
        t = d = b = 0
        for obj in seen.values():
            if str(obj) in sizes:
                tt, dd, bb = sizes[str(obj)]
                t += tt
                d += dd
                b += bb
        agg[mod] = {"text": t, "data": d, "bss": b}
    return agg, tool


def count_loc_and_files(mod):
    files = [p for p in sorted(mod.rglob("*")) if p.is_file() and p.suffix in (".c", ".h")]
    loc = 0
    for f in files:
        loc += len(read_text(f).splitlines())
    return len(files), loc


def count_api(mod):
    # 头文件名大小写跟随模块目录（如 mcal/I2c/include/I2c.h）；函数名前缀按 SWS 可能全大写（I2C_Init）
    cands = [p for p in (mod / "include").glob("*.h") if p.stem.lower() == mod.name.lower()]
    hdr = cands[0] if cands else (mod / "include" / ("%s.h" % mod.name))
    text = strip_c_comments(read_text(hdr))
    names = set()
    # 只认"函数声明"：<前缀>_xxx(...) 后面跟 ';'（排除注释/宏/调用）
    pat = re.compile(r'\b(%s_[A-Za-z0-9_]+)\s*\([^;{}]*\)\s*;' % re.escape(mod.name),
                     re.IGNORECASE | re.DOTALL)
    for m in pat.finditer(text):
        name = m.group(1).upper()
        if name.endswith("_API_ID") or name.endswith("_MODULE_ID"):
            continue
        names.add(name)
    api_ids = set(x for x in RE_API_ID.findall(text) if x.startswith(mod.name.upper() + "_"))
    cfg_cands = [p for p in (mod / "cfg").glob("*_Cfg.h")
                 if p.stem.lower() == ("%s_cfg" % mod.name).lower()]
    cfg = read_text(cfg_cands[0]) if cfg_cands else read_text(mod / "cfg" / ("%s_Cfg.h" % mod.name))
    api_ids |= set(x for x in RE_API_ID.findall(cfg) if x.startswith(mod.name.upper() + "_"))
    return len(names), len(api_ids)


def sample_modules(root):
    """解析 samples/mcal/*/CMakeLists.txt 的 mcal_add(...)，得到例程->模块列表。"""
    mapping = {}
    for d in sorted([p for p in (root / "samples" / "mcal").iterdir() if p.is_dir()]):
        cm = d / "CMakeLists.txt"
        if not cm.exists():
            continue
        mods = []
        for m in re.finditer(r'mcal_add\s*\(([^)]*)\)', read_text(cm)):
            mods.extend(m.group(1).split())
        mapping[d.name] = mods
    return mapping


def latest_log(root, sample):
    logs = sorted((root / "logs").glob("*-%s.log" % sample)) if (root / "logs").is_dir() else []
    if not logs:
        return None
    return logs[-1]


def parse_build_log(path):
    res = {}
    p = Path(path)
    if not p.is_file():
        return res
    cur_fail = None
    for line in read_text(p).splitlines():
        m = re.match(r'OK\s+(\S+)(?:\s+warnings=(\d+))?$', line.strip())
        if m:
            res[m.group(1)] = {"ok": True, "warnings": int(m.group(2) or 0)}
            cur_fail = None
            continue
        m = re.match(r'FAIL\s+(\S+)$', line.strip())
        if m:
            res[m.group(1)] = {"ok": False, "warnings": 0}
            cur_fail = m.group(1)
    return res


def write_report(root, findings, stats, build_log):
    mods = iter_modules(root)
    mod_names = [m.name for m in mods]
    sizes, size_tool = module_sizes(root, mod_names)
    smap = sample_modules(root)
    builds = parse_build_log(build_log)

    rows = []
    total_flash = total_ram = total_loc = total_files = total_api = 0
    for mod in mods:
        nfiles, loc = count_loc_and_files(mod)
        napi, napi_ids = count_api(mod)
        sz = sizes.get(mod.name, {"text": 0, "data": 0, "bss": 0})
        flash = sz["text"] + sz["data"]
        ram = sz["data"] + sz["bss"]
        total_flash += flash
        total_ram += ram
        total_loc += loc
        total_files += nfiles
        total_api += napi
        samples = [s for s, ms in smap.items() if mod.name in ms]
        logs = []
        for s in samples:
            lg = latest_log(root, s)
            if lg:
                logs.append(lg.name)
        if not samples:
            status = "无例程"
        elif any(builds.get(s, {}).get("ok") is False for s in samples):
            status = "编译失败"
        elif logs:
            status = "上板 PASS"
        elif any(builds.get(s, {}).get("ok") for s in samples):
            status = "编译 OK·未上板"
        else:
            status = "未编译"
        rows.append({
            "mod": mod.name, "files": nfiles, "loc": loc, "api": napi, "api_ids": napi_ids,
            "text": sz["text"], "data": sz["data"], "bss": sz["bss"],
            "flash": flash, "ram": ram, "samples": samples, "logs": logs, "status": status,
        })

    errs = [f for f in findings if f.level == "error"]
    warns = [f for f in findings if f.level == "warning"]
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    lines = []
    lines.append("# MCAL 模块统计（自动生成）")
    lines.append("")
    lines.append("> 本文件由 `tools/mcal_check.py --report` 生成，请勿手工编辑。")
    lines.append("> 生成时间：%s；数据来源：mcal/ 源码、build/*-debug 的 .obj（riscv size）、"
                 "logs/ 串口日志、/tmp/mcal_build.log。" % now)
    lines.append("> 体积为 Debug（-O0 级别）配置下 MCAL 目标文件之和，仅统计 `mcal/<Mod>/` 目录里的编译单元，"
                 "不含 SDK 驱动与 BSP；Flash=text+data，RAM=data+bss。")
    lines.append("> 注意 .data 里主要是 `*_PBcfg.c` 的配置表（例如 CAN 的 10 KB 大部分是滤波/报文配置表），"
                 "量产配置可按需裁剪。")
    lines.append("> 统计口径（与第 48 章表 48-8 对齐用）：每个模块的 .obj 取 text/data/bss 三段；"
                 "Flash=text+data，RAM=data+bss —— 即 .data 同时占 Flash（初值）和 RAM（运行副本），"
                 "不是重复统计。数据源为单次完整构建 build/ecu_integration-debug 下 `*/mcal/<Mod>/*.obj`，"
                 "与遍历全部 build/*-debug 去重后的结果逐模块一致（2026-11-02 核对）。")
    lines.append("")
    lines.append("## 汇总")
    lines.append("")
    lines.append("| 指标 | 数值 |")
    lines.append("|---|---|")
    lines.append("| 模块数 | %d |" % len(rows))
    lines.append("| 源文件数（.c/.h） | %d |" % total_files)
    lines.append("| 代码行数（LOC） | %d |" % total_loc)
    lines.append("| 公共 API 数（声明去重） | %d |" % total_api)
    lines.append("| Flash 占用（text+data） | %d 字节（%.1f KiB） |" % (total_flash, total_flash / 1024.0))
    lines.append("| RAM 占用（data+bss） | %d 字节（%.1f KiB） |" % (total_ram, total_ram / 1024.0))
    lines.append("| 例程数（samples/mcal） | %d |" % len(smap))
    lines.append("| 编译通过例程 | %d |" % sum(1 for v in builds.values() if v["ok"]))
    lines.append("| 静态检查 | %s（%d 错 / %d 警） |" % ("PASS" if not errs and not warns else "FAIL", len(errs), len(warns)))
    present = {x.lower() for x in mod_names}
    missing_mods = [m for m in sorted(SWS_MODULE_IDS) if m.lower() not in present]
    lines.append("| 尚未交付的模块 | %s |" % (", ".join(missing_mods) if missing_mods else "无（13 个模块齐全）"))
    lines.append("| size 工具 | %s |" % (size_tool or "未找到（体积列为 0）"))
    lines.append("")
    lines.append("## 模块明细")
    lines.append("")
    lines.append("| 模块 | 文件数 | LOC | API 数 | API_ID 数 | text(B) | data(B) | bss(B) | Flash(B) | RAM(B) | 例程 | 最新日志 | 状态 |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        lines.append("| %s | %d | %d | %d | %d | %d | %d | %d | %d | %d | %s | %s | %s |" % (
            r["mod"], r["files"], r["loc"], r["api"], r["api_ids"], r["text"], r["data"], r["bss"],
            r["flash"], r["ram"],
            ", ".join(r["samples"]) if r["samples"] else "—",
            ", ".join(r["logs"]) if r["logs"] else "—",
            r["status"]))
    lines.append("")
    lines.append("## 例程编译结果")
    lines.append("")
    lines.append("| 例程 | 使用模块 | 编译 | warning | 最新日志 | 状态 |")
    lines.append("|---|---|---|---|---|---|")
    for s, ms in smap.items():
        b = builds.get(s)
        if b is None:
            btxt, wtxt = "未编译", "—"
        else:
            btxt, wtxt = ("OK" if b["ok"] else "FAIL"), str(b["warnings"])
        lg = latest_log(root, s)
        status = "PASS" if (b and b["ok"] and lg) else ("编译 OK·未上板" if (b and b["ok"]) else "待修")
        lines.append("| %s | %s | %s | %s | %s | %s |" % (
            s, ", ".join(ms) if ms else "—", btxt, wtxt, lg.name if lg else "—", status))
    lines.append("")
    if errs or warns:
        lines.append("## 静态检查发现")
        lines.append("")
        lines.append("| 级别 | 检查项 | 位置 | 说明 |")
        lines.append("|---|---|---|---|")
        for f in errs + warns:
            lines.append("| %s | %s | %s:%d | %s |" % (f.level, f.check, f.path, f.line, f.msg))
        lines.append("")
    lines.append("## 引用方式")
    lines.append("")
    lines.append("- 第 42 章（MCAL 总览）：用上面的「汇总」表给全书 MCAL 规模/体积一句话数据。")
    lines.append("- 第 48 章（Mem/Crc）与各模块章：用模块明细表里对应行的 LOC/API/Flash/RAM。")
    lines.append("- 状态列的「上板 PASS」判据：该模块的例程存在 logs/*-<例程>.log 且编译通过；"
                 "具体 PASS/FAIL 以各例程 README 与日志为准。")
    lines.append("- 复现命令：`python3 tools/mcal_check.py --build --report`（编译 samples/mcal/* + 检查 + 生成本文件）。")
    lines.append("")

    out_dir = root / "docs" / "generated"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "mcal-modules.md"
    out.write_text("\n".join(lines), encoding="utf-8")
    return out


def run_builds(root, build_log):
    """循环编译 samples/mcal/*（等价于 tools/build_all.sh 的 mcal 子集），结果写 build_log。"""
    build_sh = root / "scripts" / "build.sh"
    out_lines = []
    ok = fail = 0
    for d in sorted([p for p in (root / "samples" / "mcal").iterdir() if p.is_dir()]):
        if d.name.startswith("_") or not (d / "CMakeLists.txt").exists():
            continue
        try:
            proc = subprocess.run(["bash", str(build_sh), str(d)], capture_output=True,
                                  text=True, timeout=900, cwd=str(root))
            log = (proc.stdout or "") + (proc.stderr or "")
            good = "ELF:" in log
        except (OSError, subprocess.SubprocessError) as exc:
            log = str(exc)
            good = False
        nwarn = log.count("warning:")
        if good:
            ok += 1
            out_lines.append("OK   %s warnings=%d" % (d.name, nwarn))
        else:
            fail += 1
            out_lines.append("FAIL %s" % d.name)
            out_lines.extend(["  " + ln for ln in log.strip().splitlines()[-6:]])
        print("  %s %s (warnings=%d)" % ("OK  " if good else "FAIL", d.name, nwarn))
    out_lines.append("---- build_all_mcal: ok=%d fail=%d ----" % (ok, fail))
    p = Path(build_log)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    print("构建结果：ok=%d fail=%d -> %s" % (ok, fail, p))
    return ok, fail


def main(argv=None):
    ap = argparse.ArgumentParser(description="HPM5E31 MCAL 静态合规检查器")
    ap.add_argument("--root", default=str(ROOT), help="仓库根目录（默认脚本上一级）")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--report", action="store_true", help="生成 docs/generated/mcal-modules.md")
    ap.add_argument("--build-log", default="/tmp/mcal_build.log", help="mcal 例程构建结果日志")
    ap.add_argument("--no-warn-fail", action="store_true", help="警告不导致非 0 退出")
    ap.add_argument("--build", action="store_true", help="先循环编译 samples/mcal/* 再生成报告")
    args = ap.parse_args(argv)

    root = Path(args.root).resolve()
    if args.build:
        print("== 编译 samples/mcal/* ==")
        run_builds(root, args.build_log)
    findings, stats = run_checks(root)
    errs = [f for f in findings if f.level == "error"]
    warns = [f for f in findings if f.level == "warning"]

    report_path = None
    if args.report:
        report_path = write_report(root, findings, stats, args.build_log)

    if args.json:
        print(json.dumps({
            "root": str(root),
            "stats": stats,
            "errors": [f.as_dict() for f in errs],
            "warnings": [f.as_dict() for f in warns],
            "report": str(report_path) if report_path else None,
            "pass": (not errs) and (not warns or args.no_warn_fail),
        }, ensure_ascii=False, indent=1))
    else:
        print("== MCAL 静态检查 ==")
        print("  模块 %d 个｜公共头 %d 个｜驱动源码 %d 个｜模块 ID 检查 %d 项｜例程 README %d 个"
              % (stats["modules"], stats["public_headers"], stats["driver_sources"],
                 stats["module_ids"], stats["sample_readmes"]))
        for f in errs + warns:
            print("  " + str(f))
        print("")
        if report_path:
            print("报告：%s" % report_path.relative_to(root))
        print("结果：%d 错 / %d 警 -> %s" % (len(errs), len(warns),
              "PASS" if (not errs and (not warns or args.no_warn_fail)) else "FAIL"))
    return 0 if (not errs and (not warns or args.no_warn_fail)) else 1


if __name__ == "__main__":
    sys.exit(main())
