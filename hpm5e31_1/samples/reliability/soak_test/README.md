# soak_test — 长稳/自检例程

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

连续运行，每 1 ms 一轮做四件事：

1. 8 KB `memcpy` + 软件 CRC32 校验（SRAM 完整性，覆盖 AXI SRAM/DLM）；
2. `bsp_uptime_ms()` 单调性检查（mcycle 时基回绕/倒退）；
3. GPTMR0 1 ms 中断计数存活检查（连续 3 轮不涨 → 报错）；
4. 每 60 s 打印心跳（uptime/中断数/循环数/错误数）并翻转 USR LED。

## 硬件要求 / 接线

无外部接线。

## 编译与运行

`bash
# 30 分钟长稳（会独占整块板，用 --lock-wait 排队）
bash scripts/hwrun.sh reliability/soak_test --secs 1800 --lock-wait 1800
`

## 实测现象（串口输出，240 s 实测）

`text
[soak] start: buf=8 KB, heartbeat=60 s
[soak] beat=1 uptime=60.000 s irq=70249 cycles=19998 err=0
[soak] beat=2 uptime=120.001 s irq=140504 cycles=39998 err=0
[soak] beat=3 uptime=180.002 s irq=210758 cycles=59998 err=0
[soak] beat=4 uptime=240.000 s irq=281009 cycles=79997 err=0
`

日志：`logs/20261030-114255-soak_test.log`（0 错误）。

## 关键点 / 坑

- **GPTMR0 时钟默认未开**：必须先 `clock_add_to_group(clock_gptmr0,0)`，否则 shell banner 打到一半就停；
- 中断计数实测 1171 Hz vs 配置 1 kHz（+17%），说明 GPTMR 实际时钟与 `clock_get_frequency` 报告值有偏差，
  已在第 25 章列为待确认；
- 首次运行前 60 s 才出第一行心跳，短测请用 `--secs 70` 以上；
- shell 命令：`soak`（打印统计）、`heap`（malloc/free 测试）。
