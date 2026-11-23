# dio_led_button —— AUTOSAR Dio 读写 / 通道组 / 错误路径

一句话：`Port_Init` 配好引脚后，用 Dio 的 Read/Write/Flip/ReadPort/WritePort/ChannelGroup 操作
USR LED(PA02)、USR BTN(PA03) 和 8 颗 CAN 状态灯，并演示 Det 错误路径。

## 硬件要求 / 接线

| 资源 | 引脚 | 说明 |
|---|---|---|
| USR LED | PA02 | 高电平点亮；兼 BOOT_MODE0 |
| USR BTN | PA03 | 按下为高；兼 BOOT_MODE1，**复位时按住会进 ISP** |
| CAN0 灯 | PF26(红) / PF18(白) | PF26 同时是 ADC0_IN01，跑 Adc 例程时不要同时驱动 |
| CAN1 灯 | PF20(红) / PF16(白) | — |
| CAN2 灯 | PF22(红) / PF21(白) | — |
| CAN3 灯 | PF23(红) / PF19(白) | — |

无需外部接线；通道组示例用 GPIOF[23:16]（含 6 颗灯）。

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/dio_led_button
bash scripts/hwrun.sh mcal/dio_led_button --secs 30 --send 'dio;dled on;dled flip;dbtn;dleds A5;dport 5;dgroup;dmasked 0 0xFF;dscan;derr;dver'
```

| 命令 | 作用 |
|---|---|
| `dio` | 模块信息、通道表、可用命令 |
| `dled on\|off\|flip` | USR LED 写 1/0/翻转，并回读 |
| `dbtn` | 读 USR BTN（按下为高）+ 按键事件计数 |
| `dleds <hex>` | 8 颗 CAN 灯图案（bit0..bit7 对应 LED0..LED7），打印 DO 与逐通道回读 |
| `dport <0..5>` | `Dio_ReadPort`（GPIOA..GPIOF），同时打印 DO |
| `dgroup` | `Dio_ReadChannelGroup`：GPIOA[3:2]（LED/BTN）、GPIOF[23:16]（灯） |
| `dmasked <lvl> <mask>` | `Dio_MaskedWritePort`（GPIOF） |
| `dscan` | 8 颗灯走 1 扫描 |
| `dperf [n]` | 测 `Dio_WriteChannel`/`Dio_ReadChannel` 单次开销（mchtmr 计时，默认 100 万次） |
| `derr` / `dver` | 触发 6 条 Dev Error / 版本信息 |

## 实测现象（串口输出）

```text
SELFTEST: LED write=1 readback=1
Dio: moduleID=120 channels=16 groups=3 (Dio 无 Init，引脚由 Port_Init 配)
letter:/$ dled on
LED: write=1 readback=1
letter:/$ dled flip
LED: write=0 readback=0
letter:/$ dleds A5
LEDS: pattern=0xA5 DO(GPIOF)=0x04380000
LEDS: DI readback: ch4(PF26)=1 ch5(PF18)=0 ch6(PF20)=1 ch7(PF16)=0 ch8(PF22)=0 ch9(PF21)=1 ch10(PF23)=0 ch11(PF19)=1
letter:/$ dgroup
GROUP Usr  (GPIOA[3:2]) = 0x0 (bit0=PA02 LED bit1=PA03 BTN)
GROUP CanLeds(GPIOF[23:16]) = 0x3A (位号=引脚号-16，不是 LED 序号：PF26/PF19 不在组内)
letter:/$ derr
DET: before=0 after=6 delta=6
letter:/$ dperf 1000000
PERF: n=1000000 write: ticks=3050250 total=127093750ns ns/op=127 ; read: ticks=4440003 total=185000125ns ns/op=185
```

日志：`logs/20261102-135945-dio_led_button.log`（功能最终）、`logs/20261102-140549-dio_led_button.log`（dperf 性能数据）、
`logs/20261102-135816-dio_led_button.log`（LOOP_BACK 修复后首次 PASS）。

## 关键点

- **AUTOSAR 的 Dio 没有 Dio_Init**：方向/上下拉由 Port 模块负责，Dio 只做电平读写；
  通道→GPIO 端口/位映射写在 `mcal/Dio/cfg/Dio_PBcfg.c`（pre-compile 语义）。
- 输出通道的 `Dio_ReadChannel` 返回真实引脚电平（依赖 Port 置 `LOOP_BACK=1`），
  所以 `dled on` 后回读是 1，而不是输出寄存器值。
- 按键在主循环里做"连续 5 次采样一致"的软件消抖，按下时翻转 LED 并打印事件。
- 通道组的 bit 位置是**引脚号**不是 LED 序号：8 颗灯分散在 PF26/PF18/PF20/PF16/PF22/PF21/PF23/PF19，
  所以 `GPIOF[23:16]` 组只能覆盖其中 6 颗（这也是 CAN 灯不适合做连续通道组的原因）。

## 待确认项

- 已实测：单片 LED 写/读/翻转、8 灯图案+逐通道回读、端口读、通道组读、掩码写、6 条 Det 错误。
- 已知限制：通道组写未驱动全部 8 颗灯（灯的引脚不连续），代码里用逐通道写补足，README 已说明。
