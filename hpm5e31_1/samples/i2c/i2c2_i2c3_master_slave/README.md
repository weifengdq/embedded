# i2c2_i2c3_master_slave —— 同一 MCU 双 I2C 实例主从实测（I2C2 master ↔ I2C3 slave）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

用**同一颗 MCU 的两个 I2C 实例**做真实主从通信（用户已用杜邦线连接两组引脚）：

- **I2C2 = master**（SCL=PD02 / SDA=PD03），**I2C3 = slave**（SCL=PD07 / SDA=PD06，从机地址 0x50）。
- slave 用 **DMA(HDMA ch6)** 自主收发：地址命中中断里按方向武装 DMA，因此 master 侧可以安全地阻塞调用
  `i2c_master_write()/i2c_master_read()`，两条总线同时"活着"。
- master 写 N 字节 → slave 收到并逐字节校验；slave 预置 N 字节 → master 读回并校验。
- 测 100k / 400k / 1M 三档的误码与吞吐。

## 硬件 / 接线（用户已接好）

| I2C2（master） | 方向 | I2C3（slave） |
|---|---|---|
| PD02 = I2C2_SCL (ALT4) | ↔ | PD07 = I2C3_SCL (ALT4) |
| PD03 = I2C2_SDA (ALT4) | ↔ | PD06 = I2C3_SDA (ALT4) |

- 两侧 PAD 都配 **开漏 OD=1 + 内部上拉 PE/PS=1**，并且 **必须带 IOC LOOP_BACK**（见"易错点 1"）。
- ⚠️ **PD06/PD07 与 tp-timer 的 `samples/qei/qei_gpio_sim` 复用**（那边把 PD06/PD07 当 GPIO 输出模拟编码器）：
  两个例程**不能同时初始化**，各自单独跑没问题。

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh i2c/i2c2_i2c3_master_slave
bash scripts/hwrun.sh i2c/i2c2_i2c3_master_slave --secs 15 --lock-wait 900 \
  --send "i2c_ms_info;i2c_ms_write 16;i2c_ms_read 16;i2c_ms_bench"
```

## 命令

| 命令 | 作用 |
|---|---|
| `i2c_ms_info` | 主从引脚/时钟/地址/DMA/中断计数 |
| `i2c_ms_write [n]` | master 写 n 字节 → slave 收 + 校验 |
| `i2c_ms_read [n]` | slave 预置 n 字节 → master 读 + 校验 |
| `i2c_ms_test [n] [khz]` | 一次写+读，指定长度/速率 |
| `i2c_ms_bench` | 100k/400k/1M 三档误码与吞吐 |
| `i2c_ms_wires` | GPIO 驱动-读回**探测接线**（确认 SCL-SCL / SDA-SDA） |
| `i2c_ms_probe_addr` | 从机地址约定探测（0x50 / 0xA0 / 0x28） |
| `i2c_ms_scl_probe` | 用 GPIO 中断统计 SCL/SDA 边沿数（诊断信号是否到达从机侧） |

## 实测现象（串口输出）

实测日志：`logs/20261030-145813-i2c2_i2c3_master_slave.log`

```text
I2C2<->I2C3 master/slave bench (24 B write+read per mode, 内部上拉):
100k   wr n=24 PASS errors=0 time=2459.84 us bw=78 kbps
100k   rd n=24 PASS errors=0 time=2454.87 us bw=78 kbps
400k   wr n=24 PASS errors=0 time=757.03 us  bw=253 kbps
400k   rd n=24 PASS errors=0 time=749.02 us  bw=256 kbps
1M     wr n=24 PASS errors=0 time=411.80 us  bw=466 kbps
1M     rd n=24 PASS errors=0 time=405.71 us  bw=473 kbps
i2c_ms_write 64 @100k: PASS errors=0 time=6351.07 us bw=80 kbps
```

**全部 0 错误**（内部上拉条件下 1 MHz 也能跑通）。

## 关键点 / 易错点

1. **必须开 IOC LOOP_BACK！**（本仓库实测：不开时 master 一个 SCL 边沿都不产生、slave 永远收不到地址，
   `i2c_ms_write` 全部 NACK/超时；给 PD02/PD03/PD06/PD07 的 `FUNC_CTL` 加上
   `IOC_PAD_FUNC_CTL_LOOP_BACK_MASK` 后立刻 PASS）。原因：I2C 控制器采样 SCL/SDA 时，
   open-drain 引脚的输入通路需要 pad 输出回送才有正确反馈。
2. slave 的 DMA 武装必须放在**地址命中中断**里（主循环里做会被 master 阻塞调用"饿死"）。
3. slave DMA 需要 `i2c_slave_dma_transfer()` 设置 DATACNT + DMAEN；每次事务前重新武装。
4. 1 MHz + 内部上拉实测 0 错误，但余量小；量产建议外部 2.2~4.7 kΩ 上拉（见第 06 章待确认）。
5. PD06/PD07 与 `qei_gpio_sim` 冲突，见上。

## 待确认 / 风险

| 项 | 状态 | 验证方法 |
|---|---|---|
| 1 MHz 长时间误码率 | **已实测（2026-11-02）** | 10000 轮 x (24 B 写 + 24 B 读) = 480000 B，0 错误，8249.30 ms |
| 外部 2.2~4.7 kΩ 上拉下 1 MHz 波形余量 | 已知限制（需示波器/改硬件） | 加外部上拉后跑 i2c_ms_ber 10000 1000 并看 SDA 上升沿；内部上拉已 0 错误 |
| 与 qei_gpio_sim 同时初始化 | 已知冲突 | 两者都改 PD06/PD07，禁止同时跑 |

## R5 新增命令与实测（2026-11-02）

    i2c_ms_addr_only   纯地址事务(START+ADDR+STOP, 无 DATA 相)的中断计数
    i2c_ms_ber [次数] [kHz]  连续误码: 每轮 24 B 写 + 24 B 读, 逐字节双向校验
    i2c_ms_od_probe    开漏输出时 DI 读数的语义探测

    bash scripts/hwrun.sh i2c/i2c2_i2c3_master_slave --secs 45       --send "i2c_ms_info;i2c_ms_addr_only;i2c_ms_od_probe;i2c_ms_ber 10000 1000"

结果（日志 logs/20261102-121157-i2c2_i2c3_master_slave.log）：

| 项 | 结果 |
|---|---|
| 1 MHz 连续 10000 轮 24 B 写+读 | 480000 B，0 错误，8249.30 ms，58186 B/s |
| 纯地址事务(n=0) | 主机 success；从机 ADDRESS_HIT +1、CMPL +1；主机 ADDRESS_HIT +1、CMPL +1（**会产生 CMPL**） |
| 开漏 DI | 对端能读到 0（开漏有效），但输出态下自身 DI 恒为 1 → 自检必须用另一个引脚 |
| 上拉 | 板上无外部上拉，全部实测按内部上拉（PE/PS=1）进行 |
