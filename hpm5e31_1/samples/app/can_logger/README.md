# can_logger —— CAN 帧记录 / candump 导出 / 回放顺序校验

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

CAN0 记录、CAN1 注入：512 帧 RAM 环形缓冲（每帧 1 µs 时间戳 + 8 字节数据），
按 **candump 文本格式**从串口导出，供 `host/canlog/candump_parse.py` 统计与顺序校验。

## 接线 / 硬件

| 通道 | 引脚 | 角色 |
|---|---|---|
| CAN0 | PD00/PD01（TCAN1044） | 记录通道（接收所有标准帧） |
| CAN1 | PD05/PD04 | 回放/生成通道（周期发 0x300 计数帧，或按 `replay` 按序发） |

两通道板内同一段 CAN 总线（2×120 Ω），无需外部接线。

## 命令

```bash
bash scripts/hwrun.sh app/can_logger --secs 12 \
     --send "stat;replay 20 300;log dump 20;verify" --lock-wait 1800
```

| 命令 | 作用 |
|---|---|
| `log start\|stop\|clear\|stat` | 记录开关 / 清空 / 状态 |
| `log dump [n]` | 导出最近 n 帧（0=全部）为 candump 文本（`# candump:` … `# end candump`） |
| `gen on [period_ms]\|off` | CAN1 周期发 0x300 计数帧（seq 在 data[0..3] 小端） |
| `replay [n] [gap_us]` | CAN1 按 seq 0..n-1 逐帧回放（默认 20 帧 / 200 µs 间隔），末尾自动 `verify` |
| `verify` | 校验已记录的 0x300 帧 seq 严格 +1（顺序 PASS/FAIL） |
| `stat` | 计数/丢弃/运行时间 |

## 实测

日志 `logs/20261030-175332-can_logger.log`（CAN0 记录 + CAN1 回放 + candump 导出）：

```
replay: sent=30 ok=30 fail=0 gap=300 us
verify: 0x300 frames=30 bad_order=0 first_bad_idx=-1 result=PASS
stat: log=RUN count=30 dropped=0 gen_on=0 gen_seq=67 uptime=1506 ms
# candump: 30 frames (start=0) epoch_base=1793000000
(1793000000.777817) can0 300#00000000A55AC33C
(1793000000.778115) can0 300#01000000A55AC33C
(1793000000.778417) can0 300#02000000A55AC33C
...
```

主机解析（`host/canlog/candump_parse.py /tmp/cd4.txt --expected-seq 30`）：

```
帧数           : 30（坏行 0）
时间跨度       : 0.009 s
数据字节       : 240 B
ID 直方图      : 0x300:30
帧间隔         : avg=0.302 ms max=0.308 ms
seq 帧         : id=0x300 收到 30/30, 顺序错误 0, 丢帧 0
=== RESULT: PASS ===
```

| 指标 | 数值 |
|---|---|
| 记录帧数 / 容量 / 覆盖丢弃 | 30 / 512 / 0 |
| 0x300 生成速率 | 100 帧/s（gen 10 ms，680 ms 内 68 帧） |
| 回放发送成功率 | 30/30（gap 300 µs）、20/20（gap 200 µs） |
| seq 顺序校验（固件） | **bad_order=0 → PASS**（0..29 严格 +1） |
| candump 导出 | 30 行合法文本，主机解析 **0 坏行、0 顺序错误、0 丢帧 → PASS** |
| RAM 占用 | 512 × 32 B = 16 KB（DLM 128 KB 的 12.5%） |

## Bench B：canplayer 回放 → 网关 → CAN0 → verify（U-38-1）

板端 \`gw on\` 后 CAN1 交给 \`app_can_gw\`（CAN1 ↔ UDP 4000 ↔ 主机 vcan0），CAN0 继续记录；
\`gw on\` 会同时停掉 CAN1 周期生成器并清空环形缓冲，保证回放校验只有一个 0x300 序列源。

\`\`\`bash
bash scripts/hwrun.sh app/can_logger --secs 10 --send "gw on" \
     --post "timeout 80 bash host/canlog/replay_check.sh /tmp/replay.dump" --lock-wait 1800
\`\`\`

主机侧 \`host/canlog/replay_check.sh\`：vcan0 up（去限速）→ 起 cannelloni → \`canplayer -I /tmp/replay.dump\`
（30 帧 0x300，seq 0..29）→ 串口发 \`log dump 40;verify\` 校验顺序。

> **实测（2026-11-02 板卡窗口）**：canplayer 回放 30 帧 0x300（seq 0..29）→ vcan0 → cannelloni → CAN1 → CAN0，
> 板端 `verify: frames=30 bad_order=0 result=PASS`，日志 `logs/20261102-112054-host-canplayer-replay.log`。
> 注意：上电自检**不要**再自动发 0x300（会污染校验基线），本版本已移除自检 replay。

## 坑

1. **RAM 预算**：512 帧 × 32 B = 16 KB，DLM 128 KB；若同时要 lwIP 网关（72%），把 `LOG_CAP` 降到 128；
2. **MCAN 消息 RAM 必须放 AHB SRAM**：两个通道各一份 `ATTR_PLACE_AT(".ahb_sram")` 缓冲，
   漏掉会 `mcan_init` 失败；
3. **时间戳语义**：`log dump` 的 epoch 基准固定为 2026-10-30 00:00:00，只用于相对顺序/间隔分析；
4. **串口带宽**：921600 下 1000 帧 candump 约 45 KB，`--secs` 要给足（>3 s），否则日志被截断。
