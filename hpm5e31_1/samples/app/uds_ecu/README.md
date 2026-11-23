# uds_ecu —— 完整 UDS ECU（0x7E0/0x7E8）+ 板内测试仪

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 1. 硬件与角色

| 角色 | CAN 通道 | 收 ID | 发 ID |
|---|---|---|---|
| ECU（DUT） | CAN0 | 0x7E0 | 0x7E8 |
| 测试仪 | CAN1 | 0x7E8 | 0x7E0 |

无需外接线：CAN0 与 CAN1 在板上经 TCAN1044AV 接到同一段 120 Ω 总线。

## 2. 覆盖的服务

0x10（01/02/03）、0x11（01/02/03）、0x14、0x19（01/02/04/06）、0x22、0x27（一级/二级）、
0x28、0x2E、0x2F、0x31（01/02/03，RID 0x0203 自检、0x0301 慢例程）、0x3E、0x85，
以及 34/36/37 之外的全部常用服务；P2=50 ms / P2*=5000 ms / S3=5000 ms；NRC 完整。

## 3. 命令

| 命令 | 作用 |
|---|---|
| `run` | 13 步服务序列（10 03 → 27 → 22 → 2E → 31 → 19 → 14 → 3E → 10 01），逐条 PASS/FAIL + RTT |
| `pend` | 0x31 01 0301（150 ms 慢例程）验证 NRC 0x78 与 P2* 续期 |
| `s3` | 10 03 后静默 5.5 s，验证 S3 超时回默认会话 |
| `sec` | 3 次错误 key → 0x35/0x35/0x36 → 锁定 10 s → 0x37 |
| `dtc` | 打印 DTC 内存（状态位/快照） |
| `stat` / `info` | 服务端统计 / 配置 |

上电 1.5 s 后自动跑 `info; run; pend; s3; sec; dtc; stat`，因此不加 `--send` 也有完整证据（约 9 s）。

## 4. 实测（2026-10-30，经典 500 kbit/s）

日志：`logs/20261030-172347-uds_ecu.log`

| 步骤 | 响应 | RTT µs | 结果 |
|---|---|---|---|
| 10 03 扩展会话 | `50 03 00 32 01 F4` | 565 | PASS |
| 22 F190/F195/F187/F18C | 62 B | 3085 | PASS |
| 2E F190 未解锁 | `7F 2E 33` | 1175 | PASS |
| 27 01 取种子 | seed=0xD2E3FB68 | 479 | PASS |
| 27 02 发密钥 | key=0x2B589DBE → `67 02` | 473 | PASS |
| 2E F190 已解锁 | `6E F1 90` | 1171 | PASS |
| 22 F190 回读 | VIN=HPM5E31NEWVIN0002 | 1166 | PASS |
| 31 01 0203 自检 | `71 01 02 03 00 01` | 498 | PASS |
| 19 02 读 DTC | 15 B（3 条 DTC） | 1190 | PASS |
| 14 FFFFFF 清码 | `54` | 473 | PASS |
| 19 02 清码后 | `59 02 09` | 468 | PASS |
| 3E 00 | `7E 00` | 468 | PASS |
| 10 01 默认会话 | `50 01 00 32 01 F4` | 480 | PASS |

`[uds_ecu] PASS steps=13/13 pass=13 fail=0`，avg RTT ≈ 0.97 ms。

专项：0x78 pending=1（慢例程 150.7 ms）；S3 超时后 2E 返回 NRC 0x7F；安全访问 0x35/0x35/0x36 后 0x37；
DTC 内存 3 条，status=0x5F/0x58/0x54。

## 5. 主机侧联调

`@bash
bash scripts/hwrun.sh app/uds_ecu --secs 20 --send "run" \
     --post "timeout 60 python3 host/uds/uds_tester.py --channel vcan0" --lock-wait 1800
`@

（需要板端同时跑 CAN 网关；网关会引入约 2.3 ms 往返延迟，见第 28 章。）

## 6. 坑

1. DTC 内存必须在 `app_uds_init()` 之前装填，init 不清表（第一版清表导致 19 02 读到 0 条）。
2. 测试仪侧要按"PDU 队列"收响应：慢服务会先回 0x78 再回最终响应，两条可能在同一轮 pump 里到达，
   用单缓冲会丢掉 0x78（本示例 `s_cli_done` 改成队列后 pending 才统计正确）。
3. 安全访问每次发 key 前都要重新取种子，否则 NRC 0x24。
