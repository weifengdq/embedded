# gpio_led_bank —— 8 颗 CAN 状态灯的位图控制 / 跑马灯 / 软件 PWM 呼吸

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

8 颗高亮 LED 全挂在 **GPIOF** 上，可以一次性整字写：

| idx | 灯 | 引脚 |
|---|---|---|
| 0 | CAN0 RED | PF26 |
| 1 | CAN0 WHITE | PF18 |
| 2 | CAN1 RED | PF20 |
| 3 | CAN1 WHITE | PF16 |
| 4 | CAN2 RED | PF22 |
| 5 | CAN2 WHITE | PF21 |
| 6 | CAN3 RED | PF23 |
| 7 | CAN3 WHITE | PF19 |

- `ledbank_mask <hex>`：一次写 8 位图案（`DO[F].SET` 位操作）。
- `ledbank_marquee`：跑马灯，每次只亮一颗。
- `ledbank_breath`：**纯软件 PWM** 呼吸（2 ms 周期切成 50 份），同时说明 CPU 代价。
- `ledbank_seq`：模拟 4 路 CAN 收发指示（RED/WHITE 交替）。
- 上电自动走一遍 walking-ones，自证引脚映射。

## 编译与运行

```bash
bash scripts/build.sh gpio/gpio_led_bank
bash scripts/hwrun.sh gpio/gpio_led_bank --secs 8 --send "ledbank_info;ledbank_mask 55;ledbank_marquee 60 8;ledbank_breath 1"
```

## 实测现象

实测（`logs/20261030-113910-gpio_led_bank.log`）：

```text
mask=0x37, DO[F]=0x04740000
marquee: period=40 ms, steps=6
  t=979 ms -> CAN0.RED(PF26) mask=0x01
  t=1020 ms -> CAN0.WHITE(PF18) mask=0x02
  t=1061 ms -> CAN1.RED(PF20) mask=0x04
  t=1101 ms -> CAN1.WHITE(PF16) mask=0x08
  t=1142 ms -> CAN2.RED(PF22) mask=0x10
  t=1183 ms -> CAN2.WHITE(PF21) mask=0x20
breath: duty=0/20/40/60/80/100/80/60/40/20 % (软件 PWM, 2 ms 周期 50 步)
```

## 关键点 / 易错点

1. **灯是"高亮"接法**：写 1 点亮（`DO` 置位）。若换板要确认极性。
2. 一次 `DO[F].VALUE` 写覆盖整个端口会连同 PF17/PF24/PF25 一起改写，所以本样例用 `SET` 位操作只动 8 颗灯，
   否则会遇到 PF19/PF22/PF23（USB0 PWR/ID/OC 复用）等其它功能。
3. 软件 PWM 呼吸会**独占 CPU**：`ledbank_breath` 期间 shell 命令不响应，实测每个 2 ms 周期要切 50 次，
   CPU 基本 100% 占用；要真正省 CPU 应用 PWMv2 外设（第 14 章）或 GPTMR+DMA。
4. 8 颗灯与 CAN 功能共用引脚，跑本样例时不要同时跑 CAN 收发（本板 CAN 状态灯就是被 CAN 例程复用的）。
