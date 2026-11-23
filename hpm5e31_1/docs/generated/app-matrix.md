# 应用层例程证据矩阵（app-matrix）

> 本文件由 `tools/gen_app_matrix.py` 自动生成，**请勿手改**；数据来源：`samples/app/*/README.md`（实测/关键实测结论表格优先）、`samples/app/*/src/*.c`、`logs/*-<例程>.log`（最新一份兜底）。

| 项 | 值 |
|---|---|
| 生成时间 | 2026-11-02 19:03 |
| 例程数 | 28（`samples/app/`） |
| 协议分布 | CAN 网关 1，CAN 记录 1，CANopen 3，DLT 1，DoIP 1，GS-USB 1，ISO-TP 1，J1939 3，MQTT 1，PTP 2，SOME/IP 3，UDS 2，VSS 1，XCP 3，XRCE-DDS 2，gPTP 1，多协议共存 1 |
| bench 说明 | A=板内双节点自测；B=主机 vcan0 + cannelloni 网关；C=以太网直连上位机 |

## 1. 证据矩阵

| 例程 | 协议 | 传输 | CAN ID / IP:端口 | 上位机工具 | 关键实测指标 | 结论 | 证据日志 | bench |
|---|---|---|---|---|---|---|---|---|
| `can_gateway` | CAN 网关（cannelloni v2） | CAN over UDP（cannelloni v2） | CAN ID: 0x123, 0x321, 0x7FF, 0x777 / 端口: 4000 | host/can_bridge | loopback PASS rc=0 (OK) | PASS | logs/20261030-184706-can_gateway.log | B |
| `can_logger` | CAN 记录（candump 回放） | CAN 经典帧 500 kbit/s | CAN ID: 0x300 / 端口: 4000 | host/canlog | 0x300 生成速率（数值）= 100 帧/s（gen 10 ms，680 ms 内 68 帧） | PASS | logs/20261102-112029-can_logger.log | B |
| `canopen_node` | CANopen（CiA 301） | CAN 经典帧 500 kbit/s | 端口: 4000 | host/canopen | maxloop=2945 us 出现在上电初始化阶段，稳态 co_node_poll() 开销远小于 1 ms（见第 32 章）。 | PASS | logs/20261102-110507-canopen_node.log | A |
| `canopen_pdo` | CANopen（CiA 301 PDO） | CAN 经典帧 500 kbit/s | CAN ID: 0x1801, 0x280, 0x281 | host/canopen/ | bidir n=128 FAIL errors=1 time=55.78 us bw=2294 KB/s | FAIL | logs/20261102-122759-canopen_pdo.log | B |
| `canopen_sdo_seg` | CANopen（CiA 301 SDO） | CAN 经典帧 500 kbit/s | - | host/canopen/ | 分段 upload 吞吐 > 1000 B/s PASS 1KB=14039 B/s 4KB=14194 B/s | PASS 6/0 | logs/20261030-184728-canopen_sdo_seg.log | A |
| `dlt_log` | DLT（COVESA DLT 日志） | Ethernet（UDP 3490） | IP: 192.168.2.1, 192.168.2.100/24, 192.168.2.1/24 / 端口: 3490 | host/dlt | [app] dlt -> 192.168.2.1:3490, 4 ctx x 5 level, 10 ms | - | logs/20261102-181543-dlt_log.log | A |
| `doip_server` | DoIP（ISO 13400-2（UDP 车辆发现 + TCP 13400）） | Ethernet（UDP/TCP 13400） | IP: 192.168.2.1/24, 192.168.2.100/24, 192.168.2.1 / 端口: 13400 | host/doip | 出向 4008 B 帧 = 375.3 µs / 85.4 Mbit/s（单帧） | PASS | logs/20261102-184416-doip_server.log | C |
| `gptp_demo` | gPTP（IEEE 802.1AS 风格） | Ethernet（lwIP） | IP: 224.0.0.107, 224.0.1.129, 192.168.2.100/24 | host/ptp | 判据 = ≤ 1000 ns 且 σ ≤ 500 ns → PASS | PASS | logs/20261102-102525-gptp_demo.log | C |
| `gs_usb_candle` | GS-USB（USB↔CAN（gs_usb/candleLight）） | USB（gs_usb / candleLight） | 端口: 4000 | host/gs_usb | CAN FD 1M/5M 64 B 双向 = can0→can1 与 can1→can0 各 4000/4000 帧，丢帧 0、tx_err/rx_err 0、ERROR-ACTIVE，6 988.5 / 6 995.1 帧/s（64 B 载荷 3.58 Mbit/s） | PASS | logs/20261102-181312-gs_usb_candle.log | C |
| `isotp_echo` | ISO-TP（ISO 15765-2） | CAN 经典帧 500 kbit/s | CAN ID: 0x18DAF110 | host/uds/ | 双向吞吐 KB/s = 189.6（长度 B=4095） | PASS | logs/20261102-120902-isotp_echo.log | A |
| `j1939_dm` | J1939（J1939-73 诊断） | CAN 经典帧 250 kbit/s | - | host/j1939/ | [PASS] DM2 history: frames=1 dtc=2 (SPN 100/190 via RTS/CTS) | PASS 7/0 | logs/20261030-184749-j1939_dm.log | A |
| `j1939_ecu` | J1939（J1939-21/-71/-73/-81） | CAN 经典帧 250 kbit/s | - | host/j1939 | req latency min/avg/max = 4/4/4 us (n=1) | PASS 9/0 | logs/20261102-124356-j1939_ecu.log | A |
| `j1939_tp` | J1939（J1939-21 TP） | CAN 经典帧 250 kbit/s | - | host/j1939/ | BAM 500B: 72 pkts, time=3550252 us, 140.8 B/s, gap min/avg/max=49205/49986/50024 us, err=0 timeout=0 | PASS 7/0 | logs/20261030-184807-j1939_tp.log | A |
| `mqtt_telemetry` | MQTT（MQTT 3.1.1（lwIP TCP）） | Ethernet（TCP 1883） | IP: 192.168.2.1, 192.168.2.100/24, 192.168.2.1/24 / 端口: 1883 | host/mqtt | 板→主机 QoS1 1000 条（板端自报） = 1000/1000，114 ms，8771 msg/s（主机侧单次计时 55 ms/18179 msg/s，未归档） | PASS | logs/20261102-162800-mqtt_telemetry.log | C |
| `multiproto_coexist` | 多协议共存（J1939+CANopen+网关） | CAN 多通道（4×500k，两段总线）+ UDP 网关 | 端口: 4000 | host/can_bridge/ | J1939 EEC1（mix（4 通道全开））= 1 500 帧 / gap 9 068/9 999/10 053 µs | PASS 8/0 | logs/20261102-102545-multiproto_coexist.log | B |
| `ptp_master` | PTP（IEEE 1588 E2E） | Ethernet（lwIP） | IP: 192.168.2.1, 192.168.2.100, 192.168.2.100/24 | host/ptp | 判据 = ≤ 1000 ns 且 σ ≤ 500 ns → PASS | PASS | logs/20261030-184820-ptp_master.log | C |
| `ptp_slave` | PTP（IEEE 1588 E2E） | Ethernet（lwIP） | IP: 192.168.2.1/24, 192.168.2.100/24, 192.168.2.100 | host/ptp | 判据 = ≤ 1000 ns 且 σ ≤ 500 ns → PASS | PASS | logs/20261102-101345-ptp_slave.log | C |
| `someip_event` | SOME/IP（SOME/IP-SD + 事件） | Ethernet（lwIP） | IP: 192.168.2.1, 224.244.224.245, 192.168.2.100 / 端口: 30501, 30490 | host/someip | [run] PASS (0 failed) | PASS | logs/20261030-184830-someip_event.log | C |
| `someip_rpc` | SOME/IP（vsomeip 兼容） | Ethernet（lwIP） | IP: 192.168.2.100/24, 192.168.2.1/24, 192.168.2.1 / 端口: 30490, 30501 | host/someip | 板端 run：8/8 PASS（add 正常/回绕、get_version、set_led 回读、温度 get/set、未知方法、REQUEST_NO_RETURN）； | PASS | logs/20261102-121620-someip_rpc.log | C |
| `someip_tp` | SOME/IP（SOME/IP-TP） | Ethernet（lwIP） | IP: 192.168.2.1, 224.244.224.245, 192.168.2.100 / 端口: 30601, 30602, 30490, 30501 | host/someip | [run] 重组 4096 B CRC 0x5D1C4EE3 : PASS | PASS | logs/20261102-121620-someip_tp.log | C |
| `uds_ecu` | UDS（ISO 14229） | CAN 经典帧 500 kbit/s | - | host/uds | 结果 = PASS（步骤=10 03 扩展会话） | PASS | logs/20261030-184841-uds_ecu.log | A |
| `uds_flash` | UDS（ISO 14229 刷写） | CAN 经典帧 500 kbit/s | - | host/uds/ | 实测 = 14664 µs → 2.23 MB/s（阶段=擦除 32 KB） | PASS | logs/20261102-123311-uds_flash.log | A |
| `vss_signals` | VSS（COVESA VSS 信号） | CAN over UDP（cannelloni） | 端口: 4000 | host/vss | [auto] 1.2 s 发送完成: vss: ON rate=100 ms sent=36 tick=12 uptime=1274 ms | - | logs/20261030-184850-vss_signals.log | A |
| `xcp_can_cal` | XCP（ASAM XCP on CAN） | CAN 经典帧 500 kbit/s | - | host/xcp | GET_SEED / 错 key UNLOCK / 正确 key = 4 B 种子 / ERR=0x25 / PASS | PASS | logs/20261030-184854-xcp_can_cal.log | B |
| `xcp_can_daq` | XCP（ASAM XCP on CAN） | CAN 经典帧 500 kbit/s | 端口: 4000 | host/can_bridge | DAQ 总帧率 = 3097 frame/s（500k 总线负载约 37%） | PASS | logs/20261102-155714-xcp_can_daq.log | B |
| `xcp_eth` | XCP（ASAM XCP on UDP（XCPonEth）） | Ethernet（UDP 5555） | IP: 192.168.2.100/24, 192.168.2.1/24, 192.168.2.100 / 端口: 5555 | host/xcp | 载荷吞吐（stress：EVT1 10 kHz/8 ODT/6 s）= 835.7 KB/s = 6.85 Mbit/s | PASS | logs/20261102-155453-xcp_eth.log | C |
| `xrce_pubsub` | XRCE-DDS（Micro-XRCE-DDS） | Ethernet（lwIP） | IP: 192.168.2.100/24, 192.168.2.1/24, 192.168.2.1 / 端口: 2018 | host/xrce | ros2_adc_sub.py --duration 10 = 991 样本 / 9.91 s = 99.89 Hz，抖动 σ=0.49 ms，丢帧 0.10%（1/991） | - | logs/20261102-124454-xrce_pubsub.log | C |
| `xrce_ros2_bridge` | XRCE-DDS（ROS 2 桥接） | Ethernet（lwIP） | IP: 192.168.2.100/24, 192.168.2.1/24, 192.168.2.1 / 端口: 2018 | host/xrce | ros2_adc_sub.py --duration 10 = 994 样本 = 100.01 Hz，抖动 σ=0.29 ms，丢帧 0.00% | PASS | logs/20261030-184924-xrce_ros2_bridge.log | C |

## 2. 覆盖统计

| 协议 | 例程 | README | 有实测日志 | bench A/B/C |
|---|---|---|---|---|
| CAN 网关 | 1 | 1 | 1 | 0/1/0 |
| CAN 记录 | 1 | 1 | 1 | 0/1/0 |
| CANopen | 3 | 3 | 3 | 2/1/0 |
| DLT | 1 | 1 | 1 | 1/0/0 |
| DoIP | 1 | 1 | 1 | 0/0/1 |
| GS-USB | 1 | 1 | 1 | 0/0/1 |
| ISO-TP | 1 | 1 | 1 | 1/0/0 |
| J1939 | 3 | 3 | 3 | 3/0/0 |
| MQTT | 1 | 1 | 1 | 0/0/1 |
| PTP | 2 | 2 | 2 | 0/0/2 |
| SOME/IP | 3 | 3 | 3 | 0/0/3 |
| UDS | 2 | 2 | 2 | 2/0/0 |
| VSS | 1 | 1 | 1 | 1/0/0 |
| XCP | 3 | 3 | 3 | 0/2/1 |
| XRCE-DDS | 2 | 2 | 2 | 0/0/2 |
| gPTP | 1 | 1 | 1 | 0/0/1 |
| 多协议共存 | 1 | 1 | 1 | 0/1/0 |

## 3. 缺失与后续

- 缺 README 的例程：无
- 无实测日志的例程：无
- 指标仍为空的例程（README 无实测表且日志无结论行）：无
- 证据日志取 `logs/` 下该例程最新一条；重跑后重新生成本文件即可刷新。

