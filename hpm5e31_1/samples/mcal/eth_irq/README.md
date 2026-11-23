# eth_irq —— AUTOSAR Eth 接收中断模式（ISR 次数 / 延迟 / CPU 占用 / 丢包）

## 1. 这是什么

验证 Eth 驱动的**接收中断模式**并与轮询模式做同口径对照：ISR 进入次数、ISR→主循环消费延迟、
ISR/帧比、主循环 RX 取帧路径 CPU 占比、丢弃计数。

PBcfg 里默认 `RxInterruptEnable = FALSE`（轮询）；本样例用一份"运行期配置副本"重新 `Eth_Init` 打开 RX 中断，等价于改 PBcfg 后重编译，但不用维护两套配置。

两种模式的取帧策略（同一份统计代码，口径可比）：

| 模式 | 取帧触发 | 说明 |
|---|---|---|
| 轮询 | 主循环每轮调一次 `Eth_GetRxBuffer` | 这是"轮询"的定义，也是它烧 CPU 的原因 |
| 中断 | `Eth_GetRxIsrCount` 增量变化后排空 DMA 环 | 真正的 ISR 驱动；另有 1 ms 兜底轮询，用于证明 ISR 没漏帧 |

## 2. 硬件与接线

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1，ENET0 RGMII + YT8011AN 1000BASE-T1 |
| 对端 | 千兆车载以太网转换盒 → 主机 enp6s0 |
| 主机要求 | root（AF_PACKET 灌包） |
| 中断号 | IRQn_ENET0 = 58，PLIC 优先级 4 |

## 3. 编译与运行

```bash
bash scripts/build.sh samples/mcal/eth_irq
```

单条命令快速切模式（窗口 4 s，不开主机灌包时 ISR 计数仍有效）：

```bash
bash scripts/hwrun.sh mcal/eth_irq --secs 16 --lock-wait 1800 --send "irq off;irq rx 4;irq on;irq rx 4"
```

完整矩阵（空闲 / 1000 fps / 20 000 fps / 全速灌包 × 中断/轮询，一次持锁约 55 s，含烧写与 banner 自证）：

```bash
bash samples/mcal/eth_irq/host/run_irq_matrix.sh --lock-wait 1800
```

脚本会：抢整机锁 → 烧写（失败/校验失败自动重试 3 次）→ banner 自证是本例程 → 逐场景起主机
`eth_host.py flood`、分步发命令、抓串口 → 合并"板端 + 主机"日志到 `logs/<时间戳>-eth_irq-host.log`。
分步发命令是必须的：`irq rx 4` 会阻塞 shell 4 s，一次性把多条命令塞进串口 FIFO 有丢失风险。

## 4. 命令表

| 命令 | 作用 |
|---|---|
| `irq info` | 当前模式、ISR 次数、Det 计数、Rx 状态、丢弃/错误计数 |
| `irq on` / `irq off` | 切中断 / 切轮询（内部重新 `Eth_Init`） |
| `irq rx <secs>` | 统计窗口（1~8 s）：帧数、吞吐、ISR、延迟、CPU、丢弃 |
| `irq burst <len> <n>` | 板端发 n 帧（配合主机 listen） |

输出两行（旧行保留给历史日志，新行给全指标）：

| 行 | 字段 |
|---|---|
| `IRQ mode=... frames=... isr=... isr_per_frame=... lat_avg=...us lat_max=...us` | R7 旧格式（兼容） |
| `IRQX mode=... fps=... isr_pf_x1000=... lat_avg_us=... lb_*... wake_*... polls=... poll_ns_avg=... rx_cpu_x100=... drop/err/crc/ovf` | 定点数（x100/x1000）；延迟分桶、兜底帧数、CPU 占比、计数器增量 |

## 5. 实测数据（R8）

完整表格与图见书稿第 49 章 §49.7.5：表 49-31（主对照）、表 49-33（延迟）、表 49-34（重复性）、图 49-10 / 图 49-11。
主日志 `logs/20261102-184310-eth_irq-host.log`（重复性 4 份：`-183509/-183711/-183827/-183934-`）。
一句话结论：空闲时主循环为"找帧"烧 8.06 % CPU，中断模式 0.03 %；1 000 帧/s 时 8.28 % → 0.32 %；
全速灌包下两种模式吞吐都在 155 k 帧/s 量级（中断无优势，且每帧一次中断）。

出图：

```bash
python3 tools/figs/mcal_eth_irq.py --log logs/20261102-184310-eth_irq-host.log
```

## 6. 关键点与坑

1. **ISR 里不要搬数据**：ISR 只计数、记 mcycle、清 W1C 状态位，数据由主循环 `Eth_GetRxBuffer` 取。
2. **从中断模式切回轮询必须先停 DMA、关 PLIC**：`Eth_Init` 会 `memset` 掉驱动状态（`Base=NULL`），
   若 PLIC 仍开着，PHY 复位 30 ms 内来一帧就会进 ISR；ISR 见 `Base==NULL` 直接返回不 W1C → 中断风暴、
   主循环饿死、串口无输出。见 `reinit()` 里的 `Eth_SetControllerMode(DOWN)` + `Eth_BoardIrqEnable(FALSE)` 安全序。
3. **测试帧 magic 判在整帧偏移 14 处**：`Eth_GetRxBuffer` 返回的是整帧（含 14 B 以太网头），
   广播帧 `data[0..1]` 是 `FF FF`；R7 版本判 `data[0]` 导致"ISR 有计数、frames 恒 0"。
4. **窗口计时用 `bsp_uptime_ms()`**：R7 版本写成 `secs * cycles_per_us * 1000`，实际只有毫秒级。
5. **延迟口径**：`Eth_GetLastIsrCycle` 是"最近一次 ISR"，一次中断覆盖多帧时不能当逐帧到达时间戳；
   例程只统计正且 <10 ms 的样本并输出分桶，另有"ISR→主循环发现"延迟更稳。
6. **全速灌包下驱动计数全 0 不代表没丢**：板端实收约为主机发送的 91 %~92 %，MAC/DMA 侧丢弃本驱动不计数（已知限制）。

日志命名规则：`logs/<时间戳>-eth_irq-host.log`（矩阵，含主机侧）或 `logs/<时间戳>-eth_irq.log`（hwrun）。
