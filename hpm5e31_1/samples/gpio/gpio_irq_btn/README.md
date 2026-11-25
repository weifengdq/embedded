# gpio_irq_btn —— PA03 按键中断 + 软件消抖 + 事件计数

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

- USR 按键 PA03 配置为 **双边沿中断**（GPIO0_A，IRQ 由 `BOARD_APP_GPIO_IRQ` 指定）。
- ISR 内做 **20 ms 时间窗消抖**（默认值可用 `btn_deb` 改），消抖后统计 press/release，并在主循环打印。
- USR LED PA02 跟随按键电平（按下点亮）。
- 提供 `btn_poll` 轮询方式对比（关闭引脚中断，按固定间隔采样读电平）。

## 硬件 / 接线

| 信号 | 引脚 | 说明 |
|---|---|---|
| USR BTN | PA03 | 按下为高电平，内部下拉（`init_butn_pins()`） |
| USR LED | PA02 | 高电平点亮（`init_led_pins()`） |
| 串口 | PA00/PA01 | 控制台 UART0，921600 8N1 |

## 编译与运行

```bash
cd hpm5e31_1
bash scripts/build.sh gpio/gpio_irq_btn
bash scripts/hwrun.sh gpio/gpio_irq_btn --secs 6 --send "btn_info;btn_stat;btn_poll 500;btn_deb 20;btn_stat"
```

## 命令

| 命令 | 作用 |
|---|---|
| `btn_info` | 打印引脚/中断号/触发方式/消抖窗口 |
| `btn_stat` | 打印 isr/press/release/bounce 计数 |
| `btn_reset` | 计数清零 |
| `btn_deb <ms>` | 设置消抖窗口（0~2000 ms） |
| `btn_edge <0..4>` | 0 上升沿 1 下降沿 2 高电平 3 低电平 4 双边沿 |
| `btn_irq <0\|1>` | 关闭/打开引脚中断 |
| `btn_poll <ms> [sample_us]` | 轮询方式统计边沿（用于和中断方式对比） |

## 实测现象（串口输出）

上电自测（`logs/20261030-111911-gpio_irq_btn.log`）：

```text
=== gpio_irq_btn (PA03 irq + debounce) ===
mchtmr=24000000 Hz, irq=1, debounce=20 ms, idle level=0
[selftest] loop_back=OK cycles=5 isr_delta=10 press_delta=5 release_delta=5
[selftest] expect press=5 release=5 when debounce=20 ms < gap=25 ms
```

实测日志：`logs/20261030-111911-gpio_irq_btn.log`（最终版日志：loop_back=OK、ISR=10、press/release 各 5 次；早期版本因未先跨过消抖窗少计 1 次，已在 111911 版本修复）
（用 ``` bash scripts/hwrun.sh gpio/gpio_irq_btn --secs 6 --send 'btn_info;btn_stat' ``` 复现）。

## 关键点 / 易错点

1. **PA03 需要内部下拉**：`init_butn_pins()` 里 `PE=1, PS=0`；若漏配，悬空脚会随机触发中断。
2. **GPIO0_A 是端口级中断向量**：ISR 里必须 `gpio_clear_pin_interrupt_flag()`，同一端口其他引脚的中断也走这个向量。
3. **ISR 里不要 printf**：本样例把事件速记到 `s_evt` 标志，由主循环打印，避免中断内阻塞串口。
4. 时间窗消抖只能滤掉慢速抖动；机械按键的亚毫秒抖动配合 PAD_CTL 的 HYS（施密特）更稳。
5. `mchtmr` 固定 24 MHz（`clock_mchtmr0`），用 counts/ticks_per_ms 换算毫秒比 `bsp_uptime_ms()` 更适合在 ISR 里用。
