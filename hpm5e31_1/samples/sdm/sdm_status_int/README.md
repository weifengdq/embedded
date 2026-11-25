# sdm_status_int —— SDM 状态 / 中断 / 数据就绪自证（无调制器）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

HPM5E00 的 SDM 是 **ΣΔ 接收单元**（`SDAT` 是输入），本板没有焊 ΣΔ 调制器，
因此**不会有数据就绪事件**。本示例把"无调制器条件下能证明的部分"全部数字化：

1. 模块/通道时钟、Sinc3 + OSR 256 配置回读；
2. 数据就绪中断 + 误差中断的使能（`sdm_channel_common_config_t` 两个开关 + 模块 `IE`）；
3. 中断服务函数的进入计数（预期 0）与最后一次 `SDM` 状态寄存器原始值；
4. FIFO 门限/计数、比较器阈值（高 32000 / 低 1000，OSR 32）配置回读；
5. 理论数据率 = SDM 时钟 / OSR，与实测 0 事件对照 —— 说明"缺的是调制器，不是配置"。

## 硬件要求 / 接线

- 串口：`/dev/ttyACM0` 921600。
- 引脚：`SDM0_DAT_0 = PF16`（输入方向）、`SDM0_CLK_0 = PF17`。
- 真实数据需要 AD7401/AMC1306 等外部 ΣΔ 调制器（本板未焊）。

## 编译与运行

```bash
bash scripts/build.sh sdm/sdm_status_int
bash scripts/hwrun.sh sdm/sdm_status_int --secs 10 --lock-wait 900 \
  --send "sstat;sirq;sclr;sstat"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `sstat` | 打印时钟/理论数据率/中断状态/数据就绪/FIFO/比较器/结论 |
| `sirq` | 只打印 ISR 进入次数与最后一次状态 |
| `sclr` | 清 ISR 计数（注意：不能叫 `clear`，会与 letter-shell 内置命令重名） |

## 实测现象（串口输出）

```text
SDM0 clk=200000000 Hz  ch0: Sinc3 OSR=256 -> 理论数据率 = 200000000 / 256 = 781250 SPS
  IRQ: enabled=1  isr_entries=0  last_status=0x00000000
  status: data_ready=0  data_error=0  filter_status=0x00000000  fifo_count=0
  comparator: data=0 (高门限 32000 / 低门限 1000, OSR=32)
  结论: 配置与中断通路就绪; isr_entries=0 说明 MDAT 没有位流(本板无调制器)
```

实测日志：`logs/SDMI_LOG`。

## 关键点 / 易错点

1. **SDM 不能驱动 LED**：`SDAT` 是输入（UM §43.2, p.723），没有任何"发送位流"的 API。
   调光请用 PWM（第 14 章），或在 PLB 里做 ΣΔ 调制。
2. `clock_sdm0` 不在 `board_init_clock()` 默认加的 clock group 里，必须
   `clock_add_to_group(clock_sdm0, 0)`，否则频率读到 0。
3. 命令名不能叫 `clear`：letter-shell 自带 `clear`，链接期会报
   `multiple definition of shellCmdclear`。改用 `sclr`。
4. 无调制器时 FIFO 恒空、ISR 恒 0，是**正常现象**，不是配置错误。
5. 数据率公式：`f_data = f_modulator / OSR`（UM §43.3.2.1, p.726），
   这里用 SDM 模块时钟近似调制器采样率。
