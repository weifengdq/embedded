# uart_loopback —— UART4 内部回环：波特率误差 / 数据完整性 / FIFO / 吞吐

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

不需要任何外部接线：把 UART 的 **MCR.LOOP** 位置 1，控制器内部把 TX 直接送到 RX。
在这个前提下测：

1. **波特率精度**：用 mcycle 计一次 TX 突发（waiting TEMT）的耗时，反算实际波特率，
   并与回读 OSCR/DLL/DLM 分频算出的"器件波特率"对比，给出 ppm 误差表。
2. **数据完整性**：分块发送递增图案并逐字节回读比对。
3. **FIFO 深度与溢出**：连续发 3 倍 FIFO 深度的数据而不读，观察实际收到多少、overrun 标志。
4. **回环吞吐**：测 KB/s 与理论值（baud/10）的比值。

## 硬件 / 接线

| 信号 | 引脚 | 说明 |
|---|---|---|
| UART4 TX | PC16 | 与 hpm5e00evk 的 `init_uart4_pins()` 一致 |
| UART4 RX | PC17 | 内部回环时外部引脚保持空闲高 |
| 控制台 | PA00/PA01 UART0 | 921600 8N1，不受影响 |

> PC16 同时可复用为 `XPI0_CA_DQS`、`PPI0_DQ_16`；本板启动 Flash 走 XPI0 的 CA/CB 组之外的引脚，
> 实测 UART4 复用不影响 XIP 运行（见日志）。

## 编译与运行

```bash
bash scripts/build.sh uart/uart_loopback
bash scripts/hwrun.sh uart/uart_loopback --secs 10 --send "uart_info;uart_selftest 1024 16;uart_thru 4096 921600;uart_fifo;uart_err"
```

## 命令

| 命令 | 作用 |
|---|---|
| `uart_info` | 时钟/分频/FIFO/控制台波特率回读 |
| `uart_err` | 9600~5 Mbps 误差表（request/reg/measured/ppm） |
| `uart_selftest [bytes] [chunk]` | 回环收发比对 |
| `uart_thru [bytes] [baud]` | 回环吞吐（KB/s，含效率） |
| `uart_fifo` | FIFO 深度/溢出演示 |
| `uart_baud <rate>` | 改波特率并验证 |

## 实测现象

实测（`logs/20261030-111055-uart_loopback.log`，UART 源时钟 80 MHz）：

```text
  src_clk=80000000 Hz, requested=921600, OSCR=8, DIV=11 -> reg_baud=909090
selftest baud=921600 bytes=1024 chunk=16 errors=0 time=11.30 ms throughput=90 KB/s
thru baud=921600 bytes=4096 errors=0 measured=90 KB/s theoretical=92 KB/s efficiency=97%
fifo: sent=96 without reading, fifo_size=32, received=33
```

误差表（request / reg_baud / measured）：9600/9596/9596、115200/114942/114940、
460800/454545/454541、921600/909090/909081（-13574 ppm）、3000000/3076923/3076820（+25641 ppm）。

## 关键点 / 易错点

1. **uart_send_byte() 有重试上限**（`HPM_UART_DRV_RETRY_COUNT`），低波特率长时间等待会返回 `status_timeout`；
   本样例改用"轮询 LSR.THRE"的本地阻塞写。
2. **THRE ≠ 最后一个字节已发完**：THRE 只表示发送保持寄存器/FIFO 空，测波特率必须再等 **TEMT**，
   否则 1 个字节（10 bit）的系统误差在小 N 时会放大到几千 ppm。
3. 内部回环时外部 TX 引脚不再输出数据（16550 语义），想量真实波形必须关掉 LOOP 并把 TX/RX 外部短接。
4. 高波特率（>3 Mbps）是否可用取决于 UART 源时钟：需要 `src/(osc*div)` 能落在 ±3% 容差内，
   否则 `uart_init()` 返回 `status_uart_no_suitable_baudrate_parameter_found`。
5. RX FIFO 只有 32 字节（`UART_SOC_FIFO_SIZE`），突发超过 FIFO 深度必然 overrun——这就是要上中断/DMA 的原因。
