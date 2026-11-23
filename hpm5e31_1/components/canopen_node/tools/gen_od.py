#!/usr/bin/env python3
"""Generate the CANopen object dictionary for the hpm5e31_1 CANopen node.

Single source of truth -> three generated artifacts:
    components/canopen_node/od/OD.h    (CANopenNode v4 object dictionary, C header)
    components/canopen_node/od/OD.c    (CANopenNode v4 object dictionary, C source)
    host/canopen/node1.eds             (EDS for python-canopen / CANopenEditor)

Usage:
    python3 components/canopen_node/tools/gen_od.py          # regenerate
    python3 components/canopen_node/tools/gen_od.py --check  # verify files are up to date

NOTE: OD.h/OD.c follow the layout produced by CANopenEditor v4 (same struct/entry naming),
so they can be replaced by CANopenEditor output later without touching the rest of the port.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
COMP = os.path.dirname(HERE)                      # components/canopen_node
REPO = os.path.dirname(os.path.dirname(COMP))     # repo root
OUT_ODH = os.path.join(COMP, "od", "OD.h")
OUT_ODC = os.path.join(COMP, "od", "OD.c")
OUT_EDS = os.path.join(REPO, "host", "canopen", "node1.eds")

CTYPE_SIZE = {"uint8_t": 1, "uint16_t": 2, "uint32_t": 4, "uint64_t": 8}
VENDOR_ID = 0x0000A531
PRODUCT_CODE = 0x0000E531
REVISION = 0x00010000
SERIAL = 0x00000001

OBJECTS = []


def var(idx, name, group, ctype, default, attr, eds_name, eds_type, access,
        pdo=0, data_len=None, is_str=False):
    OBJECTS.append(dict(kind="VAR", idx=idx, name=name, group=group, ctype=ctype,
                        default=default, attr=attr, eds_name=eds_name,
                        eds_type=eds_type, access=access, pdo=pdo,
                        is_str=is_str,
                        data_len=data_len if data_len is not None else CTYPE_SIZE[ctype]))


def arr(idx, name, group, ctype, count, attr0, attr, eds_name, eds_type,
        access0, access, default0, pdo=0):
    OBJECTS.append(dict(kind="ARR", idx=idx, name=name, group=group, ctype=ctype,
                        count=count, attr0=attr0, attr=attr, eds_name=eds_name,
                        eds_type=eds_type, access0=access0, access=access,
                        default0=default0, pdo=pdo))


def rec(idx, name, group, fields, eds_name):
    OBJECTS.append(dict(kind="REC", idx=idx, name=name, group=group,
                        fields=fields, eds_name=eds_name))


def f(sub, fname, ctype, default, attr, eds_name, eds_type, access):
    return dict(sub=sub, fname=fname, ctype=ctype, default=default, attr=attr,
                eds_name=eds_name, eds_type=eds_type, access=access)


# --------------------------------------------------------------------------------------
# 对象字典定义（按索引升序）
# 标准区 0x1000-0x1FFF：CiA 301 通信参数；厂商区 0x2000-0x2FFF：应用/测试对象
# --------------------------------------------------------------------------------------
var(0x1000, "deviceType", "PERSIST", "uint32_t", "0x00000191", "ODA_SDO_R | ODA_MB",
    "Device type", "0x0007", "ro")
var(0x1001, "errorRegister", "RAM", "uint8_t", "0x00", "ODA_SDO_R | ODA_TPDO",
    "Error register", "0x0005", "ro", pdo=1)
arr(0x1003, "pre_definedErrorField", "RAM", "uint32_t", 16, "ODA_SDO_RW",
    "ODA_SDO_R | ODA_MB", "Pre-defined error field", "0x0007", "rw", "ro", "0x10")
var(0x1005, "COB_ID_SYNCMessage", "PERSIST", "uint32_t", "0x00000080",
    "ODA_SDO_RW | ODA_MB", "COB-ID SYNC message", "0x0007", "rw")
var(0x1006, "communicationCyclePeriod", "PERSIST", "uint32_t", "0x00000000",
    "ODA_SDO_RW | ODA_MB", "Communication cycle period", "0x0007", "rw")
var(0x1007, "synchronousWindowLength", "PERSIST", "uint32_t", "0x00000000",
    "ODA_SDO_RW | ODA_MB", "Synchronous window length", "0x0007", "rw")
var(0x1008, "manufacturerDeviceName", "PERSIST", "uint8_t", '"HPM5E31 CANopen node 1"',
    "ODA_SDO_R", "Manufacturer device name", "0x0009", "ro", data_len=32, is_str=True)
var(0x1009, "manufacturerHardwareVersion", "PERSIST", "uint8_t", '"hpm5e31_1 rev1"',
    "ODA_SDO_R", "Manufacturer hardware version", "0x0009", "ro", data_len=16, is_str=True)
var(0x100A, "manufacturerSoftwareVersion", "PERSIST", "uint8_t", '"CANopenNode v4.0"',
    "ODA_SDO_R", "Manufacturer software version", "0x0009", "ro", data_len=16, is_str=True)
var(0x1012, "COB_IDTimeStampObject", "PERSIST", "uint32_t", "0x00000100",
    "ODA_SDO_RW | ODA_MB", "COB-ID time stamp object", "0x0007", "rw")
var(0x1014, "COB_ID_EMCY", "PERSIST", "uint32_t", "0x00000080",
    "ODA_SDO_RW | ODA_MB", "COB-ID EMCY", "0x0007", "rw")
var(0x1015, "inhibitTimeEMCY", "PERSIST", "uint16_t", "0x0000",
    "ODA_SDO_RW | ODA_MB", "Inhibit time EMCY", "0x0006", "rw")
arr(0x1016, "consumerHeartbeatTime", "PERSIST", "uint32_t", 8, "ODA_SDO_R",
    "ODA_SDO_RW | ODA_MB", "Consumer heartbeat time", "0x0007", "ro", "rw", "0x08")
var(0x1017, "producerHeartbeatTime", "PERSIST", "uint16_t", "0x03E8",
    "ODA_SDO_RW | ODA_MB", "Producer heartbeat time", "0x0006", "rw")
rec(0x1018, "identity", "PERSIST", [
    f(0, "highestSub_indexSupported", "uint8_t", "0x04", "ODA_SDO_R",
      "Highest sub-index supported", "0x0005", "ro"),
    f(1, "vendor_ID", "uint32_t", "0x%08X" % VENDOR_ID, "ODA_SDO_R | ODA_MB",
      "Vendor-ID", "0x0007", "ro"),
    f(2, "productCode", "uint32_t", "0x%08X" % PRODUCT_CODE, "ODA_SDO_R | ODA_MB",
      "Product code", "0x0007", "ro"),
    f(3, "revisionNumber", "uint32_t", "0x%08X" % REVISION, "ODA_SDO_R | ODA_MB",
      "Revision number", "0x0007", "ro"),
    f(4, "serialNumber", "uint32_t", "0x%08X" % SERIAL, "ODA_SDO_R | ODA_MB",
      "Serial number", "0x0007", "ro"),
], "Identity")
var(0x1019, "synchronousCounterOverflowValue", "PERSIST", "uint8_t", "0x00",
    "ODA_SDO_RW", "Synchronous counter overflow value", "0x0005", "rw")
rec(0x1200, "SDOServerParameter", "RAM", [
    f(0, "highestSub_indexSupported", "uint8_t", "0x02", "ODA_SDO_R",
      "Highest sub-index supported", "0x0005", "ro"),
    f(1, "COB_IDClientToServerRx", "uint32_t", "0x00000600",
      "ODA_SDO_R | ODA_TPDO | ODA_MB", "COB-ID client to server", "0x0007", "ro"),
    f(2, "COB_IDServerToClientTx", "uint32_t", "0x00000580",
      "ODA_SDO_R | ODA_TPDO | ODA_MB", "COB-ID server to client", "0x0007", "ro"),
], "SDO server parameter")


def rpdo_comm(idx, cob_id, name, trans=0xFF, event_timer=0):
    rec(idx, name, "PERSIST", [
        f(0, "highestSub_indexSupported", "uint8_t", "0x05", "ODA_SDO_R",
          "Highest sub-index supported", "0x0005", "ro"),
        f(1, "COB_IDUsedByRPDO", "uint32_t", "0x%08X" % cob_id,
          "ODA_SDO_RW | ODA_MB", "COB-ID used by RPDO", "0x0007", "rw"),
        f(2, "transmissionType", "uint8_t", "0x%02X" % trans, "ODA_SDO_RW",
          "Transmission type", "0x0005", "rw"),
        f(5, "eventTimer", "uint16_t", "0x%04X" % event_timer, "ODA_SDO_RW | ODA_MB",
          "Event timer", "0x0006", "rw"),
    ], "RPDO communication parameter")


def tpdo_comm(idx, cob_id, name, trans=0xFF, inhibit=0, event_timer=0):
    rec(idx, name, "PERSIST", [
        f(0, "highestSub_indexSupported", "uint8_t", "0x06", "ODA_SDO_R",
          "Highest sub-index supported", "0x0005", "ro"),
        f(1, "COB_IDUsedByTPDO", "uint32_t", "0x%08X" % cob_id,
          "ODA_SDO_RW | ODA_MB", "COB-ID used by TPDO", "0x0007", "rw"),
        f(2, "transmissionType", "uint8_t", "0x%02X" % trans, "ODA_SDO_RW",
          "Transmission type", "0x0005", "rw"),
        f(3, "inhibitTime", "uint16_t", "0x%04X" % inhibit, "ODA_SDO_RW | ODA_MB",
          "Inhibit time", "0x0006", "rw"),
        f(5, "eventTimer", "uint16_t", "0x%04X" % event_timer, "ODA_SDO_RW | ODA_MB",
          "Event timer", "0x0006", "rw"),
        f(6, "SYNCStartValue", "uint8_t", "0x00", "ODA_SDO_RW",
          "SYNC start value", "0x0005", "rw"),
    ], "TPDO communication parameter")


def pdo_map(idx, name, mapping=()):
    fields = [f(0, "numberOfMappedApplicationObjectsInPDO", "uint8_t", "0x%02X" % len(mapping),
                "ODA_SDO_RW", "Number of mapped objects", "0x0005", "rw")]
    for n in range(1, 9):
        if n <= len(mapping):
            m_idx, m_sub, m_bits = mapping[n - 1]
            default = "0x%08X" % ((m_idx << 16) | (m_sub << 8) | m_bits)
        else:
            default = "0x00000000"
        fields.append(f(n, "applicationObject%d" % n, "uint32_t", default,
                        "ODA_SDO_RW | ODA_MB", "Application object %d" % n,
                        "0x0007", "rw"))
    rec(idx, name, "PERSIST", fields, "PDO mapping parameter")


# 默认 PDO 链接（与 samples/app/canopen_pdo 一致）：
#   RPDO1(0x201) <- 0x2100:0 u16  LED/GPIO
#   RPDO2(0x301) <- 0x2103:0 u32  应用循环计数
#   TPDO1(0x181) -> 0x2101:0 u16 + 0x2102:0 u32   ADC 采样 + PDO 计数（同步型，每个 SYNC 发送）
#   TPDO2(0x281) -> 0x2102:0 u32 + 0x2104:0 u16   计数 + SYNC 计数（事件型，1000 ms）
rpdo_comm(0x1400, 0x00000200, "RPDOCommunicationParameter", trans=0xFF)
rpdo_comm(0x1401, 0x00000300, "RPDOCommunicationParameter", trans=0xFF)
pdo_map(0x1600, "RPDOMappingParameter", [(0x2100, 0, 16)])
pdo_map(0x1601, "RPDOMappingParameter", [(0x2103, 0, 32)])
tpdo_comm(0x1800, 0x00000180, "TPDOCommunicationParameter", trans=0x01)
tpdo_comm(0x1801, 0x00000280, "TPDOCommunicationParameter", trans=0xFE, event_timer=1000)
pdo_map(0x1A00, "TPDOMappingParameter", [(0x2101, 0, 16), (0x2102, 0, 32)])
pdo_map(0x1A01, "TPDOMappingParameter", [(0x2102, 0, 32), (0x2104, 0, 16)])

# ------------------------------- 厂商区 0x2000-0x2FFF -------------------------------
arr(0x2000, "vendorArray1k", "RAM", "uint64_t", 128, "ODA_SDO_R",
    "ODA_SDO_RW | ODA_MB", "Vendor array 1 KB (128 x UNSIGNED64)", "0x001B",
    "ro", "rw", "0x80")
var(0x2001, "vendorDomain1k", "RAM", "uint8_t", None, "ODA_SDO_RW",
    "Vendor domain 1 KB (segmented SDO)", "0x000F", "rw", data_len=1024)
var(0x2002, "vendorDomain4k", "RAM", "uint8_t", None, "ODA_SDO_RW",
    "Vendor domain 4 KB (segmented SDO)", "0x000F", "rw", data_len=4096)
var(0x2100, "ledState", "RAM", "uint16_t", "0x0000",
    "ODA_SDO_RW | ODA_TRPDO | ODA_MB", "LED / GPIO state (RPDO1)", "0x0006", "rw", pdo=1)
var(0x2101, "adcSample", "RAM", "uint16_t", "0x0000",
    "ODA_SDO_R | ODA_TPDO | ODA_MB", "ADC channel 0 sample (TPDO)", "0x0006", "ro", pdo=1)
var(0x2102, "pdoCounter", "RAM", "uint32_t", "0x00000000",
    "ODA_SDO_R | ODA_TPDO | ODA_MB", "PDO counter (TPDO)", "0x0007", "ro", pdo=1)
var(0x2103, "loopCounter", "RAM", "uint32_t", "0x00000000",
    "ODA_SDO_RW | ODA_TRPDO | ODA_MB", "Application loop counter (RPDO)", "0x0007", "rw", pdo=1)
var(0x2104, "syncCounter", "RAM", "uint16_t", "0x0000",
    "ODA_SDO_R | ODA_TPDO | ODA_MB", "SYNC counter (TPDO)", "0x0006", "ro", pdo=1)
var(0x2105, "lastSdoIndex", "RAM", "uint16_t", "0x0000",
    "ODA_SDO_R | ODA_TPDO | ODA_MB", "Last SDO index accessed", "0x0006", "ro", pdo=1)
var(0x2106, "emcyTrigger", "RAM", "uint32_t", "0x00000000",
    "ODA_SDO_RW | ODA_MB", "写非 0 触发 EMCY（测试钩子）", "0x0007", "rw")
var(0x2107, "emcyCount", "RAM", "uint32_t", "0x00000000",
    "ODA_SDO_R | ODA_TPDO | ODA_MB", "EMCY 注入计数", "0x0007", "ro", pdo=1)

OBJECTS.sort(key=lambda o: o["idx"])
BY_IDX = {o["idx"]: o for o in OBJECTS}

HEADER = """\
/*******************************************************************************
    CANopen Object Dictionary definition for CANopenNode V4

    本文件由 components/canopen_node/tools/gen_od.py 生成，请勿手工修改。
    Single source of truth: tools/gen_od.py 中的 OBJECTS 表。
    Upstream format: CANopenEditor v4 (https://github.com/CANopenNode/CANopenEditor)

    Device: HPM5E31 CANopen node (CiA 301 communication profile)
    Vendor-ID 0x%08X  Product 0x%08X  Revision 0x%08X
*******************************************************************************/
""" % (VENDOR_ID, PRODUCT_CODE, REVISION)


def gen_odh():
    out = [HEADER, "#ifndef OD_H", "#define OD_H", ""]
    out.append("/*******************************************************************************")
    out.append("    Counters of OD objects")
    out.append("*******************************************************************************/")
    counts = [
        ("NMT", 1), ("EM", 1), ("EM_PROD", 1), ("SYNC", 1), ("SYNC_PROD", 1),
        ("TIME", 1), ("HB_CONS", 1), ("HB_PROD", 1), ("SDO_SRV", 1), ("SDO_CLI", 0),
        ("RPDO", 2), ("TPDO", 2), ("LEDS", 1), ("STORAGE", 0),
    ]
    for k, v in counts:
        out.append("#define OD_CNT_%s %d" % (k, v))
    out.append("")
    out.append("#define OD_CNT_ARR_1003 16")
    out.append("#define OD_CNT_ARR_1016 8")
    out.append("")
    out.append("/*******************************************************************************")
    out.append("    Sizes of OD arrays")
    out.append("*******************************************************************************/")
    out.append("/*******************************************************************************")
    out.append("    OD data declaration of all groups")
    out.append("*******************************************************************************/")
    for group in ("PERSIST", "RAM"):
        out.append("typedef struct {")
        for o in OBJECTS:
            if o["group"] != group:
                continue
            cidx = "x%04X_%s" % (o["idx"], o["name"])
            if o["kind"] == "VAR":
                if o["data_len"] != CTYPE_SIZE[o["ctype"]]:
                    out.append("    %s %s[%d];" % (o["ctype"], cidx, o["data_len"]))
                else:
                    out.append("    %s %s;" % (o["ctype"], cidx))
            elif o["kind"] == "ARR":
                out.append("    uint8_t %s_sub0;" % cidx)
                out.append("    %s %s[%d];" % (o["ctype"], cidx, o["count"]))
            else:
                out.append("    struct {")
                for fld in o["fields"]:
                    out.append("        %s %s;" % (fld["ctype"], fld["fname"]))
                out.append("    } %s;" % cidx)
        out.append("} OD_%s_COMM_t;" % group if group == "PERSIST" else "} OD_RAM_t;")
        out.append("")
    out.append("#ifndef OD_ATTR_PERSIST_COMM")
    out.append("#define OD_ATTR_PERSIST_COMM")
    out.append("#endif")
    out.append("extern OD_ATTR_PERSIST_COMM OD_PERSIST_COMM_t OD_PERSIST_COMM;")
    out.append("")
    out.append("#ifndef OD_ATTR_RAM")
    out.append("#define OD_ATTR_RAM")
    out.append("#endif")
    out.append("extern OD_ATTR_RAM OD_RAM_t OD_RAM;")
    out.append("")
    out.append("#ifndef OD_ATTR_OD")
    out.append("#define OD_ATTR_OD")
    out.append("#endif")
    out.append("extern OD_ATTR_OD OD_t *OD;")
    out.append("")
    out.append("/*******************************************************************************")
    out.append("    Object dictionary entries - shortcuts")
    out.append("*******************************************************************************/")
    for i, o in enumerate(OBJECTS):
        out.append("#define OD_ENTRY_H%04X &OD->list[%d]" % (o["idx"], i))
    out.append("")
    out.append("/*******************************************************************************")
    out.append("    Object dictionary entries - shortcuts with names")
    out.append("*******************************************************************************/")
    for i, o in enumerate(OBJECTS):
        out.append("#define OD_ENTRY_H%04X_%s &OD->list[%d]" % (o["idx"], o["name"], i))
    out.append("")
    out.append("#endif /* OD_H */")
    return "\n".join(out) + "\n"


def initializer(o, indent):
    pad = " " * indent
    if o["kind"] == "VAR":
        if o["default"] is None:
            return None
        return pad + ".x%04X_%s = %s," % (o["idx"], o["name"], o["default"])
    if o["kind"] == "ARR":
        lines = [pad + ".x%04X_%s_sub0 = %s," % (o["idx"], o["name"], o["default0"])]
        if int(o["default0"], 0) > 8:
            lines.append(pad + ".x%04X_%s = {0}," % (o["idx"], o["name"]))
        else:
            vals = ", ".join(["0"] * o["count"])
            lines.append(pad + ".x%04X_%s = {%s}," % (o["idx"], o["name"], vals))
        return "\n".join(lines)
    lines = [pad + ".x%04X_%s = {" % (o["idx"], o["name"])]
    for fld in o["fields"]:
        lines.append(pad + "    .%s = %s," % (fld["fname"], fld["default"]))
    lines.append(pad + "},")
    return "\n".join(lines)


def gen_odc():
    out = [HEADER, '#define OD_DEFINITION', '#include "301/CO_ODinterface.h"', '#include "OD.h"', ""]
    out.append("#if CO_VERSION_MAJOR < 4")
    out.append('#error This Object dictionary is compatible with CANopenNode V4.0 and above!')
    out.append("#endif")
    out.append("")
    out.append("/*******************************************************************************")
    out.append("    OD data initialization of all groups")
    out.append("*******************************************************************************/")
    for group in ("PERSIST", "RAM"):
        out.append("OD_ATTR_PERSIST_COMM OD_PERSIST_COMM_t OD_PERSIST_COMM = {"
                      if group == "PERSIST" else "OD_ATTR_RAM OD_RAM_t OD_RAM = {")
        for o in OBJECTS:
            if o["group"] != group:
                continue
            s = initializer(o, 4)
            if s:
                out.append(s)
        out.append("};")
        out.append("")
    out.append("")
    out.append("/*******************************************************************************")
    out.append("    All OD objects (constant definitions)")
    out.append("*******************************************************************************/")
    out.append("typedef struct {")
    for o in OBJECTS:
        cidx = "o_%04X_%s" % (o["idx"], o["name"])
        if o["kind"] == "VAR":
            out.append("    OD_obj_var_t %s;" % cidx)
        elif o["kind"] == "ARR":
            out.append("    OD_obj_array_t %s;" % cidx)
        else:
            out.append("    OD_obj_record_t %s[%d];" % (cidx, len(o["fields"])))
    out.append("} ODObjs_t;")
    out.append("")
    out.append("static ODObjs_t ODObjs = {")
    for o in OBJECTS:
        cidx = "o_%04X_%s" % (o["idx"], o["name"])
        cname = "x%04X_%s" % (o["idx"], o["name"])
        grp = "OD_PERSIST_COMM" if o["group"] == "PERSIST" else "OD_RAM"
        out.append("    .%s = {" % cidx)
        if o["kind"] == "VAR":
            out.append("        .dataOrig = &%s.%s," % (grp, cname))
            out.append("        .attribute = %s," % o["attr"])
            out.append("        .dataLength = %d" % o["data_len"])
            out.append("    },")
        elif o["kind"] == "ARR":
            out.append("        .dataOrig0 = &%s.%s_sub0," % (grp, cname))
            out.append("        .dataOrig = &%s.%s[0]," % (grp, cname))
            out.append("        .attribute0 = %s," % o["attr0"])
            out.append("        .attribute = %s," % o["attr"])
            out.append("        .dataElementLength = %d," % CTYPE_SIZE[o["ctype"]])
            out.append("        .dataElementSizeof = sizeof(%s)" % o["ctype"])
            out.append("    },")
        else:
            for fld in o["fields"]:
                out.append("        {")
                out.append("            .dataOrig = &%s.%s.%s," % (grp, cname, fld["fname"]))
                out.append("            .subIndex = %d," % fld["sub"])
                out.append("            .attribute = %s," % fld["attr"])
                out.append("            .dataLength = %d" % CTYPE_SIZE[fld["ctype"]])
                out.append("        },")
            out.append("    },")
    out.append("};")
    out.append("")
    out.append("")
    out.append("/*******************************************************************************")
    out.append("    Object dictionary")
    out.append("*******************************************************************************/")
    out.append("static OD_ATTR_OD OD_entry_t ODList[] = {")
    for o in OBJECTS:
        cidx = "o_%04X_%s" % (o["idx"], o["name"])
        if o["kind"] == "VAR":
            typ, n = "ODT_VAR", 1
        elif o["kind"] == "ARR":
            typ, n = "ODT_ARR", o["count"] + 1
        else:
            typ, n = "ODT_REC", len(o["fields"])
        out.append("    {0x%04X, 0x%02X, %s, &ODObjs.%s, NULL}," % (o["idx"], n, typ, cidx))
    out.append("    {0x0000, 0x00, 0, NULL, NULL}")
    out.append("};")
    out.append("")
    out.append("static OD_t _OD = {")
    out.append("    (sizeof(ODList) / sizeof(ODList[0])) - 1,")
    out.append("    &ODList[0]")
    out.append("};")
    out.append("")
    out.append("OD_t *OD = &_OD;")
    return "\n".join(out) + "\n"


def gen_eds():
    out = []
    out.append("[FileInfo]")
    out.append("FileName=node1.eds")
    out.append("FileVersion=1")
    out.append("FileRevision=1")
    out.append("EDSVersion=4.0")
    out.append("Description=HPM5E31 CANopen node 1 (CiA 301) - generated by gen_od.py")
    out.append("CreationTime=12:00PM")
    out.append("CreationDate=01-01-2026")
    out.append("CreatedBy=hpm5e31_1 tp-canopen")
    out.append("ModificationTime=12:00PM")
    out.append("ModificationDate=01-01-2026")
    out.append("ModifiedBy=hpm5e31_1 tp-canopen")
    out.append("")
    out.append("[DeviceInfo]")
    out.append("VendorName=HPM5E31 demo")
    out.append("VendorNumber=%d" % VENDOR_ID)
    out.append("ProductName=HPM5E31 CANopen node1")
    out.append("ProductNumber=%d" % PRODUCT_CODE)
    out.append("RevisionNumber=%d" % REVISION)
    for rate in (10, 20, 50, 125, 250, 500, 800, 1000):
        out.append("BaudRate_%d=%d" % (rate, 1 if rate == 500 else 0))
    out.append("SimpleBootUpMaster=0")
    out.append("SimpleBootUpSlave=1")
    out.append("Granularity=8")
    out.append("DynamicChannelsSupported=0")
    out.append("CompactPDO=0")
    out.append("GroupMessaging=0")
    out.append("NrOfRXPDO=2")
    out.append("NrOfTXPDO=2")
    out.append("LSS_Supported=0")
    out.append("")
    out.append("[DummyUsage]")
    for i in range(1, 8):
        out.append("Dummy%04d=%d" % (i, 0 if i == 1 else 1))
    out.append("")
    out.append("[Comments]")
    out.append("Lines=0")
    out.append("")
    mandatory = [0x1000, 0x1001, 0x1018]
    optional = [o["idx"] for o in OBJECTS if o["idx"] < 0x2000 and o["idx"] not in mandatory]
    manuf = [o["idx"] for o in OBJECTS if o["idx"] >= 0x2000]
    out.append("[MandatoryObjects]")
    out.append("SupportedObjects=%d" % len(mandatory))
    for i, idx in enumerate(mandatory, 1):
        out.append("%d=0x%04X" % (i, idx))
    out.append("")
    out.append("[OptionalObjects]")
    out.append("SupportedObjects=%d" % len(optional))
    for i, idx in enumerate(optional, 1):
        out.append("%d=0x%04X" % (i, idx))
    out.append("")
    out.append("[ManufacturerObjects]")
    out.append("SupportedObjects=%d" % len(manuf))
    for i, idx in enumerate(manuf, 1):
        out.append("%d=0x%04X" % (i, idx))
    out.append("")

    def sub_block(sec, name, otype, dtype, access, default, pdo):
        out.append("[%s]" % sec)
        out.append("ParameterName=%s" % name)
        out.append("ObjectType=0x%X" % otype)
        if dtype is not None:
            out.append("DataType=0x%04X" % dtype)
        out.append("AccessType=%s" % access)
        if default is not None:
            out.append("DefaultValue=%s" % default)
        out.append("PDOMapping=%d" % pdo)
        out.append("")

    for o in OBJECTS:
        idx = o["idx"]
        if o["kind"] == "VAR":
            dtype = int(o["eds_type"], 0)
            out.append("[%04X]" % idx)
            out.append("ParameterName=%s" % o["eds_name"])
            out.append("ObjectType=0x7")
            out.append("DataType=0x%04X" % dtype)
            out.append("AccessType=%s" % o["access"])
            if o["default"] is not None:
                out.append("DefaultValue=%s" % o["default"].strip('"'))
            out.append("PDOMapping=%d" % o["pdo"])
            out.append("")
        elif o["kind"] == "ARR":
            out.append("[%04X]" % idx)
            out.append("ParameterName=%s" % o["eds_name"])
            out.append("ObjectType=0x8")
            out.append("SubNumber=0x%X" % (o["count"] + 1))
            out.append("")
            sub_block("%04Xsub0" % idx, "Number of entries", 0x7, 0x0005,
                      o["access0"], o["default0"], 0)
            for n in range(1, o["count"] + 1):
                sub_block("%04Xsub%d" % (idx, n), "%s %d" % (o["eds_name"], n), 0x7,
                          int(o["eds_type"], 0), o["access"], "0x00000000", o["pdo"])
        else:
            out.append("[%04X]" % idx)
            out.append("ParameterName=%s" % o["eds_name"])
            out.append("ObjectType=0x9")
            out.append("SubNumber=0x%X" % (len(o["fields"])))
            out.append("")
            for fld in o["fields"]:
                out.append("[%04Xsub%d]" % (idx, fld["sub"]))
                out.append("ParameterName=%s" % fld["eds_name"])
                out.append("ObjectType=0x7")
                out.append("DataType=0x%04X" % int(fld["eds_type"], 0))
                out.append("AccessType=%s" % fld["access"])
                out.append("DefaultValue=%s" % fld["default"])
                out.append("PDOMapping=%d" % (1 if ("TPDO" in fld["attr"] or "TRPDO" in fld["attr"]) else 0))
                out.append("")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="only verify that generated files are up to date")
    args = ap.parse_args()
    files = {OUT_ODH: gen_odh(), OUT_ODC: gen_odc(), OUT_EDS: gen_eds()}
    stale = []
    for path, text in files.items():
        if args.check:
            old = None
            if os.path.exists(path):
                with open(path, "r", encoding="utf-8") as fp:
                    old = fp.read()
            if old != text:
                stale.append(path)
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fp:
            fp.write(text)
        print("generated %s (%d bytes)" % (path, len(text)))
    if args.check:
        if stale:
            print("STALE: " + ", ".join(stale))
            return 1
        print("OD files are up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
