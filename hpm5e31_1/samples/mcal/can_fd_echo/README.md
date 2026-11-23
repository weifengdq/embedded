# can_fd_echo —— AUTOSAR Can 驱动 CAN FD 长帧回环（CAN0 <-> CAN1）

验证 CAN FD：仲裁段 500 kbit/s / 1 Mbit/s，数据段 1 / 2 / 5 Mbit/s（BRS + TDC），载荷 8~64 字节；
给出仲裁 × 数据段组合、采样点、TDC 偏移的通过矩阵与吞吐。

> 2026-11-02 更新：**5 Mbit/s 数据段已修通**。R5 记录的"0/50、TEC 248、bus-off"不是收发器/线束
> 能力问题，而是位时序配置越界：5 Mbit/s 数据段一位只有 16 tq（80 MHz、BRP=1、12.5 ns/tq），
> TDC 的 SSP 偏移 `TDCR.TDCO` 旧值 20 超出本位，发送端在数据段必然判位错误。
> 改成 12（16 tq 的 75%）后全部通过；驱动同时加了"TDCO/TDCF 必须小于数据段位长"的配置期校验。
> 详见第 47 章表 47-17/47-18 与第 09 章 §6。

## 硬件要求

同 `can_classic_echo`：CAN0(PD00/PD01) <-> CAN1(PD05/PD04)，TCAN1044AV，两端 120 Ω，无需外部线。
收发器 STB 由 PE03 统一控制（低 = 正常），例程启动时已通过 `board_can_transceivers_enable()` 使能。

## 编译 / 运行

```bash
bash scripts/build.sh samples/mcal/can_fd_echo
bash scripts/hwrun.sh mcal/can_fd_echo --secs 45 --lock-wait 1800 \
  --send "can at 2000 3;can stream 3000 3;can sizes 100 3;can stat"
```

## 命令

| 命令 | 说明 |
|---|---|
| `can info` | 速率配置摘要与 CPU 主频 |
| `can at <n> <id>` | 指定 baudrate 配置 ID 做 64 B ping-pong（n 轮） |
| `can matrix [n]` | 跑完整矩阵：2M/1M/5M 各采样点、TDCO=1/8/10/12/13/14/20、1M 仲裁 + 5M 数据 |
| `can sizes [n] [id]` | 8/12/16/20/24/32/48/64 字节各 n 次 ping-pong，输出延迟表 |
| `can stream [n] [id]` | 64 B 单向流水（FD 吞吐），可指定 baudrate 配置 |
| `can stat` | 状态/错误计数器/收发计数 |
| `can err` | 非法参数 DET 打点 |

baudrate 配置 ID（`mcal/Can/cfg/Can_PBcfg.c`）：0 经典 500k、1 经典 1M、2 FD 500k/2M（TDCO 15）、
3 FD 500k/5M（TDCO 12，默认）、4 FD 500k/1M（无 TDC）、5 FD 1M/2M、6~12 5M 采样点/TDCO 扫描、
13 FD 1M/5M、14/15 TDCO=1（测 TDCV）、16/17 2M 采样点、18 TDCO=20（非法，验证驱动拒绝）。

## 实测数据（2026-11-02 修复后，烧写运行）

日志：`logs/20261102-155424-can_fd_echo.log`（矩阵）、
`logs/20261102-155624-can_fd_echo.log`（长测/吞吐/DLC）。

```text
--- FD 500k/5M SP75 TDCO=12 (默认配置,已修正) (baud cfg ID3) ---
  [regs] CAN0 CCCR=0x00000300 NBTP=0x06008A13 DBTP=0x00800A31 TDCR=0x00000C0C PSR=0x0016300F ECR=0x00000000
         LEC=7 TDCV=22 mtq (PSR[22:16]), DLEC=0
  64B pingpong: ok=500/500 avg=369us max=2153us | CAN0 errstate=0 REC=0 TEC=0
FD stream 64B: sent=3000 rx_driver=3000 rx_app=3000 bad=0 total=550242us => 5452 frame/s
  payload=2.79 Mbit/s (uid=64 B), 仲裁 500 kbit/s / 数据 5000 kbit/s
CAN0: mode=1 errstate=0 REC=0 TEC=0
CAN1: mode=1 errstate=0 REC=0 TEC=0
```

通过矩阵（64 B ping-pong ×50，avg = 一次 A→B→A 往返）：

| 仲裁段 | 数据段 | 采样点 / TDC | 结果 | avg |
|---|---|---|---|---|
| 500 kbit/s | 1 Mbit/s | 75%，无 TDC | 50/50 | 846 us |
| 500 kbit/s | 2 Mbit/s | 75%，TDCO=15 | 50/50 | 704 us |
| 500 kbit/s | 2 Mbit/s | 80%，TDCO=16 | 50/50 | 705 us |
| 500 kbit/s | 2 Mbit/s | 85%，TDCO=17 | 50/50 | 705 us |
| 500 kbit/s | 5 Mbit/s | 75%，TDCO=8/10/12/13/14 | 各 50/50 | 364 us |
| 500 kbit/s | 5 Mbit/s | 80%，TDCO=12 | 50/50 | 364 us |
| 500 kbit/s | 5 Mbit/s | 87.5%，TDCO=13 | 50/50 | 365 us |
| 1 Mbit/s | 5 Mbit/s | 75%，TDCO=12 | 50/50 | 312 us |
| 500 kbit/s | 5 Mbit/s | 75%，TDCO=1 | 0/50，TEC=248 bus-off | — |
| 500 kbit/s | 5 Mbit/s | 75%，TDCO=20（旧） | 0/50，TEC=248 bus-off | — |

5 Mbit/s 各 DLC（每档 100 轮）：8/12/16/20/24/32/48/64 B 全部 100/100，
往返延迟 166/180/194/210/224/252/308/364 us。

吞吐对比：5M 5452 帧/s（2.79 Mbit/s 载荷）> 1M 仲裁+5M 数据 6360 帧/s（3.26 Mbit/s）
> 2M 2820 帧/s（1.44 Mbit/s）> 1M 2347 帧/s（1.20 Mbit/s）。

## 关键点

1. FD 帧的 ID 必须带帧类型位：`CAN_ID_FD_STD_FRAME | 0x123`（`0x40000123`），
   否则 `Can_Write` 会按经典帧发送（SWS §7.13）。
2. DLC 9~15 对应 12/16/20/24/32/48/64 字节，驱动按字节长度自动映射；
   经典帧长度 >8 会被 DET 拦掉（`CAN_E_PARAM_DATA_LENGTH`）。
3. **TDC 偏移必须小于数据段位长**：SSP 位置 = `TDCR.TDCO`（单位 = 数据段 mtq），
   5 Mbit/s 一位 16 tq，旧值 20 越界 → 数据段判位错误 → TEC 累加到 248 → bus-off。
   默认配置取 12（=75% 采样点）；实测 8~14 均可，1/20 失败。`PSR.TDCV` 恒等于 `TDCO+10`。
4. 数据段 prescaler ≤2 是开 TDC 时 M_CAN 的硬性约束（SDK 驱动会校验）。
5. 切换位时序必须先进 STOPPED：`Can_SetBaudrate` 在 STARTED 状态返回 E_NOT_OK +
   DET `CAN_E_TRANSITION`（SWS_Can_00256）；`cfg_switch()` 先查当前状态再 STOP（已经是 STOPPED
   就不要再发一次，否则也算非法迁移）。

## 复跑与判定

```bash
# 5M 默认配置长测 + 吞吐
bash scripts/hwrun.sh mcal/can_fd_echo --secs 45 --lock-wait 1800 \
  --send "can at 2000 3;can stream 3000 3;can stat"
# 完整矩阵（含失败对照与非法配置被拒）
bash scripts/hwrun.sh mcal/can_fd_echo --secs 45 --lock-wait 1800 --send "can matrix 50"
```

判定：`can at` 行必须是 `ok=2000/2000 ... errstate=0 REC=0 TEC=0`；`can stat` 的 `busoff` 不应增长。

## 待确认项

- 仍无外部 CAN 分析仪/第二块板：5M 结论限于"板内两节点 + 短总线（<0.3 m）+ 两端 120 Ω"。
- 8 Mbit/s 数据段超 TCAN1044AV 规格（5 Mbit/s 上限），未做眼图，不做量产承诺。
