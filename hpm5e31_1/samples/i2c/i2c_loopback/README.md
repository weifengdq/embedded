# i2c_loopback —— 软件（位翻转）I2C 主机 + 开漏/上拉/线电平自检 + SCL 时序实测

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

板上没有 I2C 从器件，硬件 I2C 主机只能做"地址扫描（全 NACK）"。本样例是 I2C 的**第二个例程**，
用 GPIO 位翻转实现一个完整的软件 I2C 主机，**每一步都靠 DI 读回真实引脚电平自证**：

1. **引脚自检**：SCL/SDA 配置为 **开漏（OD=1）+ 内部上拉（PE=1,PS=1）**；
   驱动低应读到 0，释放应读到 1。这一步验证了"开漏 + 上拉"硬件前提（I2C 必须线与）。
2. **完整 I2C 时序**：START → 8 位地址 + ACK → 数据 → STOP，全部由软件产生并读回 SDA 判定 ACK/NACK。
3. **SCL 频率实测**：用 CPU 周期精确控制半周期，再用 mcycle 实测 10 个 SCL 周期反推实际频率，
   给出 100k / 400k / 1M / 2M 的目标 vs 实测 vs ppm 误差表。
4. **软件地址扫描**：0x08~0x77 全扫，全 NACK 为正常（无从器件）；有从机时打印 **ACK @0xNN**。
5. **总线恢复**：9 个 SCL 时钟 + STOP，用于从机把 SDA 拉死时的解锁（比硬件 I2C 更可控）。

与 `i2c_scan`（硬件 I2C1 + 驱动 API）互补：一个讲寄存器/驱动，一个讲时序/电气层。

## 硬件 / 接线

| 信号 | 引脚 | 说明 |
|---|---|---|
| SCL | PB02 | GPIO 开漏 + 内部上拉 |
| SDA | PB03 | GPIO 开漏 + 内部上拉 |
| 外部从器件 | 无 | 接 AT24Cxx 等即可在扫描中看到 ACK |

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh i2c/i2c_loopback
bash scripts/hwrun.sh i2c/i2c_loopback --secs 10 --send "i2c_soft_info;i2c_soft_selftest;i2c_soft_timing;i2c_soft_scan;i2c_soft_recover"
```

## 命令

| 命令 | 作用 |
|---|---|
| `i2c_soft_info` | 引脚/时钟/半周期/目标 SCL |
| `i2c_soft_lines` | 读 SCL/SDA 电平（1=释放高） |
| `i2c_soft_selftest` | 开漏+上拉自检（驱动 0 读 0 / 释放读 1） |
| `i2c_soft_speed <kHz>` | 设置目标 SCL 并实测 |
| `i2c_soft_scan [from] [to]` | 软件地址扫描 |
| `i2c_soft_timing` | 100k/400k/1M/2M 时序表 |
| `i2c_soft_recover` | 9 时钟总线恢复 |

## 实测现象（串口输出）

实测（logs/20261030-123256-i2c_loopback.log）：

```text
pin self-test A: input + internal pull-down/pull-up
  SCL: pull-down DI=0, pull-up DI=1 OK
  SDA: pull-down DI=0, pull-up DI=1 OK
selftest A: 0 errors -> PASS
pin self-test B: output(open-drain) DI read-back (仅记录)
  SCL: drive0->DI=1, release->DI=1
  SDA: drive0->DI=1, release->DI=1
soft I2C SCL sweep: 100k->103k(+3.0%), 400k->445k(+11.3%), 1M->1541k, 2M->1542k
soft_scan 0x08..0x77: 112 addr, found=0（无从器件，全 NACK 正常）
recover: before SCL=1 SDA=1 -> after 9-clock SCL=1 SDA=1
```

> 关键发现：本 SoC 的 **GPIO 开漏输出低电平时 DI 读回仍为 1**（自检 B）；
> 但引脚设为**输入 + 内部下拉/上拉**时 DI 正确（自检 A），说明 DI 通路与上下拉正常。


## 关键点 / 坑

1. **必须开漏 + 上拉**：GPIO 推挽输出高会与从机的低电平"打架"，I2C 线必须开漏（OD=1）依靠上拉回高。
   本样例把这一点做成自检项（驱动 0 读到 0，释放读到 1）。
2. **"释放为高"是读 DI 判断的，不是读 DO**：软件 I2C 读 SDA 一定要 `gpio_read_pin()`，不能用输出寄存器。
3. 软件 SCL 频率受 **delay 循环精度**限制：本样例用 mcycle 做半周期控制，实测误差见第 05 章/表中 ppm。
   1 MHz 以上时中断/其他任务会直接破坏时序，软件 I2C 只在低速/调试/恢复场景推荐。
4. 扫描全 NACK 是**正常**（板上没有从器件）；接 AT24Cxx 后应看到 `ACK @0x50`。
5. 用软件 I2C 做总线恢复是很实用的：硬件 I2C 外设卡死时，先切 GPIO 打 9 个时钟再切回外设。
