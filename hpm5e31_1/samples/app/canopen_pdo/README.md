# canopen_pdo —— SYNC 周期 PDO 实测（延迟 / 抖动 / 丢帧）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

CAN0 从站（节点 1）+ CAN1 主站。主站每 10 ms 发 **SYNC**，从站按 TPDO1（同步型，transmission type=1）
回传映射的 ADC16 采样（0x2101，PF26/ADC0_IN01）与 PDO 计数器（0x2102）；主站用 mcycle 时基统计
**端到端延迟 / 周期抖动 / 丢帧**。TPDO2（事件型 1000 ms）与 RPDO1 控制回环一并验证。

## 接线

同 `canopen_node`：CAN0 ↔ CAN1 板内同一总线，无需外部接线。ADC 输入 PF26 悬空时为噪声底，
可在排针上接 0~3.3 V 验证映射数据变化。

## 命令

```bash
bash scripts/hwrun.sh app/canopen_pdo --secs 22 --lock-wait 1800            # 上电自动 10 ms x 5 s
# 或串口 shell：
#   run [period_ms] [secs]   默认 10 5；例如 run 5 3
#   stat / pdo
```

## 实测（logs/20261030-174138-canopen_pdo.log；bsp_uptime_ms 修复后重测）

```
  SYNC 帧数               500
  TPDO1 帧数              500
  丢帧                    0 (0.00 %)
  端到端延迟 min/avg/p99/max  318 / 341 / 375 / 379 us
  TPDO 周期 min/avg/max   10000 / 10013 / 10043 us
  周期抖动 sigma / maxdev  7.962 / 40 us
```

- 500 个 SYNC → 500 个 TPDO1，**0 丢帧**；总时长 5.006885 s（理论 5.000 s，偏差 0.14%）；
- 平均延迟 **341 µs**（p99 375 µs，max 379 µs），主要是"主站发 SYNC → 从站轮询 → 发送 → 主站轮询"的往返；
- 周期抖动 σ **8.0 µs**、最大偏差 40 µs（10 ms 周期的 0.40%），来源是主站忙等循环与 1 kHz 应用任务；
- ⚠️ TPDO2（事件定时器 1000 ms）在 5 s 窗口内 0 帧，已登记为待确认 U-33-10（不影响 TPDO1 同步路径）；
- 从站侧 ADC16 每 10 ms 采样一次，TPDO1 每帧 6 B（0x2101 u16 + 0x2102 u32）。

## 坑

1. **0x1019=0 时 SYNC 必须 DLC=0**：带计数字节的 SYNC 会被 CANopenNode 判为长度错误
   （`CO_SYNC.receiveError=0x41`）而完全丢弃，表现为"TPDO 一帧都不发"——本移植第一版就踩了这个坑；
2. **TPDO1 的 transmission type 写 1 才是"每个 SYNC 发一次"**；写 0 表示"仅同步且事件发生"，
   应用不改数据就永远不发；
3. **主站忙等期间必须持续驱动从站**（`co_master_set_service`），否则同一 MCU 上从站被饿死，
   现象是 SYNC 发出但 TPDO 永不返回、SDO 全超时。
