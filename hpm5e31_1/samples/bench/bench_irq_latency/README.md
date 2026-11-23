# bench_irq_latency —— 中断延迟与抖动（GPTMR 自计时 + GPIO 中断）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

测 **"定时器比较匹配事件 → 用户 ISR 第 1 条指令"** 的延迟，1 万次采样给
min/avg/max/p50/p90/p99 + 16 档直方图，并对比 ISR 主体放 XIP flash 与放 ILM(.`fast`)。

**不需要示波器、不需要 GPIO 翻转**：GPTMR 的比较值就是"应该触发的时刻"，
ISR 第一条语句读 GPTMR 自己的计数器，差值就是错过比较点多少个 tick：

```c
uint32_t cnt = tmr->CHANNEL[ch].CNT;        /* 第 1 条指令 */
uint32_t latency_ticks = cnt - c->cmp_reg;  /* 中断延迟 */
...
gptmr_clear_status(tmr, GPTMR_CH_CMP_STAT_MASK(ch, 0));
gptmr_channel_reset_count(tmr, ch);         /* 清零 -> 下个周期 = CMP ticks */
```

另外用 mcycle 记录 ISR 之间的周期数与 ISR 执行时间，做时基交叉校验。

## 接线

GPTMR 部分无接线；GPIO 部分尝试用 PA02 自环回（失败，见"坑"）。

## 编译与运行

```bash
bash scripts/build.sh bench/bench_irq_latency
bash scripts/hwrun.sh bench/bench_irq_latency --secs 10
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `irq` | ISR 主体在 XIP flash 时测 1 万次 |
| `irqilm` | ISR 主体在 ILM（`.fast`）时测 1 万次 |
| `irqnc` | 关闭 I-Cache 后测 2000 次（放大取指代价） |
| `irqgpio` | GPIO 中断自环回尝试（PA02） |
| `irqinfo` | 打印环境（CPU/GPTMR 频率、ISR 地址、ILM 地址） |

## 实测数据（`logs/20261030-114009-bench_irq_latency.log`）

| 配置 | min | avg | max | p99 | sd |
|---|---|---|---|---|---|
| ISR 主体在 XIP flash | 21 tick = **210 ns** | 210 ns | 220 ns | 220 ns | 0.10 tick |
| ISR 主体在 ILM（.fast） | 22 tick = 220 ns | 220 ns | 250 ns | 220 ns | 0.03 tick |

（GPTMR 配置 100MHz，1 tick = 10ns；由周期反推实际约 98.5MHz，绝对误差 ≤1.5%）

串口自证输出：

```
gptmr_hz=100000000 CMP=2000 ticks  isr_addr=0x186 ilm_body=0xa08
IRQ_STAT mode=xip      n=10000 min=21 avg=21.01 max=22 sd=0.10 p50=21 p90=21 p99=22 tick_ns=10.00
IRQ_TIME mode=xip      min_ns=210 avg_ns=210 max_ns=220 p99_ns=220
IRQ_ISR  mode=xip      isr_active_cycles=238.3 isr_active_ns=496
IRQ_STAT mode=ilm      n=10000 min=22 avg=22.00 max=25 sd=0.03 p50=22 p90=22 p99=22 tick_ns=10.00
```

注意 `isr_addr=0x186`（flash 0x80000xxx 的高位被省略打印为偏移？完整地址见 `irqinfo`）与
`ilm_body=0xa08`（ILM 0x0~0x20000 区间内）—— 这是验证".fast 段确实被搬进 ILM"的直接证据。

## 坑与注意事项

1. **ISR 第一条语句就必须读 CNT**，否则测的是"第 N 条指令"的延迟。
2. **CMP 写入值与读回值差 1**：SDK 注释说明"写 cmp，硬件为 cmp-1"，所以延迟要用
   `cnt - 寄存器读回值`，不要用程序里写的 CMP 变量。
3. **周期反推的 GPTMR 频率比 `clock_get_frequency()` 低 1.5%**：
   因为 ISR 内"清状态 + 清零计数器"有时钟域延迟，仍有一部分没扣干净。
   用 GPTMR 做精确计时前必须自校准（或用 mcycle 交叉校验）。
4. **GPIO 中断自环回不成立**：PA02 配成输出后 DI 不跟随 DO（等 200us 也等不到边沿中断），
   `irqgpio` 会打印 `GPIO_LOOPBACK result=FAIL`。要测 GPIO 中断延迟必须外部飞线。
5. **串口抓包有 0.3s 启动延迟**：本样本在自动测量前 `board_delay_ms(2000)`，
   否则第一组数据（mode=xip）会丢 —— 本仓第一版就丢了。
6. **中断延迟的绝对值含 SDK 中断包装**（`.isr_vector` 段在 ILM，但包装函数在 flash），
   本仓测的是"用户 ISR 第一条指令"，不是"PLIC 收到请求"。

## 参考

- GPTMR 驱动：`$HPM_SDK_BASE/drivers/inc/hpm_gptmr_drv.h`（`GPTMR_CH_CMP_STAT_MASK`、
  `gptmr_channel_reset_count`、cmp/reload 语义）
- 中断注册：`$HPM_SDK_BASE/arch/riscv/intc/hpm_interrupt.h`（`SDK_DECLARE_EXT_ISR_M`）
- 链接脚本：`$HPM_SDK_BASE/soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld`（`.vectors ORIGIN(ILM)`）