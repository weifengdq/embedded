# i2c_scan —— I2C1(PD12/PD13) 地址扫描 / SCL 时序实测 / IOC 回环对比

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

板上**没有 I2C 从器件**，所以本样例的重点不是"读 EEPROM"，而是把 I2C 主机控制器本身测清楚：

1. **地址扫描**：0x08~0x77 逐个 probe（`i2c_master_write(..., size=0)`），统计 ACK/NACK/超时。
2. **SCL 速率实测**：用一次"只发地址"的 probe 事务耗时（mcycle 计时）反推实际 SCL 频率，
   与 `i2c_mode_normal/fast/fast_plus`（100k/400k/1M）标称值对比，给出 ppm 误差。
3. **总线电平**：`i2c_get_line_scl_status/sda_status` + CMPL/BUSBUSY。
4. **IOC LOOP_BACK 对比**：`i2c_loopback 0|1` 切换 PC08/PC09 的 PAD 回送位，观察总线行为差异。
5. **总线恢复**：`i2c_reset` 用 `i2c_gen_reset_signal()` 发 9 个 SCL 时钟。

## 硬件 / 接线

| 信号 | 引脚 | 说明 |
|---|---|---|
| I2C1 SCL | PD13 | 内部上拉 + 开漏 |
| I2C1 SDA | PD12 | 内部上拉 + 开漏 |
| 外部从器件 | 无 | 要看到 ACK 必须外接（如 AT24C128 @0x50） |

> **为什么不用 I2C0**：I2C0 的 SCL=PC08 同时复用为 MCAN2_TXD（见 `init_mcan2_pins()`），
> 本板 4 路 CAN 全用，所以 I2C0 与 CAN2 不能同时工作。本样例改用 **I2C1(PD13/PD12)**，
> PD12/PD13 在本板上未被其它外设占用（原 ECAT 演示脚默认不复用）。
> 备选：I2C2 PB02/PB03、I2C0 PD08/PD09（`board_init_i2c_eeprom_pin()`）。

## 编译与运行

```bash
bash scripts/build.sh i2c/i2c_scan
bash scripts/hwrun.sh i2c/i2c_scan --secs 8 --send "i2c_info;i2c_lines;i2c_scan 8 119;i2c_timing"
```

## 命令

| 命令 | 作用 |
|---|---|
| `i2c_info` | 引脚/时钟/模式/回环状态 |
| `i2c_scan [from] [to]` | 地址扫描 |
| `i2c_timing` | 100k/400k/1M 的实测 SCL 与误差 |
| `i2c_lines` | SCL/SDA 电平与状态位 |
| `i2c_loopback <0|1>` | 切换 IOC PAD 回送 |
| `i2c_reset` | 9 时钟总线恢复 |
| `i2c_read` | 尝试读 4 字节（演示无从器件时的错误路径） |

## 实测现象

实测（`logs/20261030-113737-i2c_scan.log`）：

```text
scan 0x08..0x77: 112 addr, found=0 nack=112 other=0, avg_probe=751122 ns, total=84146 us
i2c_lines: SCL=1 SDA=1 (1=released/high), CMPL=0, BUSBUSY=0
100k(normal)  SETUP=0x0C901885 T_SP=4 T_SCLHI=392 T_SCLRATIO=1 -> 公式(9)(10) 算得 SCL=100.0 kHz
400k(fast)    SETUP=0x009023D5 T_SP=4 T_SCLHI=61  T_SCLRATIO=2 -> 402.0 kHz
1M(fast_plus) SETUP=0x00802155 T_SP=4 T_SCLHI=21  T_SCLRATIO=2 -> 1013 kHz
```

注：板上无 I2C 从器件，**全部 NACK 是正确现象**；probe 751 µs 主要由驱动的 NACK 重试（5000 次）构成，
不能用来反推 SCL。

## 关键点 / 易错点

1. **全 NACK 是预期**：没有从器件拉低 SDA，ACK 位就是 1（NACK），`i2c_master_write` 返回 `status_i2c_no_addr_hit`。
2. `i2c_master_write(ptr, addr, buf, 0)` 是官方推荐的"纯 probe"，不写数据。
3. **I2C0 的 PC08 与 CAN2_TXD 冲突**（同一 PAD 的两个 ALT），这是本板最容易用错的复用点；本样例因此改用 I2C1。
4. IOC 的 `LOOP_BACK` 只把**本 PAD 的输出回送到本 PAD 的输入**，不是把 SCL 和 SDA 互连；
   它不能代替从器件，只能用于时钟反馈类调试。
5. 上拉电阻：本板用内部上拉（`PE=1,PS=1`），总线电容大或线长时会明显吃掉上升沿，400k/1M 需要外部 2.2k~4.7k 上拉（待确认，需实测波形）。
