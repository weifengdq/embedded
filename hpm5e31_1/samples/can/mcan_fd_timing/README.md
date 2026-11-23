# mcan_fd_timing — CAN FD 位时序矩阵与 TDC 对比

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

在 CAN0 -> CAN1 的真实两节点总线上，遍历"仲裁段 500k/1M x 数据段 2M/4M/5M/8M x 3 个采样点"
共 24 组位时序，每组单向发 300 帧 64 B CAN FD（BRS=1），统计丢帧/错误码/frame/s/payload，
并做 TDC（发送延迟补偿）开/关对比。用于回答"哪些位时序组合在 HPM5E31 上可达、哪个最快"。

## 硬件要求 / 接线

- 板内 CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 经 TCAN1044AV 接同一段总线，两端 120 Ω（板上固定）；
- 串口 /dev/ttyACM0 921600；**不需要**外部 CAN 分析仪。

## 编译与运行

    bash scripts/hwrun.sh can/mcan_fd_timing --secs 20 --send "info;all 300;tdc 300"

## 实测现象（串口输出）

完整日志 `logs/20261030-115604-mcan_fd_timing.log`，关键行：

    MATRIX SUMMARY: cases=24 frames_ok=6600 frames_bad=0
    a1m-d8m-sp80  arb=1000k sp=80.0% dat=8000k sp=80.0% tdc=1 n=300 rx=300 bad=0 t=33116 us fps=9059 payload=4.64Mbps
    -> tdc-on  data=5000k: ok=300 bad=0 us=47589

要点：

| 数据段 | 仲裁 500k fps | 仲裁 1M fps | payload Mbit/s |
|---|---|---|---|
| 2 Mbit/s | 2842 | 2842 | 1.45~1.46 |
| 4 Mbit/s | 4565 | 5239~5240 | 2.34~2.68 |
| 5 Mbit/s | 5352 | 6303 | 2.74~3.23 |
| 8 Mbit/s | 7218~7219 | 9059~9060 | 3.70~4.64 |

- 23 组有效配置**全部 0 丢帧**、TEC/REC=0；
- 1 组（a1m-d2m-sp825，数据段 seg1=32）被驱动以 `status_mcan_invalid_bit_timing = 26014` 拒绝，
  属于硬件字段限制（FD 数据段 seg1 上限 32 含同步段，配置值 ≤31），不是 bug。

## 命令

| 命令 | 作用 |
|---|---|
| `info` | 打印矩阵规模/时钟/接线 |
| `all [n]` | 跑全部 24 组，每组 n 帧（默认 300） |
| `tdc [n]` | TDC 开/关对比（2M/5M/8M） |
| `one <idx> [n]` | 只跑第 idx 组 |

## 关键点 / 坑

1. **硬限制**：FD 数据段 `num_seg1 <= 31`（寄存器 seg1 含同步段，上限 32）、seg2 ≥2、TDC 时 prescaler ≤2；
   所以 2 Mbit/s 的 87.5%、8 Mbit/s 的 87.5%/90% 在 80 MHz CAN 时钟下**不可达**。
2. 2 Mbit/s 用 40 TQ：75/80/87.5% 对应 seg1=29/31/34，其中 34 越界，矩阵用 77.5%（seg1=30）代替；
   8 Mbit/s 用 10 TQ：只剩 60/70/80% 三档（seg2 必须 ≥2）。
3. 每次切换配置用 `mcan_deinit() + mcan_init()`，注意先 `board_init_can_clock()` 再 deinit。
4. TDC 的 `ssp_offset` 取"数据段 seg1+1"，`filter_window_length` 同值；`PSR.TDCV` 可读出实测延迟。
