# sdm_probe —— SDM（ΣΔ 接收单元）初始化与状态自检

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

初始化 SDM0 通道 0（Sinc3、OSR = 256、FIFO 门限 0），打印时钟、配置、状态位与 FIFO，
用于在**没有外部 ΣΔ 调制器**时自检配置通路。

## 重要结论（本示例存在的意义）

HPM5E00 的 SDM 是 **ΣΔ 信号接收单元**（UM §43.2, p.723）：

- `SDM0_DAT_x` 是**数据输入**（接收外部调制器的 1 bit 位流）；
- `SDM0_CLK_x` 是给调制器的时钟；
- 驱动与寄存器里**没有"发送位流"的接口**。

因此 **"用 SDM 输出位流驱动 LED 做亮度"在本芯片上做不到**（这是项目初期的误判）。
要做 LED 调光请用 PWM（第 14 章）；要在 PLB 里做 ΣΔ 调制器也可以，但那是 PLB 的活。

## 硬件要求 / 接线

- 串口同上。
- 引脚：`SDM0_DAT_0 = PF16`（本板 CAN 状态灯之一，输入方向）、`SDM0_CLK_0 = PF17`。
- 真实数据需要外部 ΣΔ 调制器（如 AD7401/AMC1306），本板未焊。

## 编译与运行

```bash
bash scripts/build.sh sdm/sdm_probe
bash scripts/hwrun.sh sdm/sdm_probe --secs 8 --send "sinfo;sdata"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `sinfo` | 打印 SDM 时钟、滤波配置、状态位 |
| `sdata` | 读 FIFO 计数与首个数据（无调制器时应为 0/空） |

## 实测现象（串口输出，实测日志：`logs/20261030-115504-sdm_probe.log`）

```text
=== sdm_probe (board hpm5e31_1) ===
SDM0 clk=200000000 Hz  ch0: Sinc3 OSR=256 (数据率 = 调制器时钟/256)
  引脚: SDM0_DAT_0=PF16(输入) SDM0_CLK_0=PF17
  本板无外部 ΣΔ 调制器 -> MDAT 无有效位流, 读回数据无意义
  状态: data_ready=0  data_error=0  filter_status=0x00000000

letter:/$ sdata
FIFO count=0
  FIFO 空: 没有 MDAT 位流(正常, 本板无调制器)
```

**实测结论**：SDM0 时钟 = 200 MHz，寄存器配置（Sinc3/OSR256）成功，
`data_ready=0`、FIFO 空——与"本板无外部 ΣΔ 调制器"的预期一致，
证明**初始化通路正确**，但要验证数据精度必须外接调制器（第 17 章 §12 待确认 17-A）。

## 关键点 / 易错点

1. `clock_sdm0` **不在** `board_init_clock()` 默认加入的 clock group 里，
   必须自己 `clock_add_to_group(clock_sdm0, 0)`，否则 `clock_get_frequency` 返回 0。
2. 无调制器时 FIFO 永远空，是正常现象，不是配置错误。
3. 数据率 = 调制器采样率 / OSR（UM §43.3.2.1, p.726）。