# plb_lut_passthrough —— PLB 查找表 + TRGM 双向路由 + GPTMR 验证

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

打通"**片上可编程逻辑**"的完整数据通路，并用 GPTMR 捕获自证频率：

```text
GPTMR0 CH2 输出比较(1 kHz 方波, 500 us 宽)
  -> TRGM: PLB_IN_00
  -> PLB TYPE A 通道0, LUT0 配置为直通(out = in0, 真值表 0xAAAA)
  -> TRGM: GPTMR0_CAPT_3
  -> GPTMR0 CH3 捕获, 主循环打印实测频率
```

## 硬件要求 / 接线

**完全片内，不需要任何飞线**。串口同上。
（之所以选 GPTMR 方波而不是 PWM 的 TRGO，是因为 TRGO 是 5 ns 窄脉冲，跨时钟域会漏采。）

## 编译与运行

```bash
bash scripts/build.sh plb/plb_lut_passthrough
bash scripts/hwrun.sh plb/plb_lut_passthrough --secs 8 --send "lstat"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `lstat` | 打印捕获次数、实测频率、PLB LUT 寄存器值 |
| `linj` | 用 PLB `SW_INJECT` 注入一个脉冲，验证 PLB 输出通路 |

## 实测现象（串口输出）

```text
letter:/$ lstat
[plb] cap_n=0  PLB_OUT00 上升沿 (还没捕到, 检查 TRGM/PLB 路由)
      PLB LUT0=0x0000aaaa  OUT=100000 Hz  GPTMR=100000000 Hz
```

实测日志：`logs/20261030-115640-plb_lut_passthrough.log`

**结论（重要）**：
- ✅ PLB TYPE A LUT **写入生效**（回读 0xAAAA 与配置一致）；
- ❌ PLB 输出 → TRGM → GPTMR 捕获**没有打通**（cap_n=0），即使补上
  `gptmr_channel_enable()`（使能比较输出 CMPEN）仍未解决；
- 该问题已登记为第 17 章 §12 待确认 **17-E**，并给出了 4 步排查方法
  （查 PLB 全局使能位 / 核对 TYPE A 输入位序 / 把 PLB 输出引到 `TRGM_Pnn` 用示波器看 /
  用软件注入宽脉冲代替 1 拍注入）。

## 关键点 / 坑

1. **PLB 的输出必须再经 TRGM 才能到 GPTMR/PWM 等外设**，不会直连。
2. TYPE A 的 LUT 是"4 bit 输入做索引"的 16 位真值表，**in0 是最低位**：
   直通(out=in0) 是 `0xAAAA`，不是直觉的 `0x00FF`。
3. LUT 输出经 1 拍寄存器，做保护逻辑时要把这拍延迟算进去。
4. TYPE B（数据单元/计数器）跑系统时钟，输入边沿不能快于系统时钟一半。