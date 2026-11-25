# lowpower_wait_timer — WAIT(WFI) 低功耗 + GPTMR 定时唤醒

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

主循环执行 `__WFI()` 进入 CPU WAIT 模式，GPTMR1 通道 0 每 100 ms 产生中断唤醒；
用 MCHTMR（24 MHz）记录"进 WFI → 唤醒返回"的延迟，统计 min/avg/max，并验证 WAIT 下 GPTMR 仍在计数。

## 硬件要求 / 接线

无外部接线。为便于串口观察，唤醒周期设为 100 ms（产品中可放大）。

## 编译与运行

`bash
bash scripts/hwrun.sh power/lowpower_wait_timer --secs 8 --send "lp"
`

## 实测现象（串口输出）

`text
[wake] cnt=... period=100.0 ms  wake_latency min/avg/max = ... us
`

日志：`logs/20261030-120934-lowpower_wait_timer.log`。

## 关键点 / 易错点

- WAIT 模式下 CPU 时钟门控、外设仍可运行（GPTMR 继续计数），适合"低功耗待机 + 周期唤醒"；
- 唤醒延迟包含中断入口与主循环恢复，实测为微秒级；
- 需要 `board_ungate_mchtmr_at_lp_mode()` 保证 WFI 下 MCHTMR 仍可读（BSP 已处理）；
- 更深的 STOP/SUSPEND 模式会关更多电源域，唤醒源与上下文保存规则不同，见第 22 章待确认项。
