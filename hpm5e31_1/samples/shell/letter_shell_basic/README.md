# letter_shell_basic — letter-shell 3.2.4 移植示例

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

演示 letter-shell 3.2.4 在本板的移植：UART0 中断接收 + 512 B 环形缓冲、命令行编辑/历史、
通用命令（`help/ver/uptime/clk/pin/reg/mem/reboot/cmds/clear`）与例程自定义命令（`hello/led`）。

## 硬件要求 / 接线

串口 `/dev/ttyACM0`，921600 8N1。

## 编译与运行

`bash
bash scripts/hwrun.sh shell/letter_shell_basic --secs 6 --send "help;ver;clk;led toggle;pin A2"
`

## 实测现象（串口输出）

`text
letter:/$ ver
board       : hpm5e31_1
soc         : HPM5E31 (RV32-IMAFDCBP @ 480000000 Hz)
...
letter:/$ led toggle
USR LED(PA02) = 1
letter:/$ pin A2
A2 = 1
`

日志：`logs/20261030-104826-letter_shell_basic.log` 等。

## 关键点 / 易错点

- `shell_cfg.h` 必须同时使能 **CR 与 LF** 回车，否则串口发 `\r\n` 时 LF 会被当成普通字符塞进下一行命令；
- 命令表用 GCC 段边界 `__start_shellCommand/__stop_shellCommand`（本仓库已补），否则命令找不到；
- 命令函数用 `SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN`，避免打印无意义的返回值；
- 主循环要周期调用 `bsp_shell_poll()`；阻塞型例程需自行拆分或放到中断/定时器。
