# uart_irq_ring —— UART4 回环 + RX 中断 + FIFO + 环形缓冲

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

不需要外部接线（UART4 内部 MCR LOOP 自环），演示嵌入式串口最常用的接收架构：

```
TX --(loopback)--> RX FIFO(32B) --RX 中断--> 环形缓冲(256B) --> 主循环校验
```

- RX FIFO 触发级设为 `uart_rx_fifo_trg_not_empty`（>=1 字节就中断）。
- ISR 循环把 FIFO 读空，写入 256 字节环形缓冲；缓冲满时计数丢包。
- 主循环边发边取，逐字节校验图案。
- 统计 **ISR 次数 / 收到字节 / 环形缓冲丢弃 / FIFO overrun**。

## 硬件

| 信号 | 引脚 | 说明 |
|---|---|---|
| UART4 TX | PC16 | 内部回环，无需接线 |
| UART4 RX | PC17 | 同左 |
| 控制台 | PA00/PA01 | 921600 8N1 |

## 编译与运行

```bash
bash scripts/build.sh uart/uart_irq_ring
bash scripts/hwrun.sh uart/uart_irq_ring --secs 6 --send "uart_ring_info;uart_ring_burst 128 16;uart_ring_burst 512 32;uart_ring_stats"
```

## 命令

| 命令 | 作用 |
|---|---|
| `uart_ring_info` | 配置与统计 |
| `uart_ring_burst <n> [chunk]` | 突发 n 字节（带图案）+ 中断收取 + 校验 |
| `uart_ring_stats` | 计数器 |
| `uart_ring_dump` | 打印环形缓冲前 32 字节 |

## 实测现象

实测（`logs/20261030-113457-uart_irq_ring.log`）：

```text
burst n=128 pass=16: isr+128 rx+128 drop+0 verified=128 errors=0, 1.42 ms, 89 KB/s
burst n=512 pass=32: isr+512 rx+512 drop+0 verified=512 errors=0, 5.68 ms, 90 KB/s
uart_ring_stats: isr=768 rx_bytes=768 ring_level=0 ring_drop=0 fifo_overrun=0
```

## 关键点 / 坑

1. **中断里不能用 `uart_send_byte()` 回显**（会与主循环竞争 TX，且阻塞时间不可控）；本样例 ISR 只做"搬运"。
2. FIFO 触发级是 **字节数**，不是时间；`uart_intr_rx_data_avail_or_timeout` 还包含 RX timeout 中断，
   所以 ISR 里必须先 `uart_get_irq_id()` 判断来源，否则 timeout 中断会反复进。
3. 环形缓冲用"写指针先加后判空"的经典做法（牺牲 1 字节），避免 head==tail 的二义性。
4. **`uart_check_status(uart_stat_overrun_error)` 读一次 LSR 就清标志**，所以要在读数据后立刻取样，否则永远读到 0。
5. 256B 环形缓冲 + 921600 波特率 ≈ 2.8 ms 就会被填满，主循环里任何阻塞（比如 printf）都可能造成丢字节——
   这也是实际项目里要开 DMA 或加大缓冲的原因。
