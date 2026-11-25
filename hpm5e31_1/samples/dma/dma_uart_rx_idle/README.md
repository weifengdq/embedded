# dma_uart_rx_idle —— UART4 + DMA + 空闲(IDLE)中断收帧，测丢包/延迟

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- UART4（PC16 TXD / PC17 RXD）115200 8N1，打开 **RX 空闲检测**（线上高电平超过 10 bit 判定一帧结束）；
- **DMA 发送**（handshake 模式，内存 → UART THR）逐帧发送变长帧（1~64 B）；
- **RX DMA**（handshake，RBR → 内存）逐帧接收，并用 **IDLE 中断**识别帧边界；
- 自测：打开 UART 的 **modem loopback（MCR.LOOP）** 做内部 TX→RX 回环，**不需要外部接线**；
- 统计：帧界识别率、丢帧（ppm）、发送开始到空闲中断的延迟（min/avg/max）、TX DMA 耗时；
- 命令：`uartinfo` / `uarttest [n]`。

## 硬件要求 / 接线

| 项 | 说明 |
|---|---|
| UART | UART4，PC16=TXD，PC17=RXD（板级 APP UART 引脚） |
| 回环 | 代码里 `uart_modem_enable_loopback()` 打开内部回环，实测 **OK**，无需杜邦线 |
| 若内部回环不可用 | 用杜邦线把 PC16 与 PC17 短接，同样可跑（README 末尾有说明） |
| 控制台 | 仍是 UART0（/dev/ttyACM0），本示例不影响 shell |

## 编译与运行

```bash
bash scripts/build.sh dma/dma_uart_rx_idle
bash scripts/hwrun.sh dma/dma_uart_rx_idle --secs 14 --send "uartinfo;uarttest 20"
```

## 命令

| 命令 | 说明 |
|---|---|
| uartinfo | 打印 UART4/波特率/DMA 通道/回环自检结果/IDLE 计数 |
| uarttest [n] | 发送 n 个变长帧并统计帧界、丢包、延迟、TX DMA 耗时 |

## 实测现象（串口输出）

日志：`logs/20261030-123512-dma_uart_rx_idle.log`

```text
UART4 @0xf0050000 baud=115200, TX=PC16 RX=PC17
  RX DMA: HDMA ch0 (DMAMUX ch0), idle threshold=10 bit
  internal loopback: OK
  idle irq total: 1

letter:/$ uarttest 20
uarttest: 20 帧, 长度 1..64 变化, 帧间 1 ms 空闲
  result: frames=20 idle_frame=20 timeout=0
  data: poll_rx_ok=3/20, dma_rx_ok=0/20 (DMA RX 异常见 README 易错点 1)
  loss=0 ppm (按 idle 帧界计), tx->idle latency min=166 us avg=2388 us max=5038 us
  tx_dma: avg=470 us max=2085 us, idle irq total=21
```

| 指标 | 实测值 | 说明 |
|---|---|---|
| 内部 loopback 自检 | **OK** | 发 1 字节轮询收到同样字节 |
| 帧界识别（IDLE 中断） | **20/20，loss=0 ppm** | 每帧结束都产生一次 IDLE 中断 |
| TX DMA 每帧耗时 | avg 470 µs，max 2085 µs | 含通道重建开销，115200 下 1~64 B 理论 87~5560 µs |
| TX 开始→IDLE 延迟 | min 166 µs / avg 2388 µs / max 5038 µs | 与帧长成正比 |
| RX DMA 数据 | **0/20 正确**（异常） | 见易错点 1；同一帧用轮询读对照也只有 3/20，说明 DMA 抢占了 FIFO |

## 关键点 / 易错点

1. **【重要】UART RX DMA 在本板配置下搬到伪数据**：同一帧用轮询读（uart_check_status + RBR）能读到正确字节
   （首帧 poll_match=1），但 RX DMA 目的地内容是随机值（0x16/0x3c/0x40 等），且 LSR 的 DR 位仍置 1，
   说明 DMA 是在 FIFO 没有真实数据时被"提前触发"读了 RBR。
   已尝试：每帧前清空 FIFO/清 IDLE 标志、改用单次大 DMA、改用 HDMA/不同突发——均未解决。
   **结论**：本板 UART RX 的 DMAMUX 触发时序需要进一步查（怀疑 RX timeout 触发的 DMA 请求），
   作为"待确认"记录（见第 11 节），生产代码暂用"轮询/中断收 + DMA 发"。
2. **IDLE 检测很可靠**：20 帧全部识别，无超时；阈值 10 bit 时，帧间只要 >1 ms 空闲就能分开。
3. **TX DMA 需要每次重建通道**（`dma_setup_handshake()`），每帧 ~470 µs 开销；连续大数据建议
   一次配置发多帧（用 linked descriptor 或一次 size_in_byte 覆盖多帧）。
4. UART4 的 TX/RX 引脚来自 `board_init_uart(BOARD_APP_UART_BASE)`，不要重复改 pinmux。
5. 内部回环（MCR.LOOP）只把 TX 数据回送给 RX，**不经过外部引脚**；如果要验证真实 RS-232 电平/外部设备，
   仍需外部短接或接对端。

## 待确认项

| 项 | 状态 | 验证方法 |
|---|---|---|
| UART RX DMA 伪数据根因 | 待确认（已实测异常） | 用调试器看 DMAMUX `MUXCFG` 与 UART `FCRR.TMOUT_RXDMA_DIS`；改用 SDK 的 `dma_uart_rx_circle_transfer` 对照 |
| 外部 TX-RX 短接时的表现 | 待确认（需接线） | PC16-PC17 短接后重跑 `uarttest 20`，看 dma_rx_ok 是否恢复 |
| 连续大数据吞吐 | 待确认（需实测） | 一次发送 4 KB，测量总时间与 DMA/CPU 占用 |
