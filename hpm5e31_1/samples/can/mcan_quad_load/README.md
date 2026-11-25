# mcan_quad_load —— 4 路 MCAN 并发满载 / 5 Mbit/s 数据段 / TSU 时间戳

四路 CAN FD 两两成组（CAN0<->CAN1、CAN2<->CAN3）同时打流，并附带 TSU/PTPC 时间戳与位时序矩阵实验。
用于《HPM5E31 实战手册》第 08/09 章的"4 路同时满载、5M 数据段、TSU/PTPC、采样点矩阵"实测。

## 硬件前提

- 板载 TCAN1044AV 使能（PE03=0），CAN0/CAN1 与 CAN2/CAN3 两两接在同一段总线，两端各 120 Ω；
- 数据段 5 Mbit/s 是 TCAN1044AV 的规格上限（TI TCAN1044A-Q1 数据手册）；
  8 Mbit/s 属于超规格使用，只能用于短总线实验，不能作为量产配置。

## 命令

| 命令 | 说明 |
|---|---|
| `info` | 打印 CPU/AHB/消息 RAM 配置与 6 组速率用例 |
| `quad [n] [case] [links]` | 并发满载。links=1 单链路 CAN0->CAN1；links=2 一对总线两端都发（CAN0<->CAN1）；links=4 两组总线两端都发（CAN0<->CAN1 + CAN2<->CAN3，4 个 MCAN 全忙） |
| `bi [n] [case]` | 等价于 `quad n case 4`，两组总线双向 |
| `one [n] [case]` | 单链路单向，与第 09 章表 9-5 同口径 |
| `tsu [n]` | TSU 内部 AHB 时基（32 位）：比较同一节点相邻帧的 TX Event / RX 间隔 |
| `ptp [n]` | PTPC0 公共时基（64 位）：直接统计 RX 时间戳 - TX 时间戳 |
| `matrix [n]` | 仲裁 500k/1M x 数据 2M/5M x 3 档采样点 x SJW 1/2/4 x TDC 开/关，共 48 组 |
| `dma` | MCAN DMA 评估：驱动现状、实测轮询成本、DMA 搬运方案与限制 |

case 索引：0=FD 1M/2M 64B，1=FD 1M/5M 64B，2=FD 1M/8M 64B，3=FD 500k/2M 64B，
4=经典 500k 8B，5=经典 1M 8B。

## 统计口径

- **frame/s**：接收端校验通过的帧数 / 墙上时间（CPU 周期计数器，480 MHz）；
- **CPU 忙闲比**：主循环在"没活干"时执行固定空转块，先用 10 ms 空转校准每块周期数，
  再由空转次数反推空闲时间；忙闲比 = 1 - 空闲/总时间，是"主循环还有多少余量"的直接度量；
- **cyc/frame**：忙时间 / 收帧数，轮询方案的软件成本；
- **丢帧**：目标帧数 - 校验通过帧数；超时 5 s 判失败。

## 注意事项（实测遇到过的）

1. **`tx_full` 大不等于丢帧**：满载时非阻塞提交会频繁返回 TX FIFO 满（实测几十万次），
   这是总线饱和的背压；判丢帧看 `bad` 与 `rxfifo_lost`。
2. **TSU 的 prescaler 只能取 1~256**：驱动 `mcan_set_tsu()` 会校验，传 0 直接返回
   `status_invalid_argument`（打印 `st=2`）。取 1 时 TSCFG.TBPRE=0，TSU 计数时钟按 AHB/1。
3. **板卡是共享的**：测试前确认 `scripts/hwlock.sh` 没被别人占用；串口被并行测试打开时
   `serial_cap.py` 会报 multiple access，采集为空。
4. **J-Link 只能由 `hwrun.sh`/`flash.sh` 调**：手工 JLinkExe 会和 hwrun 抢探针，
   报 "Cannot connect to the probe/programmer"。

## 实测结果（2026-11-02 12:28，`logs/20261102-122843-mcan_quad_load.log`）

| 拓扑 | 数据段 | 每方向帧率 | 聚合帧率 | CPU 忙闲比 | cyc/frame | 丢帧 |
|---|---|---|---|---|---|---|
| 单链路单向 | 2 Mbit/s | 2843 fps | — | — | 168859 | 0 |
| 一对总线双向（links=2） | 2 Mbit/s | 1425 fps | 2850 fps | 68.5% | 115377 | 0 |
| 两组总线双向（links=4） | 2 Mbit/s | 1425 fps | 5700 fps | 81.3% | 68492 | 0 |
| 单链路单向 | 5 Mbit/s | 6302 fps | — | — | 76172 | 0 |
| 一对总线双向（links=2） | 5 Mbit/s | 3169 fps | 6338 fps | 69.3% | 52506 | 0 |
| 两组总线双向（links=4） | 5 Mbit/s | 3166 fps | 12664 fps | 82.0% | 31117 | 0 |

结论：4 路全忙时每方向帧率与"一对总线双向"相同（差 <0.1%），说明消息 RAM（4 x 2560 B）
与 AHB 带宽不是瓶颈；瓶颈始终在 CAN 总线本身。TSU/PTPC 的配置已验证到 `mcan_init`，
修掉 prescaler=0 后尚未复跑精度。

## 上板

```bash
bash scripts/build.sh mcan_quad_load
bash scripts/hwrun.sh mcan_quad_load --no-build --secs 50 --send "quad 2000 0 4;bi 2000 1;tsu 200;ptp 200;matrix 200;dma"
```

注意：一次 `--send` 里把要跑的命令都带上，减少 flash 次数；板锁单次建议不超过 90 s。
