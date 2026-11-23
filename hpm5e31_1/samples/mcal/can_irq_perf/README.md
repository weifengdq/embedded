# can_irq_perf —— 4 路 MCAN 并发吞吐 / CPU 占用 / 延迟分布

两对 120 Ω 互连总线同时工作：CAN0 -> CAN1、CAN2 -> CAN3，4 路全部 STARTED。
测单向流水吞吐、逐帧延迟分布（直方图）、`Can_MainFunction_*` 占用的 CPU cycle，
以及 4 路同时 ping-pong 的往返延迟。

## 硬件要求

| 资源 | 引脚 | 对端 |
|---|---|---|
| CAN0 / CAN1 | PD00/PD01、PD05/PD04 | 同一段总线，120 Ω×2 |
| CAN2 / CAN3 | PC08/PA09、PC15/PC14 | 同一段总线，120 Ω×2 |
| 收发器 STB | PE03 | 4 颗 TCAN1044AV 共用，低电平使能 |

无需外部线。

## 编译 / 运行

```bash
bash scripts/build.sh samples/mcal/can_irq_perf
bash scripts/hwrun.sh mcal/can_irq_perf --secs 35 --send "can run 800;can pp 200;can stat"
```

## 命令

| 命令 | 说明 |
|---|---|
| `can run [n]` | 两对总线同时单向流水 n 帧（默认 1000），输出 fps/CPU/延迟直方图 |
| `can pp [n]` | 4 路同时 ping-pong n 次 |
| `can stat` | 4 路状态/错误计数器/接收计数 |
| `can info` | 打印处理模型与 CPU 频率 |

## 实测现象（日志：`logs/20261102-142208-can_irq_perf.log`）

> 最终交付构建的复验日志：`logs/20261102-143545-can_irq_perf.log`（8364 frame/s、CPU 4.62%、
> 4 路同时 ping-pong 100/100）。

```text
STREAM 4ch (A:CAN0->CAN1, B:CAN2->CAN3) 8B classic 500k:
  sent A=800 B=800  rx_total=1600  elapsed=191248us => 8366 frame/s total
  per-channel: 4183 frame/s (500 kbit/s 8B 理论上限 ~4386 frame/s)
  CPU in Can_MainFunction_*: 4240703 cycles = 8834us of 191248us = 4.62%
  Can_Write CAN_BUSY retries=39545  tx_confirmed=4000
  pair A: n=800 avg=246us max=256us  histogram:
    <300         800 (100.0%)
  pair B: n=800 avg=244us max=255us  histogram:
    <300         800 (100.0%)
PINGPONG 4ch simultaneous: n=200  A ok=200 avg=244us max=277us | B ok=200 avg=243us max=255us  total=98026us
CAN1: rx=2200   CAN3: rx=2200   (CAN0/CAN2 是发送端，rx=0)
```

关键数据：

| 指标 | 数值 |
|---|---|
| 4 路总吞吐 | 8366 frame/s（每通道 4183 frame/s） |
| 单通道达成率 | 4183 / 4386 ≈ **95.4%**（8B 帧 114 bit 含 IFS） |
| 主函数 CPU 占用 | 4.62%（1.9 s 内 4.24 M cycle / 480 MHz） |
| 逐帧延迟 | avg 244~246 µs，max 256 µs，100% 落在 <300 µs 桶 |
| 4 路同时 ping-pong | A 200/200 avg 244 µs，B 200/200 avg 243 µs |

## 关键点

1. 延迟 ≈ 一帧总线时间（8B 标准帧 @500 kbit/s ≈ 228 µs）+ 主函数轮询间隔，
   因此分布很窄（<300 µs 占 100%），这和 Demo 里"中断延迟"是两回事。
2. CPU 4.62% 是 `Can_MainFunction_Read/Write` 内部的 cycle 占比（用 `mcycle` 包围两次调用测得）；
   4 路同时跑时轮询成本线性增长，是轮询模型的主要代价。
3. `CAN_BUSY retries=39545`：每通道 4 个 HTH，发完一帧后要等主函数确认，重试是正常的
   SWS_Can_00213 语义，不是丢帧（rx_total=1600 = 800+800 全收到）。
4. **处理模型**：本构建 `Can_Cfg.h` 里 `CAN_RX_PROCESSING_INTERRUPT=0`、
   `CAN_TX_CONFIRM_IN_INTERRUPT=0`，即 MainFunction 轮询推进。中断模型（同一状态机，
   把推进点搬到 ISR，见 `Can_Isr_Handler`）已实现但实测 ISR 只进入 1~2 次就停，
   本轮交付不走中断模型，限制与复跑命令见第 46 章。
5. 两对总线互不干扰：A/B 的 avg 延迟差 2 µs，说明 4 路 MCAN 独立、无共享瓶颈
   （共享的只有 AHB SRAM 消息 RAM 访问，实测未构成瓶颈）。

## 待确认项

- 中断驱动模型：已知限制（ISR 只进 1~2 次），见第 46 章"待确认项与风险"；
  复跑命令：把 `mcal/Can/cfg/Can_Cfg.h` 的 `CAN_RX_PROCESSING_INTERRUPT`/`CAN_TX_CONFIRM_IN_INTERRUPT`
  置 1，重新编译 `can_classic_echo` 后 `can echo 50`。