#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""docs/mcal/gen_traceability.py —— 生成 MCAL API 追溯矩阵（第 50 章支撑材料）

输入：
  docs/mcal/sws_api_data.json        AUTOSAR CP SWS R25-11 抽取文本的机器清单（parse_sws.py 产物）
  docs/mcal/traceability_manual.json 人工口径（部分实现/备注/状态机/偏差清单/验证状态）
  mcal/<Mod>/include/<Mod>.h         本实现公共头（API 声明、API ID、DET 错误码、版本宏）
  mcal/<Mod>/src/<Mod>.c             本实现驱动源（用来核对每个 API 是否有 DET 检查）
  mcal/<Mod>/cfg/*                    预编译/后编译配置

输出：
  docs/mcal/MCAL_API_TRACEABILITY.md 追溯矩阵正文
  docs/mcal/traceability_stats.json  机器可读统计（给第 50 章热力图用）

用法：python3 docs/mcal/gen_traceability.py [--check]
  --check  只打印统计，不写文件（CI 用）
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MCAL = ROOT / "mcal"
DOCS_MCAL = ROOT / "docs" / "mcal"
SWS_JSON = DOCS_MCAL / "sws_api_data.json"
MANUAL_JSON = DOCS_MCAL / "traceability_manual.json"
OUT_MD = DOCS_MCAL / "MCAL_API_TRACEABILITY.md"
OUT_STATS = DOCS_MCAL / "traceability_stats.json"

BT = chr(96)
FENCE = BT * 3

SWS = json.loads(SWS_JSON.read_text(encoding="utf-8")) if SWS_JSON.exists() else {}

MODULES = ["Mcu", "Port", "Dio", "Gpt", "Pwm", "Icu", "Wdg", "Adc", "Spi", "I2c", "Can", "Mem", "Crc", "Eth", "EthTrcv"]
DIR_OF = {m: m for m in MODULES}

# API 分组（用于第 50 章覆盖率热力图）：先匹配先归类
GROUPS = [
    ("初始化/去初始化", r"_(Init|DeInit|InitRamSection)$"),
    ("时钟/复位/模式/低功耗", r"_(InitClock|DistributePllClock|PerformReset|GetResetReason|GetResetRawValue|"
                             r"GetPllStatus|SetMode|GetRamState|SetBaudrate|SetControllerMode|"
                             r"SetPowerState|GetCurrentPowerState|GetTargetPowerState|PreparePowerState|"
                             r"Suspend|Resume|PropagateError)$"),
    ("数据通路/作业", r"_(Read|Write|Erase|BlankCheck|AsyncTransmit|SyncTransmit|SetupEB|WriteIB|ReadIB|"
                      r"StartGroupConversion|StopGroupConversion|ReadGroup|SetupResultBuffer|StartTimer|"
                      r"StopTimer|SetDutyCycle|SetPeriodAndDuty|SetOutputToIdle|StartTimestamp|StopTimestamp|"
                      r"StartSignalMeasurement|StopSignalMeasurement|SetActivationCondition|SetPinDirection|"
                      r"SetPinMode|WriteChannel|WritePort|WriteChannelGroup|SetTriggerCondition|"
                      r"FlipChannel|MaskedWritePort|ReadChannel|ReadPort|ReadChannelGroup|RefreshPortDirection|"
                      r"DisableControllerInterrupts|EnableControllerInterrupts|SetCanPnFrameDataMask|"
                      r"Cancel|SetAsyncMode|HwSpecificService)$"),
    ("状态/查询", r"_(GetStatus|GetJobResult|GetSequenceResult|GetHWUnitStatus|GetGroupStatus|"
                  r"GetStreamLastPointer|GetOutputState|GetInputState|GetTimeElapsed|GetTimeRemaining|"
                  r"GetTimestampIndex|GetEdgeNumbers|GetDutyCycleValues|GetControllerErrorState|"
                  r"GetControllerMode|GetControllerRxErrorCounter|GetControllerTxErrorCounter|GetCurrentTime|"
                  r"GetPredefTimerValue|GetPinDirection|GetPinMode)$"),
    ("通知/中断/唤醒控制", r"_(EnableNotification|DisableNotification|EnableNotificationAsync|"
                           r"DisableNotificationAsync|EnableEdgeDetection|DisableEdgeDetection|"
                           r"EnableEdgeCount|DisableEdgeCount|ResetEdgeCount|EnableGroupNotification|"
                           r"DisableGroupNotification|EnableHardwareTrigger|DisableHardwareTrigger|"
                           r"EnableWakeup|DisableWakeup|CheckWakeup|EnableEgressTimeStamp|"
                           r"GetEgressTimeStamp|GetIngressTimeStamp)$"),
    ("版本信息", r"_GetVersionInfo$"),
    ("调度函数", r"MainFunction"),
]

PROTO_RE = re.compile(
    r"(?m)^[ \t]*(?:extern[ \t]+)?(?:const[ \t]+)?"
    r"([A-Za-z_][A-Za-z0-9_]*)[ \t*]+"
    r"([A-Z][A-Za-z0-9]*_[A-Za-z0-9_]+)[ \t]*\("
)
DEFINE_RE = re.compile(r"(?m)^[ \t]*#[ \t]*define[ \t]+([A-Za-z0-9_]+)[ \t]+(\(?[^/\n]+)")


def q(s):
    return BT + str(s) + BT


def norm(name):
    n = re.sub(r"<[^>]*>", "", name)
    return re.sub(r"[^a-z0-9]", "", n.lower())


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def read(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def parse_header(mod):
    hp = MCAL / DIR_OF[mod] / "include" / (mod + ".h")
    raw = read(hp)
    if not raw:
        return None
    code = strip_comments(raw)
    defines = {}
    for m in DEFINE_RE.finditer(code):
        defines.setdefault(m.group(1), m.group(2).strip())
    protos = {}
    for m in PROTO_RE.finditer(code):
        protos[m.group(2)] = m.group(1)
    up = mod.upper()
    api_ids = {k: v for k, v in defines.items() if k.startswith(up + "_API_ID_") or k.startswith(up + "_SID")}
    dets = {k: v for k, v in defines.items() if k.startswith(up + "_E_")}
    vers = {k: v for k, v in defines.items()
            if re.search(r"_(AR_RELEASE_(MAJOR|MINOR|REVISION)_VERSION|SW_(MAJOR|MINOR|PATCH)_VERSION|"
                         r"VENDOR_ID|MODULE_ID)$", k)}
    cfg_fields = 0
    m = re.search(r"typedef\s+struct\s*\{(.*?)\}\s*" + mod + r"_ConfigType\s*;", code, re.S)
    if m:
        cfg_fields = m.group(1).count(";")
    cfg_h = read(MCAL / DIR_OF[mod] / "cfg" / (mod + "_Cfg.h"))
    cfg_switches = len(re.findall(r"(?m)^[ \t]*#[ \t]*define[ \t]+([A-Z0-9_]+)", cfg_h))
    pbc = read(MCAL / DIR_OF[mod] / "cfg" / (mod + "_PBcfg.c"))
    files = {
        "header": str(hp.relative_to(ROOT)) if hp.exists() else "",
        "source": str((MCAL / DIR_OF[mod] / "src" / (mod + ".c")).relative_to(ROOT))
                  if (MCAL / DIR_OF[mod] / "src" / (mod + ".c")).exists() else "",
        "cfg": str((MCAL / DIR_OF[mod] / "cfg" / (mod + "_Cfg.h")).relative_to(ROOT)) if cfg_h else "",
        "pbcfg": str((MCAL / DIR_OF[mod] / "cfg" / (mod + "_PBcfg.c")).relative_to(ROOT)) if pbc else "",
    }
    loc = 0
    for p in (MCAL / DIR_OF[mod]).rglob("*"):
        if p.is_file() and p.suffix in (".c", ".h"):
            loc += len(read(p).splitlines())
    return dict(defines=defines, protos=protos, api_ids=api_ids, dets=dets, vers=vers,
                header_raw=raw,
                cfg_fields=cfg_fields, cfg_switches=cfg_switches, has_cfg=bool(cfg_h),
                has_pb=bool(pbc), files=files, loc=loc)


def func_bodies(src, names):
    code = strip_comments(src)
    out = {}
    for name in names:
        best = None
        for m in re.finditer(r"\b" + re.escape(name) + r"\s*\(", code):
            i = code.find("{", m.end())
            if i < 0:
                continue
            depth = 0
            for j in range(i, len(code)):
                if code[j] == "{":
                    depth += 1
                elif code[j] == "}":
                    depth -= 1
                    if depth == 0:
                        cand = code[i:j + 1]
                        if best is None or len(cand) > 40:
                            best = cand
                        break
        if best:
            out[name] = best
    return out


def parse_source(mod):
    return read(MCAL / DIR_OF[mod] / "src" / (mod + ".c"))


def classify(api_name):
    for gname, pat in GROUPS:
        if re.search(pat, api_name):
            return gname
    return "其他"


def build(manual):
    stats = {"modules": {}, "totals": {}, "source": {
        "sws": "AUTOSAR CP SWS R25-11 抽取文本（autosar_txt/）",
        "impl": "本仓库 mcal/<Mod>/",
        "date": "2026-11-02",
    }}
    details = {}
    for mod in MODULES:
        sws = SWS.get(mod)
        hdr = parse_header(mod)
        svc = parse_source(mod)
        proto_names = hdr["protos"] if hdr else {}
        norm_map = {norm(k): k for k in proto_names}
        bodies = func_bodies(svc, list(proto_names)) if proto_names else {}
        # 模块通常用本地宏包一层（如 #define MCU_DET(api, err) MCAL_DET_REPORT(...)），
        # 这里把“展开到 MCAL_DET_REPORT/Det_ReportError”的宏名也当成 DET 命中。
        det_macros = set()
        for txt in (svc, hdr["header_raw"] if hdr else ""):
            det_macros |= set(re.findall(
                r"#\s*define\s+(\w+)\s*\([^\n]*\)\s*(?:\(void\)\s*)?(?:MCAL_DET_REPORT|Det_ReportError)",
                txt))
        det_re = "(?:" + "|".join(["MCAL_DET_REPORT", "Det_ReportError"] + sorted(det_macros)) + r")\s*\("
        partial = manual.get("partial", {}).get(mod, {})
        notes = manual.get("notes", {}).get(mod, {})
        rows = []
        groups = {}
        missing, partial_list = [], []
        for api in (sws["apis"] if sws else []):
            name = api["name"]
            ours = norm_map.get(norm(name))
            status = "未实现"
            note = notes.get(name, "")
            if ours:
                status = "已实现"
                if name in partial:
                    status = "部分"
                    note = partial[name]
            if not ours:
                missing.append(name)
            if status == "部分":
                partial_list.append(name)
            body = bodies.get(ours, "") if ours else ""
            det = bool(re.search(det_re, body))
            g = classify(name)
            gd = groups.setdefault(g, [0, 0])
            gd[0] += 1
            if status != "未实现":
                gd[1] += 1
            rows.append(dict(api=name, sid=api.get("service_id", ""), sync=api.get("sync", ""),
                             ours=ours or "", status=status, det=det, note=note, group=g))
        extra = []
        for name in (sws.get("scheduled") or []) + (sws.get("callbacks") or []):
            ours = norm_map.get(norm(name))
            extra.append(dict(api=name, ours=ours or "", status="已实现" if ours else "未实现",
                              kind="调度/回调"))
            g = "调度函数" if "MainFunction" in name else "通知/中断/唤醒控制"
            gd = groups.setdefault(g, [0, 0])
            gd[0] += 1
            if ours:
                gd[1] += 1
        impl = sum(1 for r in rows if r["status"] == "已实现")
        part = sum(1 for r in rows if r["status"] == "部分")
        miss = sum(1 for r in rows if r["status"] == "未实现")
        det_ok = sum(1 for r in rows if r["status"] != "未实现" and r["det"])
        total = len(rows)
        stats["modules"][mod] = dict(
            sws_total=total, implemented=impl, partial=part, missing=miss,
            coverage=round((impl + 0.5 * part) / total, 3) if total else 0.0,
            api_id_defined=len(hdr["api_ids"]) if hdr else 0,
            det_codes_defined=len(hdr["dets"]) if hdr else 0,
            det_checked=det_ok,
            signature=dict(proto_count=len(proto_names), cfg_fields=hdr["cfg_fields"],
                           cfg_switches=hdr["cfg_switches"], has_cfg=hdr["has_cfg"],
                           has_pbcfg=hdr["has_pb"], loc=hdr["loc"]) if hdr else {},
            files=hdr["files"] if hdr else {},
            groups={k: dict(total=v[0], implemented=v[1],
                            coverage=round(v[1] / v[0], 3) if v[0] else None)
                    for k, v in groups.items()},
            has_header=bool(hdr),
            missing_apis=missing, partial_apis=partial_list,
        )
        details[mod] = dict(rows=rows, extra=extra, sws=sws, hdr=hdr)

    total_api = sum(m["sws_total"] for m in stats["modules"].values())
    total_impl = sum(m["implemented"] for m in stats["modules"].values())
    total_part = sum(m["partial"] for m in stats["modules"].values())
    total_miss = sum(m["missing"] for m in stats["modules"].values())
    stats["totals"] = dict(sws_total=total_api, implemented=total_impl, partial=total_part,
                           missing=total_miss,
                           coverage=round((total_impl + 0.5 * total_part) / total_api, 3) if total_api else 0,
                           det_checked=sum(m["det_checked"] for m in stats["modules"].values()),
                           loc=sum(m["signature"].get("loc", 0) for m in stats["modules"].values() if m["signature"]))
    return stats, details


def fmt_status(s):
    return {"已实现": "已实现", "部分": "**部分**", "未实现": "_未实现_"}.get(s, s)


def render(stats, details, manual):
    from mcal_module_ids import MODULE_IDS
    L = []
    A = L.append
    A("# MCAL API 追溯矩阵（AUTOSAR CP SWS R25-11 × HPM5E31 实现）")
    A("")
    A("> 本文件是第 50 章《AUTOSAR 合规性与商用 MCAL 对比》的支撑材料，由 "
      + q("docs/mcal/gen_traceability.py") +
      " 从 SWS 抽取文本与 " + q("mcal/") + " 源码生成；人工口径（部分实现判据、偏差、验证状态）"
      "在 " + q("docs/mcal/traceability_manual.json") + "。")
    A("")
    A("## 0. 口径与免责声明")
    A("")
    A("| 项 | 说明 |")
    A("|---|---|")
    A("| SWS 基准 | AUTOSAR CP R25-11 抽取文本（31 个 SWS，本地 " + q("autosar_txt/") +
      "），逐模块取 §8.5 函数定义 + §8.6/8.5 回调与调度函数 + §7.x 错误码 + §10 配置容器 |")
    A("| 实现基准 | 本仓库 " + q("mcal/<Mod>/include/<Mod>.h") + " 的公开声明 + " + q("src/*.c") + " 的函数体 |")
    A("| 状态定义 | " + q("已实现") + "＝头文件声明且源文件有函数体；" + q("部分") +
      "＝声明与主路径可用但受限（见备注）；" + q("未实现") + "＝本子集未提供 |")
    A("| DET 检查 | 扫描函数体是否出现 " + q("MCAL_DET_REPORT") + "/" + q("Det_ReportError") +
      "；有打勾，不代表每条 SWS 错误路径都覆盖 |")
    A("| 覆盖率 | （已实现 + 0.5×部分）/ SWS API 总数，仅作进度指标，**不是**认证证据 |")
    A("| 生成日期 | 2026-11-02（R6 MCAL）|")
    A("")
    A("**这不是 AUTOSAR 合规证书。** 本表只说明“SWS 里写了什么、本实现有没有对应函数、有没有 DET 检查”，"
      "不覆盖：时序/并发行为、错误路径完整性、配置参数语义、MISRA/WCET、ASIL 安全论证。"
      "商用 MCAL 的合规性靠“需求追溯 + 静态分析 + 单元/集成测试 + 安全手册 + 认证报告”共同支撑，"
      "本表只对应其中第一项的一个子集。")
    A("")
    t = stats["totals"]
    A("## 1. 总览")
    A("")
    A("表 0-1 13 个模块的 SWS API 覆盖（机器统计）")
    A("")
    A("| 模块 | 模块 ID | SWS API | 已实现 | 部分 | 未实现 | 覆盖率 | API ID 定义 | DET 错误码 | 有 DET 的 API | 代码行数 |")
    A("|---|---|---|---|---|---|---|---|---|---|---|")
    for mod in MODULES:
        m = stats["modules"][mod]
        A("| %s | %s | %d | %d | %d | %d | %.0f%% | %d | %d | %d | %d |" % (
            mod, MODULE_IDS.get(mod, "?"), m["sws_total"], m["implemented"], m["partial"],
            m["missing"], 100 * m["coverage"], m["api_id_defined"], m["det_codes_defined"],
            m["det_checked"], m["signature"].get("loc", 0) if m["signature"] else 0))
    A("| **合计** | | **%d** | **%d** | **%d** | **%d** | **%.0f%%** | | | **%d** | **%d** |" % (
        t["sws_total"], t["implemented"], t["partial"], t["missing"], 100 * t["coverage"],
        t["det_checked"], t["loc"]))
    A("")
    A("表 0-2 分组覆盖率（行＝模块，列＝API 组；格子＝已实现/该组 SWS API 数）")
    A("")
    gnames = [g for g, _ in GROUPS] + ["其他"]
    A("| 模块 | " + " | ".join(gnames) + " |")
    A("|---|" + "---|" * len(gnames))
    for mod in MODULES:
        cells = []
        for g in gnames:
            gd = stats["modules"][mod]["groups"].get(g)
            cells.append("%d/%d" % (gd["implemented"], gd["total"]) if gd else "–")
        A("| %s | %s |" % (mod, " | ".join(cells)))
    A("")
    A("## 2. 逐模块 API 追溯")
    A("")
    for idx, mod in enumerate(MODULES, 1):
        d = details[mod]
        sws = d["sws"]
        hdr = d["hdr"]
        A("### 2.%d %s（模块 ID %s）" % (idx, mod, MODULE_IDS.get(mod, "?")))
        A("")
        if not hdr:
            A("> 本模块头文件尚未落地，以下 SWS 清单为待实现基线。")
            A("")
        else:
            A("- 头文件：" + q(hdr["files"]["header"]) + "；源文件：" + q(hdr["files"]["source"]) +
              "；配置：" + q(hdr["files"]["cfg"]) + "、" + q(hdr["files"]["pbcfg"]))
            v = hdr["vers"]
            up = mod.upper()
            ar = "%s.%s.%s" % (v.get(up + "_AR_RELEASE_MAJOR_VERSION", "?"),
                               v.get(up + "_AR_RELEASE_MINOR_VERSION", "?"),
                               v.get(up + "_AR_RELEASE_REVISION_VERSION", "?"))
            sw = "%s.%s.%s" % (v.get(up + "_SW_MAJOR_VERSION", "?"),
                               v.get(up + "_SW_MINOR_VERSION", "?"),
                               v.get(up + "_SW_PATCH_VERSION", "?"))
            A("- 版本宏：AR %s / SW %s；API ID 常量 %d 个；DET 错误码 %d 个" % (
                ar, sw, len(hdr["api_ids"]), len(hdr["dets"])))
            A("- 配置：pre-compile 开关 %d 个，post-build 结构字段 %d 个（%s）" % (
                hdr["cfg_switches"], hdr["cfg_fields"], q(mod + "_ConfigType")))
            if sws:
                A("- SWS 要求：API %d 个、回调/调度 %d 个、DET 错误码 %d 个、配置容器 %d 个" % (
                    len(sws["apis"]), len(sws.get("callbacks") or []) + len(sws.get("scheduled") or []),
                    len(sws["dev_errors"]), len(sws["containers"])))
            A("")
        A("| SWS API | Service ID | Sync/Async | 本实现 | 实现状态 | DET | 备注 |")
        A("|---|---|---|---|---|---|---|")
        for r in d["rows"]:
            A("| " + q(r["api"]) + " | %s | %s | %s | %s | %s | %s |" % (
                r["sid"] or "–", r["sync"] or "–",
                q(r["ours"]) if r["ours"] else "–",
                fmt_status(r["status"]), "√" if r["det"] else ("–" if not r["ours"] else "×"),
                r["note"] or ""))
        for r in d["extra"]:
            A("| " + q(r["api"]) + " | – | %s | %s | %s | – | SWS 8.4/8.5 |" % (
                "async" if "MainFunction" in r["api"] else "回调",
                q(r["ours"]) if r["ours"] else "–", fmt_status(r["status"])))
        A("")
        sm = manual.get("state_machine", {}).get(mod)
        if sm:
            A("**状态机/生命周期（SWS §7.1 摘要）**")
            A("")
            A(sm)
            A("")
        cn = manual.get("config_notes", {}).get(mod)
        if cn:
            A("**配置参数对应**")
            A("")
            A(cn)
            A("")
        ver = manual.get("verified", {}).get(mod)
        if ver:
            A("**验证状态**")
            A("")
            A("| 例程 | 上板 | 日志 | 量化数据 |")
            A("|---|---|---|---|")
            for s in ver:
                A("| " + q(s.get("sample", "")) + " | %s | %s | %s |" % (
                    s.get("status", ""), q(s.get("log", "")), s.get("data", "")))
            A("")

    A("## 3. 与 SWS 的已知偏差清单")
    A("")
    A("> 原则：只列**能指着文件说清**的偏差，不写“基本符合”这类模糊结论。每条给影响与后续动作；"
      "“后续动作”列不做承诺，只写可行路径。")
    A("")
    A("| # | 偏差项 | SWS 依据 | 本实现现状 | 影响 | 后续动作 |")
    A("|---|---|---|---|---|---|")
    for i, dv in enumerate(manual.get("deviations", []), 1):
        A("| %d | %s | %s | %s | %s | %s |" % (
            i, dv.get("item", ""), dv.get("sws", ""), dv.get("now", ""),
            dv.get("impact", ""), dv.get("plan", "")))
    A("")
    A("## 4. 机器可读统计")
    A("")
    A(q("docs/mcal/traceability_stats.json") + " 由同一脚本输出，第 50 章图 50-2 直接读取该文件。")
    A("")
    A(FENCE + "json")
    A(json.dumps({k: stats[k] for k in ("totals", "source")}, ensure_ascii=False, indent=2))
    A(FENCE)
    A("")
    A("## 5. 复跑方法")
    A("")
    A(FENCE + "bash")
    A("# 1) 重新解析 SWS 抽取文本（Windows 侧，产物为 docs/mcal/sws_api_data.json）")
    A("#    python parse_sws.py")
    A("# 2) 重新生成追溯矩阵与统计")
    A("python3 docs/mcal/gen_traceability.py")
    A("# 3) 只做检查不写文件")
    A("python3 docs/mcal/gen_traceability.py --check")
    A(FENCE)
    A("")
    return "\n".join(L) + "\n"


def main(argv):
    manual = {}
    if MANUAL_JSON.exists():
        manual = json.loads(MANUAL_JSON.read_text(encoding="utf-8"))
    stats, details = build(manual)
    if "--check" in argv:
        t = stats["totals"]
        print("SWS API %d | 已实现 %d | 部分 %d | 未实现 %d | 覆盖率 %.1f%% | 代码 %d 行" % (
            t["sws_total"], t["implemented"], t["partial"], t["missing"],
            100 * t["coverage"], t["loc"]))
        for mod in MODULES:
            m = stats["modules"][mod]
            print("  %-4s %d/%d 部分 %d 缺失 %d cov=%.0f%%" % (
                mod, m["implemented"], m["sws_total"], m["partial"], m["missing"], 100 * m["coverage"]))
        return 0
    OUT_STATS.write_text(json.dumps(stats, ensure_ascii=False, indent=1), encoding="utf-8")
    OUT_MD.write_text(render(stats, details, manual), encoding="utf-8")
    t = stats["totals"]
    print("wrote %s / %s" % (OUT_MD.relative_to(ROOT), OUT_STATS.relative_to(ROOT)))
    print("SWS API %d | 已实现 %d | 部分 %d | 未实现 %d | 覆盖率 %.1f%%" % (
        t["sws_total"], t["implemented"], t["partial"], t["missing"], 100 * t["coverage"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))