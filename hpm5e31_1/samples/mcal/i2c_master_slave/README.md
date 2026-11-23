# i2c_master_slave —— AUTOSAR MCAL I2c 例程（I2C2 控制器 ↔ I2C3 目标）

I2C2 作控制器（主机）访问 I2C3（目标，地址 0x50）。目标侧用 I2C3 地址命中中断按方向武装
HDMA 自动收发，控制器侧用阻塞写/读。验证 100 kHz / 400 kHz / 1 MHz 三档速率、同步与异步传输、
连续传输误码、大块读回。

> 2026-11-02 更新：修掉了"400 kHz 读方向每字节被采样成 0x00"的问题。根因是**目标（从机）侧
> fast 时序的 `SETUP.T_SUDAT` 被 SDK 算成 0**（数据建立时间 100 ns，恰为快速模式规范下限、
> 没有余量），驱动 `I2C_Init` 里对目标通道调用 `i2c_target_timing_fixup()` 把 `T_SUDAT` 抬到
> 标准模式 250 ns 下限（80 MHz 下为 12）。修复后三档速率读/写全部 0 误码。详见第 46 章 §46.8.5。

## 硬件要求与接线

| 信号 | I2C2（控制器） | I2C3（目标） | 说明 |
|---|---|---|---|
| SCL | PD02 | PD07 | 杜邦线直连 |
| SDA | PD03 | PD06 | 杜邦线直连；目标地址 0x50（7 bit） |

注意：PD06/PD07 与 qei 例程复用，两个例程不能同时初始化。

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/i2c_master_slave
bash scripts/hwrun.sh mcal/i2c_master_slave --secs 40 \
  --send 'i2c regs;i2c ber 1000;i2c init 400k;i2c regs;i2c ber 1000;i2c init 1m;i2c ber 1000'
```

| 命令 | 说明 |
|---|---|
| `i2c info` | 版本、地址、速度、序列完成计数、运行时错误计数 |
| `i2c init 100k|400k|1m` | 切换 post-build 配置集（三档速率） |
| `i2c xfer [n]` | 控制器写 n 字节 + 读 n 字节，双向逐字节校验 |
| `i2c async [n]` | 异步：AsyncTransmit 后查 PENDING，再由 MainFunction 推进到 OK |
| `i2c ber [count]` | 连续 count 轮（24 B 写 + 24 B 读）统计误码与错误位置直方图 |
| `i2c rdloop [n] [cnt]` | 只读 n 字节 ×cnt 轮，输出失败数/错误字节/位置直方图 |
| `i2c wrloop [n] [cnt]` | 只写 n 字节 ×cnt 轮 |
| `i2c regs` | 回读 I2C2/I2C3 的 SETUP/TPM/CTRL/STATUS/ADDR（定位时序问题用） |
| `i2c det` | 8 条非法参数调用，验证 Det 计数 |

## 实测数据（2026-11-02 修复后，烧写运行）

日志：`logs/20261102-155949-i2c_master_slave.log`（1000 轮 ×3 档）、
`logs/20261102-161116-i2c_master_slave.log`（32 B 三档 ×写/读）。

32 B 单次传输：

```text
  100k  wr   n=32   PASS err=0   t=3238.76 us 线速率=88.9 kbps
  100k  rd   n=32   PASS err=0   t=3233.82 us 线速率=89.0 kbps
  400k  wr   n=32   PASS err=0   t=992.78 us  线速率=290.0 kbps
  400k  rd   n=32   PASS err=0   t=988.77 us  线速率=291.2 kbps
  1m    wr   n=32   PASS err=0   t=538.44 us  线速率=534.8 kbps
  1m    rd   n=32   PASS err=0   t=536.62 us  线速率=536.6 kbps
```

1000 轮连续误码（每轮 24 B 写 + 24 B 读）：

| 速率 | 轮数 | 传输量 | 失败（写/读） | 错误字节 | 总时间 | 有效吞吐 | 单次往返 |
|---|---|---|---|---|---|---|---|
| 100 kHz | 1000 | 48000 B | 0 / 0 | 0 | 4925.39 ms | 9745 B/s | 4.93 ms |
| 400 kHz | 1000 | 48000 B | 0 / 0 | 0 | 1516.18 ms | 31658 B/s | 1.52 ms |
| 1 MHz | 1000 | 48000 B | 0 / 0 | 0 | 829.82 ms | 57843 B/s | 0.83 ms |

256 B 大块读回 ×50：400 kHz 单次 7.62 ms、1 MHz 单次 4.10 ms，0 误码。

> 实测"线速率"低于标称（400 kHz 档约 290 kbit/s、1 MHz 档约 535 kbit/s）：目标侧只有 4 字节
> FIFO + DMA 装填，每字节产生约 8~11 us 的 SCL 延展，是板内双实例互测的固有效率，接真实
> 外部从机时不会这样。

## 关键点

1. 目标模式必须用中断 + DMA：纯软件在 1 MHz 下填 FIFO 会来不及。
2. 一次读写需要两次 SetupEB：`I2C_JOB_TARGET`（目标侧缓冲）+ `I2C_JOB_MASTER_TX/RX`（控制器侧缓冲）。
3. 序列里同样是"目标 Job 先预备、控制器 Job 后发时钟"，与 SPI 片内主从的做法一致。
4. 异步语义：AsyncTransmit 返回 E_OK 时序列结果必须是 PENDING（1），MainFunction 推进后才变 OK（0）。
5. **目标侧时序不能照抄主控**：从机不产生 SCL，`T_SUDAT/T_HDDAT` 只影响它自己的采样/建立窗口；
   fast/fast_plus 的 SDK 计算给 `T_SUDAT=0`（100 ns，无余量），400 kHz 读就会把字节采成 0x00。
   驱动已统一抬到 250 ns 下限，见 `mcal/I2c/src/I2c.c` 的 `i2c_target_timing_fixup()`。

## 复跑与判定

```bash
# 三档各 1000 轮，要求 fail_wr=0 fail_rd=0 err_bytes=0 且位置直方图全 0
bash scripts/hwrun.sh mcal/i2c_master_slave --secs 30 --lock-wait 1800 \
  --send "i2c ber 1000;i2c init 400k;i2c ber 1000;i2c init 1m;i2c ber 1000"
# 大块读回
bash scripts/hwrun.sh mcal/i2c_master_slave --secs 20 --lock-wait 1800 --send "i2c init 400k;i2c rdloop 256 50"
```

## 待确认项

- 10 bit 地址模式未验证（SWS 允许 10 bit，本板无 10 bit 器件）。
- SCL 实际波形/占空比未用示波器测量；本轮用寄存器回读 + RM 公式 (6)~(10) 校验。
