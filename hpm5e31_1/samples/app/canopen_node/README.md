# canopen_node —— CANopen（CiA 301）从站最小闭环

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

CAN0 上跑 CANopenNode v4.0 从站（节点 1，500 kbit/s，心跳 1 s），CAN1 上跑
`components/app_canopen` 的最小主站做 **NMT / SDO / 心跳 / EMCY** 全流程自测。
单块 hpm5e31_1 即可闭环（Bench A，不需要上位机和网关）。

## 接线

| 信号 | 引脚 | 说明 |
|---|---|---|
| CAN0_H/L | PD00/PD01（TCAN1044） | DUT，节点 1 |
| CAN1_H/L | PD05/PD04（TCAN1044） | 测试主站 |
| 串口 | CH343 /dev/ttyACM0 921600 | letter-shell |

CAN0 与 CAN1 在板上已并到同一段总线，两端各有 120 Ω 终端，**无需外部接线**。

## 命令

```bash
cd hpm5e31_1
bash scripts/hwrun.sh app/canopen_node --secs 12 --lock-wait 1800
```

串口 shell：`run`（重跑自测）、`stat`、`nmt <start|stop|preop|reset|resetapp>`、`emcy`、`mon`。

## 实测（logs/20261030-173143-canopen_node.log）

```
=== CANopen node1 self test (CAN0 DUT <- CAN1 master) ===
[test] NMT 状态切换
  NMT stop                             PASS  state=4 (期望 4=Stopped)
  NMT start                            PASS  state=5 (期望 5=Operational)
  NMT pre-operational                  PASS  state=127 (期望 127=Pre-op)
[test] SDO expedited 读写
  SDO read 0x1000 deviceType           PASS  0x1000=0x00000191 (期望 0x00000191)
  SDO read 0x1017 heartbeat            PASS  0x1017=1000 ms (期望 1000)
  SDO write 0x1017 = 200               PASS  写 200 -> 读回 200
  SDO write 0x1017 restore 1000        PASS
  SDO read 0x1018:1 vendor-ID          PASS  vendorID=0x0000A531
  SDO segmented upload 0x1008          PASS  len=32 "HPM5E31 CANopen node 1"
[test] 心跳（生产者 1 s）
  heartbeat 0x701 @1s                  PASS  2.6 s 内收到 2 帧（期望 2~3）
[test] EMCY 注入
  EMCY 0x081 manufacturer              PASS  EMCY 计数 +1, code=0x1000
  stats: heap=2876B rx=0 tx=0 retry=0 loops=538898 maxloop=2945us canerr=0x0000
=== RESULT: 11 PASS / 0 FAIL ===
```

- CANopenNode 对象堆分配 **2876 B**；OD 静态 RAM 约 **6.2 KB**；镜像 Flash 增量约 **26 KB**；
- `maxloop=2945 us` 出现在上电初始化阶段，稳态 `co_node_poll()` 开销远小于 1 ms（见第 32 章）。

## Bench B（上位机 vcan0 + cannelloni 网关）

`gw on` 把 CAN1 交给 `app_can_gw`（CAN1 ↔ UDP 4000 ↔ 主机 vcan0），CAN0 仍是节点 1：
CAN0/CAN1 板内同一段总线，因此主机 vcan0 看到的就是节点 1 的真实帧。

```bash
bash scripts/hwrun.sh app/canopen_node --secs 12 --send "gw on" \
     --post "timeout 70 bash host/canopen/run_all.sh" --lock-wait 1800
```

实测（logs/20261030-174516-canopen_node.log）：`co_node_test.py` **14 PASS / 0 FAIL**；
`co_pdo_bench.py` 延迟 avg 2.30 ms / p99 2.75 ms、丢包 0.2%（cannelloni UDP 固有）；
`co_sdo_seg.py` 在 UDP 网关下会因丢包超时（见第 32 章 U-33-11）。
DLM 占用 72.61%，Flash 158.7 KB。

## 易错点

1. **MCAN 消息 RAM 必须在 AHB SRAM**：漏掉 `mcan_set_msg_buf_attr()` 时 `mcan_init()` 直接失败，
   表现为 `co_node_init()` 返回负数、总线上没有任何帧；
2. **没开 CANopen 时单节点收不到 ACK**：只有一个节点时发帧会因无 ACK 逐渐 bus-off，必须两个节点都上电；
3. **NMT Reset Application 会重启 MCU**：串口会重新打印 banner，属于预期；
4. **心跳计数要在 `co_master_poll()` 里统计**（本移植已放在 `co_master_t.hb_frames`），
   若在主循环 `pop` 时计数，SDO 等待期间的心跳会漏计。
