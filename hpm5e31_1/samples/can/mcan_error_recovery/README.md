# mcan_error_recovery — 错误帧 / bus-off / 恢复

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

把 CAN1 设成 **listen-only**（只收不发、不应答 ACK、不发错误帧），CAN0 正常发送 -> 因无 ACK
不断重传 -> TEC +8/次 -> 错误被动（TEC≥128）-> bus-off（TEC>255）；随后恢复 CAN1 为正常模式、
调用 `mcan_recover_from_busoff()`，验证恢复后能正常收发。用于学习 PSR/ECR 解读与 bus-off 处理。

## 硬件要求 / 接线

- 板内 CAN0/CAN1 同一段真实总线（TCAN1044AV，两端 120 Ω），串口 921600。
- 单节点（总线上只有一个活动节点）即可复现"无 ACK"。

## 编译与运行

    bash scripts/hwrun.sh can/mcan_error_recovery --secs 10 --send "info;run"

## 实测现象（串口输出）

完整日志 `logs/20261030-120042-mcan_error_recovery.log`，关键输出：

    阶段1: CAN0@500k TEC=111 REC=0 LEC=0
           [tx] submit st=0 idx=16
           CAN0(busoff) TEC=135 REC=0 LEC=3 BO=0 EP=1
           RESULT: 未在 200ms 内进入 bus-off (FAIL)
    阶段2: recover st=0 recovered=1, took=0 us
    阶段3: sent=1 got=1 id=0x123 dlc=8 data0=0xDEADBEEF => PASS

结论（详见第 09 章 7 节）：

1. 单节点无 ACK 时 bus-off **不是瞬间**：错误主动阶段每次重传 >4 ms，200 ms 只让 TEC 从 111 涨到 135；
   要做 bus-off 压力测试请把窗口放到 ≥500 ms，并每轮彻底复位节点；
2. 用"对端波特率错配"复现 bus-off 反而更难：错配节点会主动发错误帧，双方 TEC/REC 此消彼长；
3. 恢复成功判据是 **PSR.BO=0 且能正常收发**，不是 TEC 归零（实测恢复后 TEC 仍为 118）。

## 命令

| 命令 | 作用 |
|---|---|
| `info` | 打印三阶段说明 |
| `run` | 跑完整错误注入/恢复流程 |

## 关键点 / 易错点

1. `mcan_recover_from_busoff()` 返回 0 不代表"是我们恢复的"：若调用前总线已静默足够长时间
   （129x11 个隐性位），硬件可能已自动退出 bus-off，耗时显示 0 µs。
2. listen-only 模式下节点不产生 ACK、不发错误帧，是制造"干净无 ACK"场景的正确手段。
3. 错误计数寄存器：`ECR.TEC/REC`、`PSR.LEC`（ACK 错误=3）、`PSR.BO/EP/EW`，
   驱动 API 为 `mcan_get_error_counter()` / `mcan_get_protocol_status()`。
