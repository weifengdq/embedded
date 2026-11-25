# blinky_btn — USR LED / USR BTN 最基础例程

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

USR LED（PA02，高电平点亮）周期性闪烁；按下 USR BTN（PA03，按下为高）切换闪烁频率（500 ms → 100 ms → 50 ms 循环）。
同时演示 IOC 引脚配置、GPIO 输出/输入、`board_delay_ms()`。

## 硬件要求 / 接线

- 无外部接线；使用板载 USR LED / USR BTN。
- ⚠️ PA02/PA03 同时是 BOOT_MODE[0:1]，**复位瞬间不要按住按键**（会进 ISP 模式）。

## 编译与运行

`bash
bash scripts/hwrun.sh gpio/blinky_btn --secs 6
`

## 实测现象（串口输出）

`text
blinky_btn: press USR BTN (PA03) to change blink rate
btn pressed -> blink delay 100 ms
btn pressed -> blink delay 50 ms
btn pressed -> blink delay 500 ms
`

日志示例：`logs/20261030-120632-blinky_btn.log`（无按键时只有启动行，LED 在闪）。

## 关键点 / 易错点

- LED 高电平点亮（`BOARD_LED_ON_LEVEL=1`）；
- 按键按下为高（`BOARD_BUTTON_PRESSED_VALUE=1`），与官方 EVK 相反；`board_init_butn_pins()` 由 BSP 提供；
- 轮询读按键比中断简单，适合入门；中断+消抖见 `samples/gpio/gpio_irq_btn`。
