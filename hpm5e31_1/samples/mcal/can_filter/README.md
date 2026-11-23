# can_filter —— AUTOSAR Can 驱动硬件过滤器验证（CAN0 发 / CAN1 收）

覆盖 M_CAN 标准/扩展过滤器列表的四种类型：掩码（classic）、范围（range）、双 ID（dual）、
拒绝（reject），外加全局过滤（未命中帧丢弃），共 13 个用例，逐条核对命中与落哪个 RX FIFO。

## 硬件要求

同 `can_classic_echo`：CAN0 <-> CAN1 同一段总线，无需外部线。

## 编译 / 运行

```bash
bash scripts/build.sh samples/mcal/can_filter
bash scripts/hwrun.sh mcal/can_filter --secs 25 --send "can run;can stat"
```

## 命令

| 命令 | 说明 |
|---|---|
| `can run` | 跑 13 个过滤器用例并打印判定表 |
| `can stat` | 状态/错误计数器/收发计数 |
| `can info` | 打印 HRH 与 FIFO 的对应关系 |

## 过滤器表（post-build，`mcal/Can/cfg/Can_PBcfg.c`）

| 控制器 | 列表 | 元素 | 类型 | 参数 | 落点 |
|---|---|---|---|---|---|
| 4 × MCAN | STD | #0 | 掩码 | 0x123 / 0x7FF | RXFIFO0 |
| 4 × MCAN | STD | #1 | 掩码 | 0x321 / 0x7FF | RXFIFO0 |
| 4 × MCAN | STD | #2 | 范围 | 0x400 .. 0x40F | RXFIFO0 |
| 4 × MCAN | STD | #3 | 双 ID | {0x500, 0x501} | RXFIFO1 |
| 4 × MCAN | STD | #4 | 掩码 | 0x600 / 0x7F0 | RXFIFO0 |
| 4 × MCAN | STD | #5 | 范围 | 0x700 .. 0x7FF | 拒绝 |
| 4 × MCAN | EXT | #0 | 范围 | 0x18000000 .. 0x18FFFFFF | RXFIFO0 |
| 4 × MCAN | EXT | #1 | 掩码 | 0x1ABC0000 / 0x1FFF0000 | RXFIFO0 |
| 4 × MCAN | EXT | #2 | 双 ID | {0x1FFFFFF0, 0x1FFFFFF1} | RXFIFO1 |

全局过滤：未命中标准帧/扩展帧一律丢弃（GFC ANFS=ANFE=2）。

## 实测现象（日志：`logs/20261102-141905-can_filter.log`）

> 最终交付构建的复验日志：`logs/20261102-143308-can_filter.log`（13/13 PASS）。

```text
filter test: 13 cases (expect hit/drop vs actual)
  #  can_id      case                      expect    got       verdict
   0 0x00000123  mask 0x123/0x7FF          FIFO0     FIFO0     PASS
   2 0x00000405  range 0x400..0x40F        FIFO0     FIFO0     PASS
   3 0x0000060A  mask 0x600/0x7F0          FIFO0     FIFO0     PASS
   4 0x00000500  dual {0x500,0x501}        FIFO1     FIFO1     PASS
   6 0x00000502  no match -> drop          drop      drop      PASS
   7 0x00000705  reject 0x700..0x7FF       drop      drop      PASS
   9 0x98FF0001  ext range 0x18FFFFFF      FIFO0     FIFO0     PASS
  10 0x9ABC1234  ext mask 0x1FFF0000       FIFO0     FIFO0     PASS
  11 0x9FFFFFF0  ext dual {..F0,..F1}      FIFO1     FIFO1     PASS
  12 0x92345678  ext no match -> drop      drop      drop      PASS
filter: pass=13 fail=0  (rx_total=18 rx_overflow=0 tx_confirmed=26)
```

**13/13 PASS**。判定依据是 `CanIf_RxIndication` 的 `Mailbox->Hoh`：PBcfg 里
HRH[2]={ctrl1,FIFO0}、HRH[3]={ctrl1,FIFO1}，从 Hoh 反查落点，而不只是"收到就算过"。

## 关键点

1. M_CAN 过滤器元素布局由 SDK 的 `mcan_set_filter_element` 编码（S0/S1 的 SFEC/SFT 位域），
   MCAL 只负责把 `Can_FilterConfigType` 表翻译成 `mcan_filter_elem_t`，元素个数必须与
   `ram_config.std_filter_elem_count` / `ext_filter_elem_count` 匹配（否则 `mcan_config_all_filters`
   返回 `status_invalid_argument`，`Can_Init` 会报 `CAN_E_INIT_FAILED`）。
2. 过滤器是 **post-build 常量**，运行期没有改过滤器的 AUTOSAR API（SWS 里只有
   `Can_SetCanPnFrameDataMask`，与 ID 过滤无关）；改过滤规则要改 `Can_PBcfg.c` 重新编译。
3. 未命中帧由 GFC 直接丢弃，**不会**进 FIFO，也不会触发 `CanIf_RxIndication`；
   用"收到帧数 + 超时"两侧判据避免假阳性（例程每例先 `rxq_drain_all()`）。
4. 本例顺带验证了 RXFIFO0 与 RXFIFO1 的独立路由：双 ID 元素命中后从 RXFIFO1 出，Hoh=3。

## 待确认项

- 无未决项。