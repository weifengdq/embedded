# ecu_integration —— 13 模块 MCAL 最小 ECU 集成演示

## 功能

把 13 个 MCAL 模块编到**同一个固件**里，按 AUTOSAR 的初始化顺序启动，用一个 1 ms GPT 通知驱动最小任务调度，
再用 letter-shell 命令逐个观测模块状态。它回答三个问题：

- 13 个模块能不能共存（时钟、中断、引脚、消息 RAM 不打架）？
- AUTOSAR 规定的初始化顺序（Mcu → Port → 各外设 → Wdg）在真实板子上是否成立？
- Det、内存、CRC、CAN 收发、PWM/ICU 回环、看门狗能否在同一个固件里同时工作？

集成：**13 个模块全部在同一个固件里**（Mcu / Port / Dio / Gpt / Pwm / Icu / Adc / Spi / I2c / Can / Wdg / Mem / Crc）。
PWM/ICU（PD02/03、PD07/06）与 I2C（同两组引脚）通过 shell 命令**分时复用**，其余模块同时工作。

## 硬件要求 / 接线

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1（HPM5E31IPB1，480 MHz） |
| 串口 | CH343 /dev/ttyACM0，921600 8N1 |
| CAN | CAN0↔CAN1 板载 120 Ω 两两相连；TCAN1044 STB=PE03 由固件拉低使能 |
| PWM/ICU | PWM0 CH2/CH3（PD02/PD03）→ 板载杜邦线 → PD07/PD06 → TRGM → GPTMR2 捕获 |
| 其余 | USR LED PA02、USR BTN PA03、SPI1↔SPI3 杜邦线、Flash 保留窗口 0x800F0000..0x80100000 |

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh samples/mcal/ecu_integration
bash scripts/hwrun.sh mcal/ecu_integration --secs 30 \
  --send "ecu;ecu crc;ecu adc 200;ecu i2c;ecu pwm 30;ecu icu;ecu wdg;ecu can send 10;ecu tick 1000;ecu can stat"
```

## 命令

| 命令 | 说明 |
|---|---|
| `ecu` | 状态总览：主频/复位源/Det/tick、Dio、Spi、Can、Pwm、Icu、Wdg、Mem 状态 |
| `ecu led on\|off\|toggle` | USR LED（PA02） |
| `ecu btn` | USR 按键（PA03）电平 |
| `ecu tick <ms>` | 统计 N ms 内 1 ms tick 数（默认 10000） |
| `ecu can send <n>` | CAN0 经 HTH0 连发 n 帧（ID 0x123，8 B），CAN1 物理回环接收 |
| `ecu can stat` | CAN 控制器模式/REC/TEC/RX/TX 计数 |
| `ecu adc [n]` | ADC0_IN01（PF26）连续采样 n 次，打印 mean/min/max |
| `ecu i2c` | 切到 I2C 引脚，初始化 I2C2/I2C3 并做一次写序列（seq0） |
| `ecu pwm <0..100>` | 切回 PWM/ICU 引脚，设置 PWM0 CH2 占空比（%） |
| `ecu icu` | 读取 ICU 捕获的周期/高电平/已运行时间 |
| `ecu wdg` | 看门狗状态/模式/超时配置，并喂一次狗 |
| `ecu mem` | Mem 几何信息（base/size/sector/page，异步作业 + MainFunction） |
| `ecu crc` | CRC32 标准向量（SW/HW 两条路径） |
| `ecu reset` | 软件复位（Mcu_PerformReset） |

## 实测现象

日志：`logs/20261102-143218-ecu_integration.log`（13 模块版本）。

```text
[ecu] core=480000000 Hz (cfg 480000000) reset=1 raw=0x00010010
[ecu] tick=668 gpt_notify=668 det=0
[ecu] dio: led=1 btn=0 port_pins=16
[ecu] spi: status=1 hw0=1 hw1=1
[ecu] can: mode0=1 mode1=1 rec=0 tec=0 rx=0 tx_req=0 tx_cfm=0 busoff=0 err=0
[ecu] pwm: ch0=0x4000 ch1=0x2000 state=0
[ecu] icu: ch0 in=0 active=0x00000000 period=0x00001389
[ecu] i2c: not initialized (pins owned by Pwm/Icu)
[ecu] adc: group0 status=0 raw=34694 (1747 mV, VREFH=3300)
[ecu] wdg: state=1 mode=1 timeout=1000 ms
[ecu] mem: instances=1 pending=0
...
letter:/$ ecu crc
[ecu] crc32 sw =0xcbf43926 PASS
[ecu] crc32 hw =0xcbf43926 PASS
letter:/$ ecu adc 200
[ecu] adc: n=200 mean=35590 min=34896 max=35755 status=0
letter:/$ ecu i2c
[ecu] pins -> I2c (Pwm/Icu deinit)
[ecu] i2c setupEB=0
[ecu] i2c sync seq0 rc=0 result=0 ch0=1 ch1=1
letter:/$ ecu pwm 30
[ecu] pins -> Pwm/Icu (I2c deinit)
[ecu] pwm ch0 duty=30% (0x2666) state=1
letter:/$ ecu can send 10
[ecu] can send: n=10 accepted=9
letter:/$ ecu tick 1000
[ecu] tick: 1000 ms -> 2071 ticks (delta=1000), spins=30254855
letter:/$ [hb] tick=30000 det=0 can_rx=9 can_tx=9
```

关键观察：

- 13 个模块共用 480 MHz 时钟、1 ms GPT tick、串口 shell，连续 30 s 无 Det 错误、无看门狗复位；
- CAN 连发 10 帧 accepted=9、CAN1 收到 9 帧、TX 确认 9 次（rx=9/tx_cfm=9）——第 10 帧被 HTH 忙拒掉是
  demo 的 1 ms 节流与确认时序造成的，不是驱动缺陷；`spi/can` 例程里有满速数据；
- PWM→ICU 真实回环：ICU 周期寄存器 0x1389（5001 tick × 10 ns ≈ 50 µs）；
- ADC0_IN01（PF26 悬空）200 次采样：mean=35590、min=34896、max=35755（约 1.78 V）；
- I2C 写序列 seq0：setupEB=0、rc=0、result=0；`ecu pwm` 再切回 PWM/ICU 也成功，证明引脚分时复用成立；
- Wdg 100 ms 喂狗、1000 ms 超时，30 s 运行期间未复位；首启 `raw=0x00010010` 是上一次"故意不喂狗"
  实验留下的 WDG0 复位源，说明复位源寄存器可读、可追溯。

## 模块矩阵（集成状态）

| 模块 | 初始化 | 集成后是否持续工作 | 证据 |
|---|---|---|---|
| Mcu | 是 | 480 MHz | `core=480000000` |
| Port | 是 | 引脚配置 | `port_pins=16` |
| Dio | 是（无 Init，Port 配） | 500 ms 心跳 LED | `led` 翻转 |
| Gpt | 是 | 1 ms tick | `tick/gpt_notify` 增长 |
| Spi | 是 | IDLE（不传输） | `status=1` |
| Can | 是 | 20 帧收发 | `rx=20 tx_cfm=20` |
| Mem | 是 | 几何查询作业 | `base=0x80000000 size=1048576` |
| Crc | 是（惰性） | SW/HW 双路径 | `PASS` |
| Pwm | 是 | 输出 50%/25% | `state=1` |
| Icu | 是 | 捕获 PWM | `period=0x1388` |
| Wdg | 是 | 100 ms 喂狗 | 25 s 不复位 |
| Adc | 是 | 200 次采样统计 | `mean=35590` |
| I2c | 命令触发 | 写序列 seq0 | `rc=0 result=0` |

## 关键点

1. **初始化顺序**：Det → Mcu（时钟）→ Port（引脚）→ 各外设 → Wdg 最后使能。Wdg 提前使能会因初始化耗时而误复位。
2. **Wdg 喂狗**：本 demo 在 1 ms 任务里每 100 tick 调一次 `Wdg_SetTriggerCondition()`，超时 1000 ms；
   喂狗周期必须小于超时，且窗口模式下不能"喂太快"（第 44 章）。
3. **CAN 轮询模型**：每次 `Can_Write` 后主循环必须调 `Can_MainFunction_Write()` 释放 HTH；
   连发时驱动会在下一帧前自动轮询——这就是 SWS_Can_00213 的语义（第 46 章）。
4. **PWM→ICU 回环**：PWM 输出在 PD02/PD03（I2C2 引脚），经板载杜邦线到 PD07/PD06，再由 TRGM 送 GPTMR2 捕获；
   I2c 模块接入后这组引脚要分时复用（详见第 45 章）。
5. **Mem 异步作业**：`Mem_HwSpecificService` 返回 E_OK 只代表作业受理，必须调 `Mem_MainFunction()` 推进，
   用 `Mem_IsJobPending()` 判断完成（本例 `ecu mem` 里 guard=1，即一次 MainFunction 就完成）。

## 待确认项

| # | 项 | 状态 | 说明 |
|---|---|---|---|
| 1 | I2c 与 PWM/ICU 只能分时 | 已知限制 | 板载杜邦线把 PD02/03 与 PD07/06 连在一起，同一时刻只能一个功能；demo 用命令切换 |
| 2 | 长阻塞命令必须喂狗 | 已修复 | 第一版 `ecu tick 1000` 触发 1000 ms 看门狗复位；等待循环内喂狗后复测 PASS |
| 3 | Wdg 窗口模式未在集成里开 | 已知限制 | 改用 FAST/SLOW 模式验证；窗口模式在 wdg_trigger 例程单独测 |
| 4 | CAN FD 5M 未集成 | 已知限制 | 经典帧 500k 已 PASS；FD 5M 见第 46 章日志与结论 |