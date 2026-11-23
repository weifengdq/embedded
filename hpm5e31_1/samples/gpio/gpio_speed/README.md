# gpio_speed —— GPIO 软件翻转/读写极限速度实测

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

用 `mcycle`（CPU 周期计数器，480 MHz）测量纯软件驱动的 GPIO 操作开销：

| 测试项 | 说明 |
|---|---|
| `empty_loop` | 空循环基线（扣除循环开销用） |
| `api_toggle` | `gpio_toggle_pin()` API |
| `reg_toggle` | 直接写 `GPIO->DO[x].TOGGLE` |
| `reg_set_clear` | 交替写 `SET`/`CLEAR`（一次完整方波） |
| `port_word_write` | 端口整字写 `DO[F].VALUE`，同时驱动 8 颗 CAN 状态灯 |
| `read_pin` | `gpio_read_pin()` 单脚读 |
| `read_port` | `DI[F].VALUE` 端口整字读 |
| `fgpio_toggle` | 经 GPIOM 映射到 `HPM_FGPIO`（核心私有快速总线）后翻转 |

## 硬件 / 接线

- 单脚：USR LED **PA02**（高电平点亮）。
- 端口：**GPIOF** PF16/PF18/PF19/PF20/PF21/PF22/PF23/PF26 = 8 颗 CAN 状态灯。
- 测量本身不需要任何外部接线；如需看波形，用示波器量 PA02 或 PF 灯脚。

## 编译与运行

```bash
bash scripts/build.sh gpio/gpio_speed
bash scripts/hwrun.sh gpio/gpio_speed --secs 8 --send "spd_info;spd 500000;spd_dump"
```

## 命令

| 命令 | 作用 |
|---|---|
| `spd_info` | 引脚/端口/用法 |
| `spd [N]` | 跑完整基准（N 为每项迭代数，默认 200000） |
| `spd_bank [value]` | 直接写 GPIOF 整字（观察 8 颗灯） |
| `spd_fgpio [N]` | 单独测 FGPIO 翻转 |
| `spd_dump` | 打印 GPIO0 DO/DI[A]/[F] 与 GPIOM 控制器映射 |

## 实测现象（串口输出）

实测（`logs/20261030-110857-gpio_speed.log`，CPU 480 MHz，N=500000）：

```text
empty_loop         cyc/op=5.00   ns/op=10.41
api_toggle         cyc/op=31.20  ns/op=65.00
reg_toggle         cyc/op=31.20  ns/op=65.01
reg_set_clear      cyc/op=62.40  ns/op=130.00
port_word_write    cyc/op=31.20  ns/op=65.00
read_pin           cyc/op=55.20  ns/op=115.00
read_port          cyc/op=36.00  ns/op=75.00
fgpio_toggle       cyc/op=7.00   ns/op=14.58
```

## 关键点 / 坑

1. `mcycle` 是 64 位 CSR，读数在 480 MHz 下分辨率 2.08 ns；**必须减掉循环本身的开销**，否则小 N 时误差很大。
2. 代码运行在 XPI Flash（XIP）上，指令取指经过 cache；如需最纯粹的寄存器访问开销，应把测试函数放 ILM（本样例未做，属"待确认"项）。
3. 写 `TOGGLE` 只产生**一次**总线写；交替 `SET/CLEAR` 是两次写，所以 `reg_toggle` 约是 `reg_set_clear` 的一半。
4. `FGPIO` 需要先用 **GPIOM** 把引脚的控制器从 `gpiom_soc_gpio0` 改成 `gpiom_core0_fast`（并打开可见性），否则写 `HPM_FGPIO` 不起作用——这是最常见的"FGPIO 没反应"原因。
5. 软件能达到的翻转频率远高于引脚实际电平建立/边沿速率，**真正的最高方波频率受 PAD_CTL 的 SR/SPD 与外部负载限制**，需示波器实测（待确认）。
6. 本样例结束会把 GPIOF 灯全部熄灭、把 PA02 的 GPIOM 映射还原，避免影响后续测试。
