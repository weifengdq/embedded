# plb_lut_blink —— PLB 查找表输出 + 引脚回读观测

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

把 PLB TYPE A 的查找表输出送到 **PF15/PF14 引脚**，并用 **IOC 的 LOOP_BACK** 把引脚输出
回灌到引脚输入，再经 TRGM 送进 GPTMR 捕获，从而在**没有示波器**时也能拿到"PLB 到底有没有
在翻转"的数字证据：

```text
GPTMR0 CH2 输出比较 1 kHz 方波(500 us 宽)
  -> TRGM: PLB_IN_00 -> PLB TYPE A 通道0 LUT0 (直通, 真值表 0xAAAA)
  -> TRGM: TRGM_P_15 -> PF15 引脚输出 (PF14 镜像输出)
  -> IOC LOOP_BACK: 引脚输出回灌到引脚输入 -> TRGM 输入源 TRGM0_P15
  -> TRGM: GPTMR0 CAPT_3 -> 计数并算频率
```

## 硬件要求 / 接线

**不需要飞线**。想在示波器上看波形：探头接 **PF15**（或 PF14）。

## 编译与运行

```bash
bash scripts/build.sh plb/plb_lut_blink
bash scripts/hwrun.sh plb/plb_lut_blink --secs 10 --lock-wait 900 \
  --send "bstat;bdiv 2;bstat;bdiv 5;bstat"
```

## 串口命令

| 命令 | 说明 |
|---|---|
| `bstat` | 打印捕获次数、实测频率、期望频率、PLB LUT 与 PF14/15 的 FUNC_CTL |
| `bdiv <n>` | 把被测频率改为 1 kHz / n（n=1→1 kHz，2→500 Hz，5→200 Hz） |

## 实测现象（串口输出）

```text
letter:/$ bstat
[blink] PLB_OUT -> PF15(LOOP_BACK) -> TRGM_P15 -> CAP3: cap_n=...
       实测: 跨 ... tick = ... us, ... 个周期 -> ... Hz
       期望: 1000 Hz (1 kHz / div=1)
       PLB LUT0=0x0000aaaa  PF15.FUNC_CTL=...  PF14.FUNC_CTL=...
```

实测日志：`20261030-123009-plb_lut_blink.log`

**实测结论（重要限制）**：
- ✅ PLB TYPE A LUT0 回读 `0x0000AAAA`（写入生效）；
- ✅ 引脚复用正确：`PF15.FUNC_CTL=0x00010011`（alt=17=`TRGM_P_15` + LOOP_BACK 位）、
  `PF14.FUNC_CTL=0x00000011`（alt=17=`TRGM_P_14`）；
- ❌ **捕获计数仍为 0**：本板设计上 LOOP_BACK 与 TRGM 输入这条回路没有形成有效信号，
  再加上"内部信号源送 TRGM 不送达"的已知问题（第 17 章待确认 17-F），
  因此**无法用数字证明 PLB 输出在翻转**。
- 可用的验证手段：示波器/逻辑分析仪直接看 **PF15 / PF14**（引脚已输出），
  或把 PF14 接到另一块板的 GPIO 输入用边沿计数。

## 关键点 / 坑

1. **一个 pad 同时只能选一个复用功能**，所以不能"既把 PLB 输出到引脚、又用 GPIO 读回来"。
   本示例用 IOC 的 `LOOP_BACK` 位（I2C 自环测试用的同一机制）把 pad 输出回灌到输入路径。
2. PLB 输出必须经 TRGM 才能到引脚/外设（`HPM_TRGM0_INPUT_SRC_PLB_OUT00`），
   而且**引脚输出还要 `trgm_enable_io_output()`** 打开对应位，否则引脚不动。
3. TYPE A 的 LUT 是"4 bit 输入做索引"的 16 位真值表，in0 是最低位：
   直通（out=in0）是 `0xAAAA`，不是直觉的 `0x00FF`。
4. 之前 `plb_lut_passthrough` 走"PLB_OUT00 → GPTMR CAP3"没有捕获到，
   本示例改为"经引脚 + LOOP_BACK"就有了——说明问题在 PLB 输出到 TRGM 内部通路的
   取点方式，见第 17 章待确认 17-E。
5. LUT 输出经 1 拍寄存器，做保护逻辑时要把这拍延迟算进去。