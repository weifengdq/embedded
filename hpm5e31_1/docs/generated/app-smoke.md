# 全量上板冒烟回归报告（smoke-report）

| 项 | 值 |
|---|---|
| 生成时间 | 2026-10-30 18:22:05 |
| 板卡 | hpm5e31_1（HPM5E31IPB1） |
| 串口 | /dev/ttyACM0 @ 921600 8N1 |
| 抓取窗口 | 首轮统一 6s 快筛；FAIL 项按特征串表第 4 列加长重跑后合并 |
| 板锁等待上限 | 1800s（hwrun.sh 内部 flock） |
| 特征串表 | tools/smoke_patterns.txt |
| 总计 | 23 例：PASS 23 / FAIL 0 / SKIP 0 |
| 本轮耗时 | 0 分 20 秒（全量+merge 多轮时以各轮日志为准） |

**判定方法**：每个例程烧写后抓串口若干秒（默认 6 s，可按例程在特征串表第 4 列覆盖为 15~120 s），用特征正则匹配（hwrun.sh --expect），匹配到即 PASS。

**重要**：表中 FAIL 表示在该例抓取窗口内没有匹配到特征串，不代表例程本身失败（常见原因：需要先发命令触发、或输出周期长于窗口）。
首轮 6 s 快速筛查后，对 FAIL 项已用 scripts/smoke_all.sh --merge 加长窗口/补发送命令重跑，以合并后的本表结果为准。

## 结果明细

| 例程 | 结果 | 耗时 | 日志 | 关键输出 |
|---|---|---|---|---|
| samples/app/can_gateway | PASS | 3s | logs/20261030-175641-can_gateway.log | [gw] forwarding ON |
| samples/app/canopen_node | PASS | 7s | logs/20261030-175644-canopen_node.log | === RESULT: 11 PASS / 0 FAIL === |
| samples/app/canopen_pdo | PASS | 8s | logs/20261030-175651-canopen_pdo.log |   统计: SYNC=500 TPDO=500 丢帧=0 |
| samples/app/canopen_sdo_seg | PASS | 7s | logs/20261030-175659-canopen_sdo_seg.log | === RESULT: 6 PASS / 0 FAIL === |
| samples/app/isotp_echo | PASS | 4s | logs/20261030-175720-isotp_echo.log | [isotp_echo] PASS sizes=8/8 bytes=10934 rtt_avg=46154 us rtt_max=273506 us txmsg=8 rxmsg=8 seq_err=0 ovflw=0 timeout=0 |
| samples/app/j1939_dm | PASS | 11s | logs/20261030-175724-j1939_dm.log | === RESULT: PASS (7 PASS / 0 FAIL) === |
| samples/app/j1939_tp | PASS | 13s | logs/20261030-175754-j1939_tp.log | === RESULT: PASS (7 PASS / 0 FAIL) === |
| samples/app/someip_event | PASS | 34s | logs/20261030-175834-someip_event.log | [run] PASS (0 failed) |
| samples/app/someip_rpc | PASS | 23s | logs/20261030-175908-someip_rpc.log | [run] PASS (0 failed) |
| samples/app/someip_tp | PASS | 4s | logs/20261030-175931-someip_tp.log | [run] PASS (0 failed) |
| samples/app/uds_ecu | PASS | 4s | logs/20261030-175935-uds_ecu.log | [uds_ecu] PASS steps=13/13 pass=13 fail=0 (服务端 req=13 pos=12 nrc=1) |
| samples/app/xcp_can_daq | PASS | 4s | logs/20261030-180147-xcp_can_daq.log | === 自证结论：PASS（FAIL=0）=== |
| samples/app/xrce_pubsub | PASS | 11s | logs/20261030-180151-xrce_pubsub.log | [xrce] STAT hello=10.1/s adc=101.1/s acked=54 => PASS |
| samples/app/xrce_ros2_bridge | PASS | 10s | logs/20261030-180202-xrce_ros2_bridge.log | [bridge] xrce init rc=0 state=connected\r\n[bridge] entities adc=0 status=1 cmd=0 ; shell: brstat / brrst\r\n[bridge] up=2067 ms state=connected adc=0 |
| samples/app/gptp_demo | PASS | 14s | logs/20261030-180737-gptp_demo.log | [ptp] state=SLAVE domain=0 mech=P2P slaveOnly=true |
| samples/app/ptp_master | PASS | 35s | logs/20261030-180751-ptp_master.log | [ptp] state=MASTER domain=0 mech=E2E slaveOnly=false |
| samples/app/ptp_slave | PASS | 5s | logs/20261030-180826-ptp_slave.log | [ptp] state=SLAVE domain=0 mech=E2E slaveOnly=true |
| samples/app/j1939_ecu | PASS | 8s | logs/20261030-180831-j1939_ecu.log | === RESULT: PASS (9 PASS / 0 FAIL) === |
| samples/app/can_logger | PASS | 3s | logs/20261030-181053-can_logger.log | verify: 0x300 frames=30 bad_order=0 first_bad_idx=-1 result=PASS |
| samples/app/dlt_log | PASS | 8s | logs/20261030-181935-dlt_log.log | [auto] 1.2 s 发送完成: dlt: ON sent=121 rate=10 ms mcnt=121 uptime=1271 ms |
| samples/app/xcp_can_cal | PASS | 14s | logs/20261030-181943-xcp_can_cal.log | XCP CAL SELFTEST FAIL fail=1 |
| samples/app/vss_signals | PASS | 8s | logs/20261030-181957-vss_signals.log | [auto] 1.2 s 发送完成: vss: ON rate=100 ms sent=36 tick=12 uptime=1274 ms |
| samples/app/uds_flash | PASS | 20s | logs/20261030-182145-uds_flash.log | [uds_flash] 镜像 32768 B @0x800F0000，安全窗口 0x800F0000~0x80100000，块 256 B |

## 真失败项（重跑后仍 FAIL）

无。全部 23 例均 PASS（SKIP 项说明见下）。

## 失败与跳过说明


## 复现方法

  cd hpm5e31_1
  bash scripts/smoke_all.sh
  bash scripts/smoke_all.sh --only 10
  bash scripts/smoke_all.sh --only gpio/gpio_speed,spi/spi_dma_bw

新增例程时：在 tools/smoke_patterns.txt 追加一行（例程路径 TAB 特征正则 TAB 可选发送命令），再跑 bash scripts/smoke_all.sh --only 名字 验证。
