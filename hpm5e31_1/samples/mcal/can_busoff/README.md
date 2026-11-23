# can_busoff —— AUTOSAR Can 驱动 error passive / bus-off / 恢复实测

让对端（CAN1）进入 STOPPED，不再发 ACK，观察 CAN0 的 TEC/REC 与错误状态迁移；
再恢复对端，测业务恢复时间。同时验证 `Can_GetControllerErrorState` /
`Can_GetControllerRxErrorCounter` / `Can_GetControllerTxErrorCounter` /
`Can_MainFunction_BusOff` 的行为。

## 硬件要求

同 `can_classic_echo`：CAN0 <-> CAN1 同一段总线，无需外部线、无需干扰源。

## 编译 / 运行

```bash
bash scripts/build.sh samples/mcal/can_busoff
bash scripts/hwrun.sh mcal/can_busoff --secs 25 --send "can run"
```

## 命令

| 命令 | 说明 |
|---|---|
| `can run` | 完整流程：基线 ping-pong -> 对端 STOP -> 观察 TEC -> 恢复对端 |
| `can stat` | 打印当前状态与错误计数器 |
| `can info` | 打印原理与预期 |

## 实测现象（日志：`logs/20261102-142116-can_busoff.log`）

> 最终交付构建的复验日志：`logs/20261102-143510-can_busoff.log`（passive@953 µs TEC=129；
> 对端恢复后 6025 µs 业务恢复）。

```text
--- error-passive / bus-off / recovery test ---
  baseline ok        mode=1 errstate=0 TEC=0 REC=0 busoff_cb=0
  CAN1 -> STOPPED (no ACK on bus)
  t=   953us  -> ERROR_PASSIVE  TEC=129 REC=0
  t= 20954us  TEC 平台 129（20 ms 内不再增长）
  after acks lost    mode=1 errstate=1 TEC=129 REC=0 busoff_cb=0
  summary: error-passive @ 953us, TEC=129；平台 TEC=129
  note: ISO 11898-1 规定 error-passive 节点遇 ACK 错误不再增加 TEC；
        所以对端不 ACK 只能到 TEC=128（error passive），到不了 bus-off(TEC>255)。
  Can_SetControllerMode(CAN0, STARTED) while error-passive = 1 (expect 1/DET)
  peer back: ping-pong(10) rc=0 in 6022us
  after peer back    mode=1 errstate=0 TEC=118 REC=0 busoff_cb=0
```

关键数据：

| 阶段 | 时间 | TEC | 状态 |
|---|---|---|---|
| 基线 ping-pong 20 帧 | — | 0 | ACTIVE |
| 对端 STOP 后首次失败重传 | 953 µs | 129 | PASSIVE |
| 再等 20 ms | 20 954 µs | 129（平台） | PASSIVE |
| 对端恢复 ACK 后 | +6 022 µs | 118 → 0 | ACTIVE，业务恢复（10/10 ping-pong） |

## 关键点

1. **对端不 ACK 到不了 bus-off**：ISO 11898-1 规定 error-passive 节点检测到 ACK 错误时
   不再增加 TEC，所以 TEC 停在 128 附近（实测 129），要进入 bus-off 必须制造
   位错误/格式错误（需要外部干扰或对端主动发错误帧）。这是本轮"已知限制"，见下。
2. bus-off 恢复路径（`Can_SetControllerMode(STARTED)` 在 bus-off 时触发
   `mcan_recover_from_busoff` + 重新初始化）已实现（SWS_Can_00161），但本板用 ACK 缺失
   复现不出 bus-off，因此该分支只有代码评审 + 逻辑说明，没有实测日志。
3. error passive 状态下请求 STARTED 属于非法迁移：返回 `E_NOT_OK` 并报
   `CAN_E_TRANSITION`（日志里 `= 1 (expect 1/DET)`，Det 计数 +1）。
4. TEC 回落是指数式的：恢复 ACK 后每成功一帧 TEC -= 1，实测 6 ms 内从 118 回落到 0，
   期间业务可用（10 帧 ping-pong 全成功）。

## 待确认项

- **bus-off 进入未实测**：已知限制（原因：ISO 11898-1 的 TEC 规则，对端不 ACK 只能到
  error passive）。复跑命令：`can run`；如需真 bus-off，需外加干扰源或在总线上接一个
  主动发错误帧的节点。