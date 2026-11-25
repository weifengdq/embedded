# can_classic_echo —— AUTOSAR Can 驱动经典帧双节点回环（CAN0 <-> CAN1）

用 MCAL 的 `Can_Init` / `Can_SetControllerMode` / `Can_Write` / `Can_MainFunction_*` 在板上做
经典 CAN 2.0 500 kbit/s 的 ping-pong（CAN0 发 0x123，CAN1 回 0x321）与单向吞吐测试。

## 硬件要求

| 资源 | 引脚 | 说明 |
|---|---|---|
| CAN0 TX/RX | PD00 / PD01 | 板上 MCAN0，接 TCAN1044AV |
| CAN1 TX/RX | PD05 / PD04 | 板上 MCAN1，与 CAN0 同一段总线 |
| 终端电阻 | 板上 120 Ω × 2 | CAN0/CAN1 两端各一个，已焊接 |
| 收发器 STB | PE03 | 低电平=正常工作，由 `Can_PBcfg.c` 的 `TransceiverEnable` 回调拉低 |
| USR 串口 | /dev/ttyACM0 @921600 8N1 | letter-shell 交互 |

不需要外部 CAN 分析仪、不需要杜邦线。

## 编译 / 运行

```bash
cd hpm5e31_1
bash scripts/build.sh samples/mcal/can_classic_echo
bash scripts/hwrun.sh mcal/can_classic_echo --secs 30 --send "can echo 200;can echo 200;can stream 500;can stat;can err"
```

## 命令

| 命令 | 说明 |
|---|---|
| `can info` | 打印接线、位时序、HTH 映射、过滤器说明 |
| `can echo [n]` | 经典帧 ping-pong n 次（默认 200） |
| `can stream [n]` | CAN0 单向连续发 n 帧，CAN1 计数（默认 500） |
| `can stat` | 打印两路控制器状态/错误计数器/收发计数 |
| `can err` | 非法参数打点，验证 8 个 DET 错误码 |
| `can mode <c> [start\|stop]` | 读/写控制器状态机 |
| `can ver` | 打印 `Can_GetVersionInfo` 结果 |
| `can diag` | dump MCAN0/1 的 CCCR/PSR/IR/TXBC/TXBTO，排查用 |

## 实测现象（日志：`logs/20261102-142350-can_classic_echo.log`）

> 最终交付构建（含 bus-off 恢复与位时序表扩展）的复验日志：`logs/20261102-142930-can_classic_echo.log`
> （ECHO ok=200/200、STREAM 300/300 @4203 frame/s；与下面引用的日志同量级，差异 <0.2%）。

```text
[boot] === can_classic_echo: AUTOSAR Can driver, classic 500k, CAN0 <-> CAN1 ===
[init] Can_Init done, controllers=4 hth=16 hrh=8 filters=36
  [mode] ctrl=0 -> 1
  [mode] ctrl=1 -> 1
[start] CAN0/CAN1 STARTED, st=0
ECHO classic 500k: n=200 ok=200 lost_req=0 lost_echo=0 bad=0 total=95493us
  rx_total=400 rx_overflow=0 tx_confirmed=400 busoff=0
ECHO classic 500k: n=200 ok=200 lost_req=0 lost_echo=0 bad=0 total=95481us
  rx_total=400 rx_overflow=0 tx_confirmed=400 busoff=0
STREAM classic 500k: sent=500 rx=500 bad=0 total=118757us => 4210 frame/s
  payload=0.27 Mbit/s (8B/frame, 108 bit on wire)  bus=0.5 Mbit/s  wire_eff=53.9%
CAN0: mode=1 errstate=0 REC=0 TEC=0
CAN1: mode=1 errstate=0 REC=0 TEC=0
DET: errors before=8 after=16 (delta=8)
DET: last mod=80 inst=0 api=0x06 err=0x03
```

关键数据：200 帧 ping-pong 两轮 **ok=200/200**，TEC/REC 全程 0；往返延迟约 477 µs
（500 kbit/s 下一帧 8B 标准帧占用约 230 µs，两次单程 + 主函数轮询）；单向吞吐
**4210 frame/s**（理论 500000/114 ≈ 4386 frame/s，达成率 96%）。

## 关键点

1. **STB 必须先拉低**：`Can_PBcfg.c` 里 `TransceiverEnable` 回调调用
   `board_can_transceivers_enable()`，不调用则整条总线收发器处于 standby，TX 会无限重传。
2. **CCCR.INIT 必须轮询确认**：清 INIT 是 AHB posted write，写完立刻读可能还是 1。
   驱动用 `can_wait_cccr_init()` 循环确认（对应 SWS_Can_00262）。
3. **TX 完成判定不能写 TXBTO**：M_CAN 的 TXBTO 只读，位清除只能靠下一次 TXBAR 写 1 自动完成。
   驱动用 `TXBRP==0 && TXBTO==1` 判定完成并回调 `CanIf_TxConfirmation`。
4. **HTH 语义**：一个 HTH = 一个专用 TX buffer，同一 HTH 未确认前再写返回 `CAN_BUSY`
   （SWS_Can_00213），例程的 `can_send` 在 BUSY 时调用 `Can_MainFunction_Write` 后再重试。
5. **跨用例要清硬件 FIFO**：`rxq_drain_all()` 先调 8 次 `Can_MainFunction_Read` 把残留帧抽干，
   否则上一轮的尾帧会串进下一轮的期望序列（这是本次调试里耗时最多的易错点）。
6. 本构建用**轮询模型**（`Can_Cfg.h` 的 `CAN_RX_PROCESSING_INTERRUPT=0`、
   `CAN_TX_CONFIRM_IN_INTERRUPT=0`），收发推进由 `Can_MainFunction_Read/Write` 完成；
   中断模型代码同源，限制见第 46 章"待确认项与风险"。

## 与 SDK 直调例程的区别

`samples/can/mcan_two_node` 直接调 `mcan_transmit_blocking` / `mcan_read_rxfifo`，发送是阻塞式；
本例走 AUTOSAR 语义的 HTH 句柄 + `CAN_BUSY` 反馈 + 主函数确认，上层（CanIf/Com）语义完整，
代价是多一次 `Can_Write` 重试与主函数轮询开销（4.6% CPU，见 can_irq_perf 实测）。

## 待确认项

- 无未决项；中断模型限制见 `docs/chapters/47-mcal-can.md` §待确认项与风险。