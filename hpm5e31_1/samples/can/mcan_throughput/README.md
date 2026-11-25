# mcan_throughput — CAN / CAN FD 单向吞吐与 CPU 周期

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

6 种速率组合（经典 500k/1M 8B；FD 500k+2M、1M+2M、1M+5M、1M+8M，64 B），
每组 CAN0 -> CAN1 单向发 2000 帧，统计 frame/s、payload Mbit/s、每帧平均 CPU 周期与错误计数。

## 硬件要求 / 接线

- 板内 CAN0 <-> CAN1 同一段真实总线（TCAN1044AV，两端 120 Ω）；
- 串口 921600；无需外部分析仪。

## 编译与运行

    bash scripts/hwrun.sh can/mcan_throughput --secs 15 --send "info;thr 2000"

## 实测现象（串口输出）

完整日志 `logs/20261030-114225-mcan_throughput.log`：

| 用例 | frame/s | payload Mbit/s | CPU 周期/帧 |
|---|---|---|---|
| can20-500k-8B | 4313 | 0.28 | 111 290 |
| can20-1M-8B | 8615 | 0.55 | 55 714 |
| fd-500k-2M-64B | 2632 | 1.35 | 182 348 |
| fd-1M-2M-64B | 2843 | 1.46 | 168 859 |
| fd-1M-5M-64B | 6302 | 3.23 | 76 172 |
| fd-1M-8M-64B | 9058 | 4.64 | 52 989 |

全部用例 0 丢帧、TEC/REC=0、LEC=0。

## 命令

| 命令 | 作用 |
|---|---|
| `info` | 列出 6 个用例 |
| `thr [n]` | 跑全部用例，每例 n 帧（默认 2000） |
| `one <idx> [n]` | 只跑第 idx 个用例 |

## 关键点 / 易错点

1. **批次深度受 TX FIFO 限制**：FD 默认 TX FIFO 只有 4 个 64 B 元素，本样例用批次 3；经典用 6（16 元素）。
   `mcan_transmit_via_txfifo_nonblocking()` 返回满时要立即重试。
2. **"CPU 周期/帧"是轮询忙等值**，不是硬件开销；上中断/DMA 可大幅降低（第 09 章 10 节）。
3. 统计区间内不打印，避免串口阻塞污染数据。
4. 吞吐不是芯片上限：payload 受 64 B 帧里 22 bit 仲裁段与 21 bit CRC 限制，提高数据段速率收益递减。
