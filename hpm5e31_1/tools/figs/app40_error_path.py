#!/usr/bin/env python3
"""tools/figs/app40_error_path.py —— 第 41 章 图 41-6：CAN_ERR_* 错误帧上报通路

说明：gs_usb 协议本身没有"错误帧"专用帧类型；设备把错误帧伪装成一帧经典 CAN 帧，
在 can_id 里置 CAN_ERR_FLAG(0x20000000) 并带 linux/can/error.h 的类别位，
内核 gs_usb 驱动（gs_usb.c 的 ERROR frames 分支）据此更新 netdev 状态与统计。
本图把"MCAN 状态寄存器 → 固件轮询 → 24 B 错误帧 → 内核 → 用户态"画成一条链路，
右侧列出事件到 can_id/data 的映射（与代码 bridge.c: poll_can_error_state() 对应）。
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

setup()
fig, ax = plt.subplots(figsize=(8.8, 6.0))
ax.set_xlim(0, 1)
ax.set_ylim(0, 1)
ax.axis("off")

boxes = [
    (0.86, "MCAN 控制器（4 路）",
     "PSR: BO / EP / EW 状态位，LEC / DLEC 错误码\nECR: TEC（发送）/ REC（接收）错误计数", COLORS["blue"]),
    (0.66, "固件 gs_bridge_poll()",
     "每个主循环轮询 4 路 PSR/ECR：状态变化才上报\n不用错误中断（无 NVIC 依赖，避免 ISR 里抢 USB 环）", COLORS["teal"]),
    (0.46, "gs_usb 错误帧（经典 24 B）",
     "12 B 头 + 8 B 数据 + 4 B 时间戳；echo_id=0xFFFFFFFF\ncan_id = 0x20000000 | 类别位（CAN_ERR_FLAG）", COLORS["orange"]),
    (0.26, "USB EP1 IN（bulk 512 B）",
     "与普通 RX 帧同一条队列；短帧会被内核判 rx_length_errors\n所以必须发满 24 B（本固件 advertise 了 HW_TIMESTAMP）", COLORS["purple"]),
    (0.06, "内核 gs_usb.ko → 用户态",
     "gs_update_state(): can.state / can_stats\nip -details 显示 STATE、candump 显示 CAN_ERR_* 帧", COLORS["green"]),
]
w, h = 0.66, 0.135
for y, title, body, color in boxes:
    ax.add_patch(FancyBboxPatch((0.03, y), w, h,
                                boxstyle="round,pad=0.008,rounding_size=0.012",
                                linewidth=1.2, edgecolor=color, facecolor="white"))
    ax.text(0.045, y + h - 0.028, title, fontsize=10.5, fontweight="bold", color=color, va="top")
    ax.text(0.045, y + h - 0.058, body, fontsize=8.4, color=COLORS["dark"], va="top", linespacing=1.35)

for y in (0.855, 0.655, 0.455, 0.255):
    ax.add_patch(FancyArrowPatch((0.36, y), (0.36, y - 0.055),
                                 arrowstyle="-|>", mutation_scale=13,
                                 linewidth=1.2, color=COLORS["gray"]))

# 右侧映射表
ax.text(0.715, 0.93, "事件 → can_id / data", fontsize=10.5, fontweight="bold", color=COLORS["dark"])
rows = [
    ("bus-off", "0x40  CAN_ERR_BUSOFF", "state=3"),
    ("error warning", "0x04  CAN_ERR_CRTL", "data[1]=0x08/0x04"),
    ("error passive", "0x04  CAN_ERR_CRTL", "data[1]=0x20/0x10"),
    ("恢复 active", "0x04  CAN_ERR_CRTL", "data[1]=0"),
    ("重启成功", "0x100 CAN_ERR_RESTARTED", "state=0"),
    ("计数", "0x200 CAN_ERR_CNT", "data[6..7]=TEC/REC"),
]
yy = 0.87
for ev, cid, st in rows:
    ax.text(0.715, yy, ev, fontsize=8.0, color=COLORS["dark"])
    ax.text(0.855, yy, cid, fontsize=8.0, color=COLORS["red"], family="monospace")
    ax.text(0.715, yy - 0.028, st, fontsize=7.6, color=COLORS["gray"])
    yy -= 0.072

ax.text(0.715, 0.30, "前提：主机 MODE flags\n置 BERR_REPORTING(bit12)\n（ip link ... berr-reporting on）",
        fontsize=8.4, color=COLORS["gray"], va="top", linespacing=1.4)
ax.text(0.715, 0.16, "未开启时不上报错误帧，\n但 GET_STATE 的 berr-counter\n仍可用（bReq=14）",
        fontsize=8.4, color=COLORS["gray"], va="top", linespacing=1.4)

save(fig, 41, "fig-41-6-error-path")
