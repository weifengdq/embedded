# app_j1939 —— SAE J1939 协议栈（HPM5E31）

面向 hpm5e31_1 的 J1939 协议栈，覆盖 J1939-21（数据链路/传输）、J1939-71（周期 PGN）、
J1939-73（诊断 DM1/DM2/DM3/DM11）、J1939-81（地址声明）。协议核心与硬件解耦，
通过端口层收发 29 位扩展帧；无动态内存，可同时实例化多个节点（ECU + 测试仪）。

## 文件

| 文件 | 内容 |
|---|---|
| `inc/app_j1939.h` | 公共类型与 API（ID/PGN 宏、NAME、DTC、调度表、统计） |
| `inc/j1939_port.h` | 端口层接口 + `j1939_bus_t`（绑定一路 MCAN） |
| `inc/j1939_internal.h` | TP/DM/分发内部接口 |
| `src/j1939_id.c` | 29 位 ID 与 PGN 互转、默认优先级、NAME 编解码 |
| `src/j1939_stack.c` | 初始化、地址声明状态机、请求/应答、周期调度、收发分发 |
| `src/j1939_tp.c` | BAM 与 RTS/CTS（T1~T4 超时、CTS 窗口续发） |
| `src/j1939_dm.c` | DM1/DM2 编码、DM3/DM11 清除、灯映射 |
| `src/j1939_port_mcan.c` | MCAN 端口：250 kbit/s、SP 87.5%、扩展帧收发、us/ms 时基 |

## 接入

```cmake
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
sdk_inc(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/inc)
sdk_app_src(src/main.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_id.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_stack.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_tp.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_dm.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_port_mcan.c)
```

节点初始化：

```c
j1939_bus_init(&bus, BOARD_CAN0_BASE, msg_buf, 250000U);   /* msg_buf 见例程 */
j1939_cfg_t cfg = {.auto_claim = true, .preferred_sa = 0x00, .name = NAME, .bam_gap_ms = 50, .cts_gap_ms = 1};
j1939_init(&stack, &bus, &cfg);
j1939_sched_add(&stack, J1939_PGN_EEC1, 10, 3, fill_eec1, NULL);
j1939_dm_init(&stack);
/* 主循环：1 kHz 以上调用 */
j1939_poll(&stack);
```

## 行为约定

- 地址声明完成前**不发送周期 PGN**（调度器门控），只发请求 0xEA00；
- `j1939_send()` 长度 <=8 自动单帧，>8 自动选 BAM（DA=0xFF）或 RTS/CTS；
- 请求 0xEA00 的应答：PDU2 报文仍是广播（标准如此），不要用 DA 区分；
- TP 接收方收满窗口会自动续发 CTS；BAM 发送间隔默认 50 ms，可用 `cfg.bam_gap_ms` 调整；
- 统计见 `stack.stats`（收发计数、TP 包间隔 min/max、EOMA 往返、请求应答时延）。

## 实测

- 地址声明 251.167 ms；EEC1 10 ms 周期 1.5 s 内 150 帧（平均 10000 µs，最大偏差 +1492 µs）；
- 请求/应答端到端 955~988 µs；
- BAM 500 B 140.8 B/s，RTS/CTS 500 B 6867.9 B/s（详见第 31 章与 logs/）。
