#!/usr/bin/env python3
"""R7: 全书地图与阅读路径（第 0 章前言用）。"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import setup, save, COLORS
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

setup(10)

PARTS = [
    ("第一部分\n基础与工具", "00 前言 · 01 芯片与板卡 · 02 环境/构建/烧写 · 03 letter-shell", COLORS["blue"]),
    ("第二部分\n外设驱动与实测",
     "04~07 GPIO/UART/I2C/SPI · 08~09 CAN FD · 10~13 10BASE-T1S/千兆/lwIP\n"
     "14~17 PWM/GPTMR/QEI/PLB · 18~22 ADC/DMA/USB/XPI/低功耗", COLORS["green"]),
    ("第三部分\n系统与性能", "23 性能基准 · 24 总线吞吐 · 25 长稳 · 26 勘误 · 27 选型对比", COLORS["orange"]),
    ("第四部分\n应用层与实战",
     "29 测试台 · 30~31 ISO-TP/UDS · 32 DoIP · 33~35 J1939/CANopen/XCP(XCP on UDP)\n"
     "36~38 SOME/IP/XRCE/MQTT · 39 PTP · 40 DLT/VSS · 41 选型 · 42 gs_usb", COLORS["purple"]),
    ("第五部分\nAUTOSAR MCAL",
     "43 总览 · 44 基础设施 · 45 Mcu/Port/Dio · 46 Gpt/Pwm/Icu/Wdg · 47 Adc/Spi/I2c\n"
     "48 Can · 49 Mem/Crc · 50 Eth/EthTrcv+lwIP/FreeRTOS · 51 合规性与商用对比", COLORS["teal"]),
    ("第六部分\n附录", "52 引脚表 · 接线与测量方法 · 日志索引 · 命令速查", COLORS["gray"]),
]

fig, ax = plt.subplots(figsize=(7.9, 7.3))
ax.set_xlim(0, 10)
ax.set_ylim(0, len(PARTS) * 1.28 + 1.15)
ax.axis("off")

for i, (title, body, color) in enumerate(PARTS):
    y = (len(PARTS) - 1 - i) * 1.28 + 0.15
    box = FancyBboxPatch((0.1, y), 9.8, 1.06, boxstyle="round,pad=0.02,rounding_size=0.08",
                         linewidth=1.2, edgecolor=color, facecolor=color + "14")
    ax.add_patch(box)
    ax.text(0.32, y + 0.53, title, fontsize=10.2, fontweight="bold", color=color, va="center", ha="left")
    ax.text(3.30, y + 0.53, body, fontsize=7.2, va="center", ha="left", color="#24292F")

top = len(PARTS) * 1.28 + 0.55
ax.text(0.1, top, "本书结构与四条阅读路径", fontsize=13, fontweight="bold", color="#24292F")
ax.text(0.1, top - 0.42,
        "入门 00→01→02→03 ｜ 移植 按外设跳读 04~22 ｜ 查错 26 + 各章易错点清单 + 51 附录 ｜ 选型 28→42→43（MCAL 从 42 进）",
        fontsize=7.8, color="#57606A")

save(fig, 0, "fig-00-1-book-map")