# MCAL 模块统计（自动生成）

> 本文件由 `tools/mcal_check.py --report` 生成，请勿手工编辑。
> 生成时间：2026-11-02 19:03:49；数据来源：mcal/ 源码、build/*-debug 的 .obj（riscv size）、logs/ 串口日志、/tmp/mcal_build.log。
> 体积为 Debug（-O0 级别）配置下 MCAL 目标文件之和，仅统计 `mcal/<Mod>/` 目录里的编译单元，不含 SDK 驱动与 BSP；Flash=text+data，RAM=data+bss。
> 注意 .data 里主要是 `*_PBcfg.c` 的配置表（例如 CAN 的 10 KB 大部分是滤波/报文配置表），量产配置可按需裁剪。
> 统计口径（与第 48 章表 48-8 对齐用）：每个模块的 .obj 取 text/data/bss 三段；Flash=text+data，RAM=data+bss —— 即 .data 同时占 Flash（初值）和 RAM（运行副本），不是重复统计。数据源为单次完整构建 build/ecu_integration-debug 下 `*/mcal/<Mod>/*.obj`，与遍历全部 build/*-debug 去重后的结果逐模块一致（2026-11-02 核对）。

## 汇总

| 指标 | 数值 |
|---|---|
| 模块数 | 15 |
| 源文件数（.c/.h） | 77 |
| 代码行数（LOC） | 15001 |
| 公共 API 数（声明去重） | 226 |
| Flash 占用（text+data） | 136931 字节（133.7 KiB） |
| RAM 占用（data+bss） | 53981 字节（52.7 KiB） |
| 例程数（samples/mcal） | 34 |
| 编译通过例程 | 28 |
| 静态检查 | PASS（0 错 / 0 警） |
| 尚未交付的模块 | 无（13 个模块齐全） |
| size 工具 | $HOME/toolchains/riscv32-unknown-elf-newlib-multilib/bin/riscv32-unknown-elf-size |

## 模块明细

| 模块 | 文件数 | LOC | API 数 | API_ID 数 | text(B) | data(B) | bss(B) | Flash(B) | RAM(B) | 例程 | 最新日志 | 状态 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Adc | 5 | 990 | 16 | 13 | 4870 | 0 | 69 | 4870 | 69 | adc_bench, adc_single, ecu_integration | 20261102-143824-adc_bench.log, 20261102-143752-adc_single.log, 20261102-160315-ecu_integration.log | 上板 PASS |
| Can | 6 | 2205 | 18 | 18 | 12164 | 10240 | 488 | 22404 | 10728 | can_busoff, can_classic_echo, can_fd_echo, can_filter, can_irq_perf, ecu_integration | 20261102-143510-can_busoff.log, 20261102-160850-can_classic_echo.log, 20261102-180603-can_fd_echo.log, 20261102-143308-can_filter.log, 20261102-143545-can_irq_perf.log, 20261102-160315-ecu_integration.log | 上板 PASS |
| Crc | 5 | 1005 | 17 | 9 | 12019 | 1 | 94 | 12020 | 95 | crc_bench, crc_integrity, ecu_integration | 20261102-135730-crc_bench.log, 20261102-140431-crc_integrity.log, 20261102-160315-ecu_integration.log | 上板 PASS |
| Dio | 5 | 470 | 12 | 9 | 1564 | 0 | 0 | 1564 | 0 | dio_led_button, ecu_integration, port_pin_config | 20261102-140549-dio_led_button.log, 20261102-160315-ecu_integration.log, 20261102-135603-port_pin_config.log | 上板 PASS |
| Eth | 6 | 1983 | 26 | 25 | 7384 | 37632 | 253 | 45016 | 37885 | eth_freertos_lwip, eth_irq, eth_lwip, eth_raw_echo, eth_stats, eth_trcv | 20261102-171553-eth_freertos_lwip.log, 20261102-184510-eth_irq.log, 20261102-184929-eth_lwip.log, 20261102-184421-eth_raw_echo.log, 20261102-184455-eth_stats.log, 20261102-164119-eth_trcv.log | 上板 PASS |
| EthTrcv | 5 | 953 | 21 | 21 | 3606 | 0 | 45 | 3606 | 45 | eth_freertos_lwip, eth_irq, eth_lwip, eth_raw_echo, eth_stats, eth_trcv | 20261102-171553-eth_freertos_lwip.log, 20261102-184510-eth_irq.log, 20261102-184929-eth_lwip.log, 20261102-184421-eth_raw_echo.log, 20261102-184455-eth_stats.log, 20261102-164119-eth_trcv.log | 上板 PASS |
| Gpt | 5 | 902 | 13 | 14 | 5482 | 0 | 53 | 5482 | 53 | ecu_integration, gpt_oneshot, gpt_periodic | 20261102-160315-ecu_integration.log, 20261102-141404-gpt_oneshot.log, 20261102-134707-gpt_periodic.log | 上板 PASS |
| I2c | 5 | 735 | 11 | 9 | 4338 | 1 | 92 | 4339 | 93 | ecu_integration, i2c_error, i2c_master_slave | 20261102-160315-ecu_integration.log, 20261102-160217-i2c_error.log, 20261102-180525-i2c_master_slave.log | 上板 PASS |
| Icu | 5 | 960 | 19 | 20 | 6666 | 0 | 93 | 6666 | 93 | ecu_integration, icu_capture, pwm_basic | 20261102-160315-ecu_integration.log, 20261102-143359-icu_capture.log, 20261102-143113-pwm_basic.log | 上板 PASS |
| Mcu | 5 | 857 | 15 | 20 | 3492 | 0 | 12 | 3492 | 12 | ecu_integration, mcu_clock_init, mcu_reset_mode | 20261102-160315-ecu_integration.log, 20261102-135140-mcu_clock_init.log, 20261102-140030-mcu_reset_mode.log | 上板 PASS |
| Mem | 5 | 821 | 15 | 13 | 4027 | 0 | 553 | 4027 | 553 | crc_integrity, ecu_integration, mem_bench, mem_flash_rw | 20261102-140431-crc_integrity.log, 20261102-160315-ecu_integration.log, 20261102-141425-mem_bench.log, 20261102-141617-mem_flash_rw.log | 上板 PASS |
| Port | 5 | 568 | 9 | 5 | 2020 | 0 | 5 | 2020 | 5 | dio_led_button, ecu_integration, port_pin_config | 20261102-140549-dio_led_button.log, 20261102-160315-ecu_integration.log, 20261102-135603-port_pin_config.log | 上板 PASS |
| Pwm | 5 | 806 | 12 | 10 | 6392 | 0 | 45 | 6392 | 45 | ecu_integration, icu_capture, pwm_basic, pwm_notify | 20261102-160315-ecu_integration.log, 20261102-143359-icu_capture.log, 20261102-143113-pwm_basic.log, 20261102-143253-pwm_notify.log | 上板 PASS |
| Spi | 5 | 1367 | 16 | 15 | 9792 | 4099 | 200 | 13891 | 4299 | ecu_integration, spi_async_job, spi_master_slave | 20261102-160315-ecu_integration.log, 20261102-144042-spi_async_job.log, 20261102-180706-spi_master_slave.log | 上板 PASS |
| Wdg | 5 | 379 | 6 | 4 | 1142 | 0 | 6 | 1142 | 6 | ecu_integration, wdg_timeout_reset, wdg_trigger | 20261102-160315-ecu_integration.log, 20261102-181731-wdg_timeout_reset.log, 20261102-141513-wdg_trigger.log | 上板 PASS |

## 例程编译结果

| 例程 | 使用模块 | 编译 | warning | 最新日志 | 状态 |
|---|---|---|---|---|---|
| adc_bench | Adc | OK | 0 | 20261102-143824-adc_bench.log | PASS |
| adc_single | Adc | OK | 0 | 20261102-143752-adc_single.log | PASS |
| can_busoff | Can | OK | 0 | 20261102-143510-can_busoff.log | PASS |
| can_classic_echo | Can | OK | 0 | 20261102-160850-can_classic_echo.log | PASS |
| can_fd_echo | Can | OK | 0 | 20261102-180603-can_fd_echo.log | PASS |
| can_filter | Can | OK | 0 | 20261102-143308-can_filter.log | PASS |
| can_irq_perf | Can | OK | 0 | 20261102-143545-can_irq_perf.log | PASS |
| crc_bench | Crc | OK | 0 | 20261102-135730-crc_bench.log | PASS |
| crc_integrity | Mem, Crc | OK | 0 | 20261102-140431-crc_integrity.log | PASS |
| dio_led_button | Port, Dio | OK | 0 | 20261102-140549-dio_led_button.log | PASS |
| ecu_integration | Mcu, Port, Dio, Gpt, Spi, Can, Mem, Crc, Pwm, Icu, Wdg, I2c, Adc | OK | 0 | 20261102-160315-ecu_integration.log | PASS |
| eth_freertos_lwip | Eth, EthTrcv | 未编译 | — | 20261102-171553-eth_freertos_lwip.log | 待修 |
| eth_irq | Eth, EthTrcv | 未编译 | — | 20261102-184510-eth_irq.log | 待修 |
| eth_lwip | Eth, EthTrcv | 未编译 | — | 20261102-184929-eth_lwip.log | 待修 |
| eth_raw_echo | Eth, EthTrcv | 未编译 | — | 20261102-184421-eth_raw_echo.log | 待修 |
| eth_stats | Eth, EthTrcv | 未编译 | — | 20261102-184455-eth_stats.log | 待修 |
| eth_trcv | Eth, EthTrcv | 未编译 | — | 20261102-164119-eth_trcv.log | 待修 |
| gpt_oneshot | Gpt | OK | 0 | 20261102-141404-gpt_oneshot.log | PASS |
| gpt_periodic | Gpt | OK | 0 | 20261102-134707-gpt_periodic.log | PASS |
| i2c_error | I2c | OK | 0 | 20261102-160217-i2c_error.log | PASS |
| i2c_master_slave | I2c | OK | 0 | 20261102-180525-i2c_master_slave.log | PASS |
| icu_capture | Pwm, Icu | OK | 0 | 20261102-143359-icu_capture.log | PASS |
| mcal_hello | — | OK | 0 | 20261102-133416-mcal_hello.log | PASS |
| mcu_clock_init | Mcu | OK | 0 | 20261102-135140-mcu_clock_init.log | PASS |
| mcu_reset_mode | Mcu | OK | 0 | 20261102-140030-mcu_reset_mode.log | PASS |
| mem_bench | Mem | OK | 0 | 20261102-141425-mem_bench.log | PASS |
| mem_flash_rw | Mem | OK | 0 | 20261102-141617-mem_flash_rw.log | PASS |
| port_pin_config | Port, Dio | OK | 0 | 20261102-135603-port_pin_config.log | PASS |
| pwm_basic | Pwm, Icu | OK | 0 | 20261102-143113-pwm_basic.log | PASS |
| pwm_notify | Pwm | OK | 0 | 20261102-143253-pwm_notify.log | PASS |
| spi_async_job | Spi | OK | 0 | 20261102-144042-spi_async_job.log | PASS |
| spi_master_slave | Spi | OK | 0 | 20261102-180706-spi_master_slave.log | PASS |
| wdg_timeout_reset | Wdg | OK | 0 | 20261102-181731-wdg_timeout_reset.log | PASS |
| wdg_trigger | Wdg | OK | 0 | 20261102-141513-wdg_trigger.log | PASS |

## 引用方式

- 第 42 章（MCAL 总览）：用上面的「汇总」表给全书 MCAL 规模/体积一句话数据。
- 第 48 章（Mem/Crc）与各模块章：用模块明细表里对应行的 LOC/API/Flash/RAM。
- 状态列的「上板 PASS」判据：该模块的例程存在 logs/*-<例程>.log 且编译通过；具体 PASS/FAIL 以各例程 README 与日志为准。
- 复现命令：`python3 tools/mcal_check.py --build --report`（编译 samples/mcal/* + 检查 + 生成本文件）。
