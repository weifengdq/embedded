
#!/usr/bin/env python3
"""图 34-3：DTO 长度对 DAQ 采样率的影响（经典 8 B vs CAN FD 64 B，估算）

模型：每个 ODT 条目 4 字节，DTO 一帧能装 floor(DTO/4) 个条目；
500 kbit/s 经典 8 B 帧约 222 µs、1 Mbit/s 仲裁 + 2 Mbit/s 数据段 64 B 帧约 320 µs（第 29 章实测）。

生成：python3 tools/figs/app34_dto_size.py
产物：docs/figures/34/fig-34-3-dto-size.png
"""
import os
import sys

import matplotlib
matplotlib.use("Agg")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import setup  # noqa: E402

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

setup()

DTO = [8, 16, 32, 64]
# 每帧可装 4 字节条目数（8B DTO 里 CTO/DTO 头算 0，DAQ 的 ODT 条目直接铺满）
ENTRY = [d // 4 for d in DTO]
# 帧时间：经典 8 B 实测 231 µs（第 29 章 CF 间隔）；FD 64 B 实测 320 µs
FRAME_US = [231, 300, 310, 320]
# 100% 总线占用下每秒帧数上限
FPS = [1e6 / t for t in FRAME_US]
SAMPLES = [f * e for f, e in zip(FPS, ENTRY)]

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10.5, 3.9))

x = np.arange(len(DTO))
w = 0.38
ax1.bar(x - w / 2, ENTRY, w, color="#1565c0", label="每帧条目数（4 B/条）")
ax1.bar(x + w / 2, [d / 4 for d in DTO], w, color="#90a4ae", label="理论最大（DTO/4）")
for i, v in enumerate(ENTRY):
    ax1.text(x[i] - w / 2, v + 0.25, "%d" % v, ha="center", fontsize=9)
ax1.set_xticks(x)
ax1.set_xticklabels(["%d B" % d for d in DTO])
ax1.set_xlabel("DTO 长度（字节）")
ax1.set_ylabel("每帧可装条目数")
ax1.set_ylim(0, max(ENTRY) * 1.3)
ax1.grid(True, axis="y", ls=":", alpha=0.5)
ax1.legend(fontsize=8)
ax1.set_title("DTO 越大，一帧装的测量量越多", fontsize=10)

ax2.bar(x, [s / 1000 for s in SAMPLES], 0.55, color="#2e7d32")
for i, v in enumerate(SAMPLES):
    ax2.text(x[i], v / 1000 + 1.5, "%.0f k/s" % (v / 1000), ha="center", fontsize=9)
ax2.set_xticks(x)
ax2.set_xticklabels(["%d B" % d for d in DTO])
ax2.set_xlabel("DTO 长度（字节）")
ax2.set_ylabel("DAQ 采样率上限（千样本/s，100% 占用）")
ax2.set_ylim(0, max(SAMPLES) / 1000 * 1.25)
ax2.grid(True, axis="y", ls=":", alpha=0.5)
ax2.set_title("2 Mbit/s 数据段下 64 B DTO 理论 50 k 样本/s", fontsize=10)

plt.tight_layout()
os.makedirs("docs/figures/34", exist_ok=True)
plt.savefig("docs/figures/34/fig-34-3-dto-size.png", bbox_inches="tight", facecolor="white", dpi=300)
print("ok docs/figures/34/fig-34-3-dto-size.png")
