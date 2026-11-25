# uart_dma —— UART4 回环 + HDMA 收发吞吐

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

用片上 DMAV2（`HPM_HDMA`）做 UART4 的 TX/RX，数据经内部 MCR LOOP 回到自己：

```
内存 --TX DMA--> UART4.THR --(loopback)--> UART4.RBR --RX DMA--> 内存
```

- TX/RX 各自一个 DMA 通道（本样例 2/3），DMAMUX 通道号由 `DMA_SOC_CHN_TO_DMAMUX_CHN()` 换算。
- UART 侧只要 `dma_enable = true`（FCR.DMAE）+ FIFO 触发级。
- 测不同波特率下的单次传输耗时、KB/s、误码数。

## 硬件

UART4 TX=PC16 / RX=PC17（内部回环，无需接线），控制台 UART0 921600。

## 编译与运行

```bash
bash scripts/build.sh uart/uart_dma
bash scripts/hwrun.sh uart/uart_dma --secs 8 --send "udma_info;udma_test 1024 921600;udma_bench"
```

## 命令

| 命令 | 作用 |
|---|---|
| `udma_info` | 通道/DMAMUX 请求号/配置 |
| `udma_test [bytes] [baud]` | 一次 DMA 回环传输 + 校验 + 吞吐 |
| `udma_bench` | 115200~3 Mbps 吞吐表 |

## 实测现象

实测（`logs/20261030-112250-uart_dma.log`）：

```text
udma_test n=1024 baud=921600 rc=0 errors=0 time=11266 us throughput=90 KB/s
udma_bench: 115200->11 KB/s, 460800->45, 921600->90, 2000000->199, 3000000->307 (全部 0 错误)
```

## 关键点 / 易错点

1. **先启动 RX DMA，再启动 TX DMA**：反过来前几个字节会丢（本样例按此顺序）。
2. 缓冲区必须 `ATTR_PLACE_AT_NONCACHEABLE`（或做 cache 维护），否则 D-Cache 会读到旧数据 / 写到未刷新的内存。
3. UART 的 DMA 请求来自 FIFO 触发级：`rx_fifo_level` 越高，DMA 批量越大但延迟越大。
4. DMA 只负责搬字节，**协议/校验要自己写**；本样例用递增图案逐字节比对。
5. `dma_check_transfer_status()` 是 **W1C**：读一次就把 TC 标志清掉，不能靠重复读同一个通道判断。
