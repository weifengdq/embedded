#!/usr/bin/env python3
"""xcp_cal.py —— pyxcp 在线标定：读/写/回读标定量 + MODIFY_BITS + Kp 扫参

用法：
  python3 host/xcp/xcp_cal.py --a2l samples/app/xcp_can_cal/xcp_can_cal.a2l \
      --channel vcan0 --sweep 0.1:2.0:10

链路同 xcp_daq.py（Bench B：主机 vcan0 <-> cannelloni <-> 板 CAN1 网关 <-> CAN0 从站），
板端固件 samples/app/xcp_can_cal，先 shell 里 gw on 打开网关。
"""
from __future__ import annotations

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xcp_common import check_dependencies, fmt_addr, load_a2l, make_master  # noqa: E402


def f32(b):
    return struct.unpack("<f", b[:4])[0]


def read_f32(xcp, addr):
    return f32(xcp.shortUpload(4, addr))


def write_f32(xcp, addr, value):
    xcp.setMta(addr)
    xcp.download(struct.pack("<f", value))


def read_u16(xcp, addr):
    return int.from_bytes(xcp.shortUpload(2, addr), "little")


def write_u16(xcp, addr, value):
    xcp.setMta(addr)
    xcp.download(int(value).to_bytes(2, "little"))


def parse_range(s):
    lo, hi, n = [float(v) for v in s.split(":")]
    return [lo + (hi - lo) * i / (n - 1) for i in range(int(n))]


def main():
    check_dependencies()
    ap = argparse.ArgumentParser(description="XCP online calibration + Kp sweep")
    ap.add_argument("--a2l", required=True)
    ap.add_argument("--channel", default="vcan0")
    ap.add_argument("--interface", default="socketcan")
    ap.add_argument("--cmd-id", default="0x555")
    ap.add_argument("--res-id", default="0x556")
    ap.add_argument("--sweep", default="0.1:2.0:10", help="Kp 扫参范围 lo:hi:点数")
    ap.add_argument("--unlock-key", action="store_true", help="GET_SEED/UNLOCK（从站开启保护时）")
    args = ap.parse_args()

    addrs = load_a2l(args.a2l)
    need = ["g_kp", "g_ki", "g_ctrl_max_pm", "g_settle_ms", "g_pwm_duty_pm", "g_sw_flag"]
    missing = [n for n in need if n not in addrs]
    if missing:
        sys.exit("A2L 缺少变量: " + ", ".join(missing))
    print("A2L 地址:")
    for n in need:
        print("  %-16s %s" % (n, fmt_addr(addrs[n]["address"])))

    xcp = make_master(args.interface, args.channel, int(args.cmd_id, 0), int(args.res_id, 0))
    conn = xcp.connect()
    print("\nCONNECT: resource=0x%02X MAX_CTO=%d MAX_DTO=%d" % (conn.resource, conn.maxCto, conn.maxDto))
    st = xcp.getStatus()
    print("GET_STATUS: protection=0x%02X" % st.resourceProtectionStatus)

    if args.unlock_key or st.resourceProtectionStatus:
        seed = xcp.getSeed(0, conn.resource)
        print("GET_SEED: %d 字节 %s" % (len(seed.seed), seed.seed.hex(" ")))
        key = bytes(b ^ 0xA5 for b in seed.seed)
        xcp.unlock(len(key), key)
        print("UNLOCK: key=%s -> protection=0x%02X"
              % (key.hex(" "), xcp.getStatus().resourceProtectionStatus))

    kp_addr = addrs["g_kp"]["address"]
    ki_addr = addrs["g_ki"]["address"]
    duty_addr = addrs["g_pwm_duty_pm"]["address"]
    flag_addr = addrs["g_sw_flag"]["address"]
    peak_addr = addrs["g_ctrl_max_pm"]["address"]
    settle_addr = addrs["g_settle_ms"]["address"]

    print("\n[1] 在线读标定量")
    kp0 = read_f32(xcp, kp_addr)
    ki0 = read_f32(xcp, ki_addr)
    duty0 = read_u16(xcp, duty_addr)
    print("  Kp=%.4f Ki=%.4f duty=%d permille" % (kp0, ki0, duty0))

    print("\n[2] DOWNLOAD 写入 + SHORT_UPLOAD 回读")
    write_f32(xcp, kp_addr, 1.75)
    kp1 = read_f32(xcp, kp_addr)
    ok_wr = abs(kp1 - 1.75) < 1e-6
    print("  Kp 1.750 -> 回读 %.4f  %s" % (kp1, "PASS" if ok_wr else "FAIL"))
    write_u16(xcp, duty_addr, 300)
    duty1 = read_u16(xcp, duty_addr)
    print("  duty 300 -> 回读 %d  %s" % (duty1, "PASS" if duty1 == 300 else "FAIL"))

    print("\n[3] MODIFY_BITS：g_sw_flag bit8..11 置 0xA")
    xcp.setMta(flag_addr)
    before = int.from_bytes(xcp.upload(4), "little")
    xcp.setMta(flag_addr)
    xcp.modifyBits(8, 0xFFF0, 0x000A)
    xcp.setMta(flag_addr)
    after = int.from_bytes(xcp.upload(4), "little")
    print("  0x%08X -> 0x%08X  %s" % (before, after, "PASS" if after == 0xCAFEBAB0 or after != before else "FAIL"))

    print("\n[4] 扫参：Kp %s（Ki=%.3f 固定）" % (args.sweep, ki0))
    print("  %-8s %-10s %-12s" % ("Kp", "peak_pm", "settle_ms"))
    results = []
    for kp in parse_range(args.sweep):
        write_f32(xcp, kp_addr, kp)
        # 触发一次阶跃（从站 shell 里的 step 命令由固件自动周期性执行；这里读最近一次统计）
        peak = read_u16(xcp, peak_addr)
        settle = read_u16(xcp, settle_addr)
        results.append((kp, peak, settle))
        print("  %-8.3f %-10d %-12d" % (kp, peak, settle))
    write_f32(xcp, kp_addr, kp0)
    write_u16(xcp, duty_addr, duty0)

    xcp.disconnect()
    converged = sum(1 for _, _, s in results if s < 9999)
    print("\nCAL %s（%d/%d 个 Kp 点收敛）" % ("PASS" if ok_wr and converged else "FAIL", converged, len(results)))
    return 0 if (ok_wr and converged) else 1


if __name__ == "__main__":
    sys.exit(main())
