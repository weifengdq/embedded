# 《HPM5E31 实战手册》文档索引

> 本文件由 `tools/gen_index.py` 自动生成，**请勿手工编辑**；改目录请改 [00-目录.md](00-目录.md)。
> 当前版本：**v1.6.0** ｜ 合成命令：`tools/build_pdf.sh` ｜ 质量检查：`python3 tools/check_docs.py`

## 一、章节目录与进度

| 章 | 标题 | 文件 | 作者 | 状态 | 行数 | 二级标题 | 表 | 图 | 代码块 |
|---|---|---|---|---|---:|---:|---:|---:|---:|
| 00 | 前言、阅读指南与版本 | 00-preface.md | 作者 | 已交付（219 行） | 219 | 12 | 6 | 1 | 1 |
| 01 | HPM5E31 芯片与 hpm5e31_1 板卡 | 01-chip-and-board.md | 作者 | 已交付（354 行） | 354 | 12 | 13 | 5 | 0 |
| 02 | 开发环境、构建、烧写与调试 | 02-toolchain-build-flash.md | 作者 | 已交付（336 行） | 336 | 12 | 10 | 2 | 9 |
| 03 | letter-shell 与命令行调试框架 | 03-letter-shell.md | 作者 | 已交付（279 行） | 279 | 15 | 7 | 2 | 5 |
| 04 | GPIO / IOC / GPIOM | 04-gpio-ioc.md | 作者 | 已交付（292 行） | 292 | 14 | 12 | 2 | 1 |
| 05 | UART 与串口通信 | 05-uart.md | 作者 | 已交付（324 行） | 324 | 14 | 11 | 2 | 2 |
| 06 | I2C 总线 | 06-i2c.md | 作者 | 已交付（413 行） | 413 | 14 | 17 | 3 | 2 |
| 07 | SPI 总线 | 07-spi.md | 作者 | 已交付（407 行） | 407 | 14 | 14 | 3 | 2 |
| 08 | MCAN / CAN FD（4 路） | 08-canfd.md | 作者 | 已交付（613 行） | 613 | 11 | 16 | 4 | 3 |
| 09 | CAN FD 性能、压力与错误注入 | 09-canfd-perf.md | 作者 | 已交付（534 行） | 534 | 15 | 14 | 5 | 2 |
| 10 | LAN8651 10BASE-T1S 原理与移植 | 10-lan8651-t1s.md | 作者 | 已交付（333 行） | 333 | 13 | 12 | 4 | 2 |
| 11 | LAN8651 PLCA 主从、协议栈与性能 | 11-lan8651-plca-lwip.md | 作者 | 已交付（458 行） | 458 | 13 | 19 | 3 | 4 |
| 12 | YT8011AN 千兆 PHY 与 RGMII | 12-yt8011an-rgmii.md | 作者 | 已交付（463 行） | 463 | 13 | 15 | 4 | 3 |
| 13 | lwIP 协议栈、iperf 与以太网性能 | 13-lwip-iperf.md | 作者 | 已交付（532 行） | 532 | 13 | 20 | 6 | 5 |
| 14 | PWMv2 与 HRPWM | 14-pwmv2-hrpwm.md | 作者 | 已交付（463 行） | 463 | 12 | 14 | 3 | 7 |
| 15 | GPTMR 定时、捕获与测量 | 15-gptmr.md | 作者 | 已交付（346 行） | 346 | 13 | 12 | 3 | 3 |
| 16 | QEI/QEO 编码器接口 | 16-qei-qeo.md | 作者 | 已交付（355 行） | 355 | 12 | 12 | 3 | 5 |
| 17 | SDM / PLB / TRGM 可编程逻辑与互联 | 17-sdm-plb-trgm.md | 作者 | 已交付（474 行） | 474 | 14 | 20 | 5 | 4 |
| 18 | ADC16 采样、触发与精度 | 18-adc16.md | 作者 | 已交付（328 行） | 328 | 13 | 12 | 2 | 3 |
| 19 | DMA / DMAMUX 与数据搬运 | 19-dma.md | 作者 | 已交付（403 行） | 403 | 13 | 16 | 3 | 3 |
| 20 | USB 设备（CDC/HID） | 20-usb-device.md | 作者 | 已交付（345 行） | 345 | 13 | 13 | 2 | 5 |
| 21 | XPI NOR 存储与启动流程 | 21-xpi-flash-boot.md | 作者 | 已交付（322 行） | 322 | 13 | 13 | 2 | 2 |
| 22 | 看门狗、电源、低功耗与温度 | 22-wdg-power-temp.md | 作者 | 已交付（348 行） | 348 | 13 | 16 | 3 | 3 |
| 23 | 性能基准：CoreMark / Dhrystone / 内存 / 中断 / 计算 | 23-benchmarks.md | 作者 | 已交付（382 行） | 382 | 13 | 16 | 3 | 2 |
| 24 | 总线吞吐与实时性对比 | 24-bus-throughput.md | 作者 | 已交付（366 行） | 366 | 11 | 11 | 3 | 1 |
| 25 | 复位、启动与运行时可靠性 | 25-reliability.md | 作者 | 已交付（219 行） | 219 | 11 | 8 | 1 | 2 |
| 26 | 寄存器速查、勘误与陷阱清单 | 26-registers-errata.md | 作者 | 已交付（383 行） | 383 | 14 | 10 | 2 | 3 |
| 27 | 参考设计、同类对比与扩展阅读 | 27-comparison-reading.md | 作者 | 已交付（376 行） | 376 | 14 | 14 | 2 | 1 |
| 28 | 应用层测试台：CAN 网关、cannelloni 协议与上位机链路 | 28-app-bench.md | 作者 | 已交付（443 行） | 443 | 10 | 12 | 5 | 7 |
| 29 | ISO-TP 传输层：从帧格式到 4095 字节双向收发 | 29-isotp.md | 作者 | 已交付（461 行） | 461 | 12 | 16 | 4 | 5 |
| 30 | UDS 诊断服务：会话、安全访问、DTC 与刷写 | 30-uds.md | 作者 | 已交付（440 行） | 440 | 11 | 16 | 3 | 2 |
| 31 | DoIP：以太网诊断（ISO 13400-2） | 31-doip.md | 作者 | 已交付（425 行） | 425 | 15 | 17 | 5 | 4 |
| 32 | SAE J1939 商用车协议栈 | 32-j1939.md | 作者 | 已交付（528 行） | 528 | 13 | 12 | 5 | 6 |
| 33 | CANopen（CiA 301/302）协议栈移植与实测 | 33-canopen.md | 作者 | 已交付（471 行） | 471 | 11 | 19 | 5 | 4 |
| 34 | XCP 标定与测量（CAN / 以太网）与 A2L 工具链 | 34-xcp-calibration.md | 作者 | 已交付（706 行） | 706 | 13 | 22 | 7 | 4 |
| 35 | SOME/IP 面向服务通信（lwIP 服务端 + vsomeip 上位机） | 35-someip-vsomeip.md | 作者 | 已交付（572 行） | 572 | 13 | 14 | 5 | 9 |
| 36 | XRCE-DDS 与 ROS 2 集成 | 36-xrce-dds-ros2.md | 作者 | 已交付（380 行） | 380 | 12 | 15 | 4 | 3 |
| 37 | MQTT 与车云遥测 | 37-mqtt.md | 作者 | 已交付（576 行） | 576 | 13 | 23 | 4 | 8 |
| 38 | IEEE 1588 PTP / 802.1AS gPTP 时间同步 | 38-ptp-time-sync.md | 作者 | 已交付（510 行） | 510 | 13 | 12 | 3 | 10 |
| 39 | 车载日志与信号：DLT / VSS / candump 记录回放 | 39-dlt-vss-canlog.md | 作者 | 已交付（374 行） | 374 | 12 | 13 | 2 | 6 |
| 40 | 应用层综合评测与选型 | 40-app-comparison.md | 作者 | 已交付（491 行） | 491 | 13 | 15 | 3 | 1 |
| 41 | gs_usb / candleLight：把 HPM5E31 做成 4 路 CAN FD USB 适配器 | 41-gs-usb-candlelight.md | 作者 | 已交付（1019 行） | 1019 | 12 | 31 | 6 | 6 |
| 42 | MCAL 总览：AUTOSAR 分层、范围与路线 | 42-mcal-overview.md | 作者 | 已交付（344 行） | 344 | 12 | 12 | 3 | 2 |
| 43 | MCAL 基础设施、配置模型与集成 | 43-mcal-infra.md | 作者 | 已交付（338 行） | 338 | 12 | 11 | 3 | 6 |
| 44 | MCAL 核心驱动：Mcu / Port / Dio | 44-mcal-core.md | 作者 | 已交付（404 行） | 404 | 13 | 12 | 3 | 3 |
| 45 | 定时器与看门狗：Gpt / Pwm / Icu / Wdg 的 AUTOSAR 化 | 45-mcal-timer.md | 作者 | 已交付（398 行） | 398 | 13 | 8 | 3 | 1 |
| 46 | MCAL I/O 驱动：Spi / I2c / Adc | 46-mcal-io.md | 作者 | 已交付（452 行） | 452 | 13 | 16 | 4 | 3 |
| 47 | MCAL CAN 驱动：Can（4 路 MCAN / CAN FD） | 47-mcal-can.md | 作者 | 已交付（511 行） | 511 | 14 | 20 | 4 | 4 |
| 48 | Mem 与 Crc：片内 Flash 驱动与 CRC 库 | 48-mcal-mem-crc.md | 作者 | 已交付（544 行） | 544 | 15 | 10 | 4 | 5 |
| 49 | Eth 与 EthTrcv：千兆车载以太网 MAC 与 PHY 驱动 | 49-mcal-eth.md | 作者 | 已交付（1327 行） | 1327 | 12 | 41 | 11 | 7 |
| 50 | AUTOSAR 合规性与商用 MCAL 对比 | 50-mcal-compliance.md | 作者 | 已交付（650 行） | 650 | 17 | 20 | 3 | 2 |
| 51 | 附录：引脚表、接线、测量方法与日志索引 | 51-appendix.md | 作者 | 已交付（348 行） | 348 | 10 | 9 | 1 | 2 |

**进度**：已提交 52/52 章，达到质量门槛 49 章，累计 23379 行。

## 二、目录结构

| 路径 | 内容 |
|---|---|
| `docs/chapters/` | 书稿正文（每章一个 Markdown，文件名 `NN-*.md`） |
| `docs/figures/<章号>/` | 章内插图 PNG（300 dpi，由 `tools/figs/` 下脚本生成） |
| `docs/figures/_common/` | 全书公用图（封面、字体自检等） |
| `docs/build/` | 合成产物（PDF，已 gitignore） |
| `docs/_templates/chapter.md` | 章节模板，新章从它复制 |
| `docs/写作规范.md` | 写作规范与质量门槛（**动笔前必读**） |
| `tools/build_pdf.sh` | 按目录顺序合成单本 PDF（封面/版权/目录/页眉页脚） |
| `tools/check_docs.py` | 质量门槛检查（行数/标题/表/图/引用/链接/占位） |
| `tools/gen_index.py` | 生成本索引 |
| `tools/gen_regmap.py` | 从 SDK 头文件生成外设基地址/IRQ 速查表（第 26 章用） |
| `tools/figs/` | 所有插图的生成脚本 + 公共绘图库 |

## 三、构建与检查

```bash
cd hpm5e31_1
bash tools/build_pdf.sh                 # 合成 PDF（缺失章节自动跳过并告警）
bash tools/build_pdf.sh --only 26 --no-cover   # 只合成第 26 章，快速看版式
python3 tools/check_docs.py             # 全量质量检查（退出码非 0 = 不合格）
python3 tools/check_docs.py --file docs/chapters/26-registers-errata.md
python3 tools/gen_index.py              # 重新生成本索引
```

## 四、写作约定（摘要，详见 [写作规范](写作规范.md)）

1. 每章 ≥250 行、≥8 个二级标题、≥3 张表、≥2 张图、≥1 段代码。
2. 图必须由 `tools/figs/` 下脚本生成（graphviz/matplotlib），产物 300 dpi PNG。
3. 所有数字给出来源：`RM §x.y (p.N)` / `DS §x.y (p.N)` / `Errata E000xx` / 日志路径。
4. 必须包含“参考来源”与“待确认项与风险”两节；未实测的内容明确标注。
5. 章节文件命名 `NN-短横线英文名.md`，与 00-目录.md 保持一致。

## 五、章节状态说明

| 状态 | 含义 |
|---|---|
| 待写 | 尚未开始 |
| 进行中 | 正在写/正在实测 |
| 待评审 | 已提交，等待 QA |
| 完成 | 通过 `tools/check_docs.py` 且已进入 PDF |
