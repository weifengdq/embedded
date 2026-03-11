# tc4d7_can

`tc4d7_can` 是从 `tc4d7_uart_printf_echo` 清理后得到的 TC4D7 单通道 CAN echo 示例。工程不再依赖 UART 输入输出，启动后仅完成一件事：初始化板上唯一可用的 CAN 通道，收到任意数据帧后原样回发。

## 板级资源

- CAN01_TX: `P01.3`
- CAN01_RX: `P01.4`
- CAN_STB: `P03.5`

当前实现中：

- 使用 `MODULE_CAN0` 的 `Node1`
- `P03.5` 被配置为普通推挽输出并拉低，用于退出 CAN 收发器 standby
- 默认开启 CAN FD 能力，名义波特率为 `500 kbit/s`，数据相位波特率为 `2 Mbit/s`

如果你的对端只跑 Classic CAN，仍然可以正常收发；如果你需要别的位时序，直接修改 [Cpu0_Main.c](tc4dx/tc4d7_can/Cpu0_Main.c) 顶部的波特率宏即可。

## 软件行为

- `core0_main()` 初始化收发器使能脚和 CAN 节点
- 节点工作在 `transmitAndReceive + fdLongAndFast` 模式
- 标准 ID 和扩展 ID 都配置了“全接收”滤波，统一进入 `Rx FIFO0`
- 主循环轮询 `Rx FIFO0`
- 每收到一帧，就复制数据、ID、DLC、帧模式，并重新写入 `Tx FIFO`

为了便于在线观察，工程保留了几组全局计数器，可以在调试器中直接看：

- `g_canRxCount`
- `g_canTxCount`
- `g_canDropCount`
- `g_canLastMessageId`
- `g_canLastDlc`

## 构建

示例命令：

```powershell
.\build.ps1 -Action rebuild -BuildType Debug
.\build.ps1 -Action build -BuildType Release
.\build.ps1 -Action download -BuildType Release
```

输出文件名已经改为 `tc4d7_can.elf` 和 `tc4d7_can.hex`。

## TC387 与 TC4D7 CAN 差异分析

参考工程是 `tc387/tc387_can_x12`，它的目标是把 TC387 上 12 路逻辑 CAN 通道一次性铺开；本工程则只针对 TC4D7 板上唯一引出的 1 路 CAN。两边最大的差异不在“收发 API”，而在“驱动模型”和“资源组织方式”。

### 1. iLLD 版本和驱动能力已经不是同一代

- TC387 工程使用的是 `iLLD_1_20_0`
- TC4D7 工程使用的是 `iLLD-TC4-v2.4.0`

从驱动头文件就能看出来，TC4 的 CAN 标准层新增了 CRE 相关枚举、结构和接口，而 TC387 的标准层没有这部分内容。对应证据见：

- [IfxCan.h](tc387/tc387_can_x12/Libraries/iLLD/TC3xx/Tricore/Can/Std/IfxCan.h)
- [IfxCan.h](tc4dx/tc4d7_can/Libraries/iLLD/TC4xx/Tricore/Can/Std/IfxCan.h)

TC4 的 `IfxCan_Can_initNode()` 里还显式处理了 AP protection，并在初始化结束时默认关闭 CRE：

- [IfxCan_Can.c](tc4dx/tc4d7_can/Libraries/iLLD/TC4xx/Tricore/Can/Can/IfxCan_Can.c#L1120)

这意味着，TC387 示例里的节点初始化思路不能原封不动搬过来，尤其不能假设两代器件的底层寄存器辅助特性完全一致。

### 2. TC387 示例是“多模块多节点分组”，TC4D7 板卡只有“单板单通道”

TC387 参考工程在 [Cpu0_Main.c](tc387/tc387_can_x12/Cpu0_Main.c) 里人为定义了 `CAN0` 到 `CAN11` 这 12 个逻辑通道，并把它们再映射到多个物理模块和节点上。这个设计是为了覆盖更多接口，不是板级最简移植方案。

而 TC4D7 板子上用户实际可用的引脚只有：

- `P01.3 -> IfxCan_TXD01_P01_3_OUT`
- `P01.4 -> IfxCan_RXD01C_P01_4_IN`

这两个 pinmap 都明确指向 `MODULE_CAN0` 的 `Node1`：

- [IfxCan_PinMap_TC4Dx_BGA436_COM.c](tc4dx/tc4d7_can/Libraries/iLLD/TC4xx/Tricore/_PinMap/TC4Dx/IfxCan_PinMap_TC4Dx_BGA436_COM.c#L81)
- [IfxCan_PinMap_TC4Dx_BGA436_COM.c](tc4dx/tc4d7_can/Libraries/iLLD/TC4xx/Tricore/_PinMap/TC4Dx/IfxCan_PinMap_TC4Dx_BGA436_COM.c#L319)

所以 TC4D7 移植的正确做法不是复制 TC387 的 `CAN_NUM = 12` 和一大串 ISR 宏，而是收缩成 `MODULE_CAN0 + Node1 + 单通道 echo`。

### 3. TC387 示例手工写死 Message RAM 基址，TC4 新驱动默认从模块句柄推导

TC387 参考工程在应用层里直接写了：

- `0xF0200000u + (channel / 4) * 0x10000`
- 再叠加 `(channel % 4) * 0x1000`

这是一种“应用层自行分片 Message RAM”的旧式写法。

但 TC4 驱动的 `IfxCan_Can_initNodeConfig()` 默认配置已经把 `messageRAM.baseAddress` 设成：

- `IfxCan_ramAddress[IfxCan_getIndex(can->can)]`

也就是由当前模块句柄自动推导模块对应的 RAM 基址，应用层只需要给各段填写偏移即可。证据见：

- [IfxCan_Can.c](tc4dx/tc4d7_can/Libraries/iLLD/TC4xx/Tricore/Can/Can/IfxCan_Can.c#L1798)

因此在 TC4D7 工程里，继续照搬 TC387 那种绝对地址写法没有必要，还会把驱动默认模型绕开。当前实现只保留偏移设置：

- 标准滤波表
- 扩展滤波表
- `Rx FIFO0`
- `Tx FIFO`

这更符合 TC4 驱动接口本身的设计。

### 4. TC4 的节点/模块规模比 TC387 参考工程更大，但板上并没有全部引出

从 TC4 的 `IfxCan.h` 可以看到 `IfxCan_DestinationId` 已经扩展到 `Can4_Node3`，说明驱动层支持的模块数量比 TC387 参考工程里实际使用的 `CAN0/1/2` 更大。证据见：

- [IfxCan.h](tc4dx/tc4d7_can/Libraries/iLLD/TC4xx/Tricore/Can/Std/IfxCan.h#L231)

但这只是芯片能力，不等于板级都可用。对这块 TC4D7 板来说，真正应该围绕的是已引出的 `CAN01_TX/RX`，而不是试图把芯片所有 CAN 节点都一次性做成示例。

### 5. TC387 示例偏向“中断+回调矩阵”，TC4D7 当前实现改成“轮询单节点”

TC387 参考工程在应用层做了三套中断宏：

- `CAN_RX_ISR(...)`
- `CAN_TX_ISR(...)`
- `CAN_BUSOFF_ISR(...)`

再加通道数组和回调函数表，适合做多路压测，但对单通道板级 bring-up 来说太重。

本次 TC4D7 版本 deliberately 采用：

- 单节点配置
- `Rx FIFO0` 轮询
- 收到即回发

原因很直接：

- 板上只有 1 路 CAN
- 工程从 UART echo 复制而来，首先要完成的是“功能去耦”和“最小可验证移植”
- 单通道轮询更容易先确认位时序、收发器使能脚、滤波和 Message RAM 都正确

如果后续你要扩展成中断版本，可以直接在当前节点配置上打开 `rxf0n` / `traco` / `boff` 三组中断，不需要再回到 TC387 那个 12 路模板。

## 当前移植结论

这次移植的关键不是把 `tc387_can_x12` 逐行改成能编译，而是把其中真正有用的部分提炼出来：

- 保留 `IfxCan_Can` 这套高层发送/接收接口
- 保留“收到后回发”的 echo 行为
- 去掉 TC387 特有的多逻辑通道包装
- 改为匹配 TC4D7 板级资源的 `MODULE_CAN0/Node1/P01.3/P01.4/P03.5`
- 使用 TC4 新版驱动推荐的模块句柄 + Message RAM 偏移配置方式

结果就是当前这个 `tc4d7_can`：它是一个适合在 TC4D7 板上先验证收发链路是否打通的最小 CAN echo 工程，而不是 TC387 12 路示例的机械拷贝。