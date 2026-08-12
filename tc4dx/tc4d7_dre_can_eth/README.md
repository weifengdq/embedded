# tc4d7_dre_can_eth

这个工程从 `tc4d7_lwip_iperf` 拷贝重命名而来，当前已经改造成一个面向 TC4D7 的 DRE CAN/Ethernet bridge bring-up 工程。

本次工作目标是让 TC4D7 的 `CAN01` 和 `GETH0 Port0` 之间可以通过 DRE 完成 CAN 报文与以太网 IEEE 1722 ACF/AVTP 帧的互转，同时把工具链和 vendor 库升级到新的环境版本。

## 当前状态

- 构建目标已从 `tc4d7_lwip_iperf` 改为 `tc4d7_dre_can_eth`。
- `build.ps1` 默认工具链路径已切换到 `C:/Infineon/AURIX-Studio-1.10.36/tools/Compilers/tricore-gcc11/bin`。
- `build.ps1` 默认下载工具路径已切换到 `C:/Infineon/AURIX-Studio-1.10.36/tools/AurixFlasherSoftwareTool_v3.0.18/AURIXFlasher.exe`。
- 工程内 `Libraries/IfxLldVersion.h`、`Libraries/iLLD`、`Libraries/Infra`、`Libraries/Service` 已替换为 `tc4dx/ref/illd_release_tc4x-main/src/Libraries` 中的 `iLLD-TC4-v2.6.0`。
- 旧的 lwIP/iperf 业务路径已从主流程中移除，当前主流程进入 DRE bridge。

## 实现概览

### 1. CAN -> Ethernet

当前实现不是“纯硬件 CAN 自动入 DRE”，而是下面这条较稳妥的首版路径：

1. CPU 轮询 `CAN01 (MODULE_CAN0 / Node1)` 的 `Rx FIFO0`。
2. 收到 CAN/CAN FD 报文后，软件把报文内容填入 CRE `RHBUF0` 兼容格式。
3. 软件调用 `IfxCan_Can_triggerDebugMessageToDre()` 对 `RHBUF0` 做 `SWTRIG`。
4. DRE 根据 `RHBUF0.UCRH.DID = IfxCan_DestinationId_Ethernet1`，把报文打包成 Ethernet ACF/AVTP 帧并送往 `EOBUF0`。
5. `EOBUF0` 通过 `GETH0 DMA Channel 0` 发出。

这条路径里，真正的 CAN -> Ethernet 封装仍由 DRE 完成；CPU 只负责把 CAN Rx FIFO 中的数据灌入 CRE host buffer 并触发 DRE。

### 2. Ethernet -> CAN

当前实现采用 DRE 直接解析与转发：

1. `GETH0 DMA Channel 0` 接收 RMII + DP83825I 上来的以太网帧。
2. DRE `RETHDL0 + EIBUF0` 关联到该 Rx descriptor list。
3. `Stream Filter 0` 当前配置为“全接收”范围匹配。
4. `RT0 element 0` 当前配置为“全接收 CAN ID -> 单播到 CAN0_Node1”。
5. DRE 解析 ACF 中的 CAN 报文后，直接转发到 `CAN01`。

## 关键配置

### CAN 侧

- 物理通道：`CAN01`
- 节点：`MODULE_CAN0 / Node1`
- 引脚：
  - `TX = P01.3`
  - `RX = P01.4`
  - `STB = P03.5`
- 位时序：
  - 仲裁段：`500 kbit/s @ 80%`
  - 数据段：`2 Mbit/s @ 80%`
- 当前接收策略：标准帧和扩展帧都全接收，统一进入 `Rx FIFO0`

### Ethernet 侧

- 接口：`GETH0 Port0`
- 物理层：`RMII + DP83825I`
- DMA：`Tx Channel 0` / `Rx Channel 0`
- MAC 地址：优先从板上 EEPROM 读取；失败则回退到本地管理地址 `02:00:5E:4D:70:01`
- 当前 `EOBUF0` 发包头配置：
  - 目标 MAC：`FF:FF:FF:FF:FF:FF`
  - 源 MAC：板卡 MAC
  - EtherType：`0x22F0`
  - `triggerMode = frameCount`
  - `triggerFillLevel = 1`

### DRE 侧

- `Stream Filter 0`：当前配置为 64-bit Stream ID 全范围接收
- `RT0 element 0`：当前配置为全 CAN ID 接收并转发到 `IfxCan_DestinationId_Can0_Node1`
- `EIBUF0`：`ntscfStartAddress = 14`，按未打 VLAN 的以太网头偏移处理 NTSCF
- `EOBUF0`：用于 DRE 打包后输出到 Ethernet1

## 工程结构变化

### 新增模块

- `DreCanEthBridge.c`
- `DreCanEthBridge.h`

这个模块接管了：

- CAN01 初始化
- GETH0/PHY 初始化
- DRE 初始化
- 主循环轮询
- DRE 状态清理

### 构建系统调整

为了绕过 Windows 下 TriCore `ld.exe` 对临时 `@response-file` 的处理问题，当前 `CMakeLists.txt` 额外做了两项调整：

1. 旧的 `lwIP/iperf` 业务源码不再编译进目标。
2. 最终链接改为：
   - 启动相关对象直接参与链接
   - 其他源文件先打进 `libtc4d7_dre_can_eth_support.a`

这样可以显著减少最终链接命令上的对象数量，避免 `ld.exe: cannot find @C:\WINDOWS\TEMP\ccXXXXXX` 这一类 response-file 兼容问题。

## 构建与下载

### Debug 重建

```powershell
.\build.ps1 -Action rebuild -BuildType Debug
```

### Release 重建

```powershell
.\build.ps1 -Action rebuild -BuildType Release
```

### 下载

```powershell
.\build.ps1 -Action download -BuildType Debug
```

### 一步完成构建和下载

```powershell
.\build.ps1 -Action all -BuildType Debug
```

## 本次留痕

### 2026-08-07

- 把工程主目标从 `lwIP iperf` 切换为 `DRE CAN/Ethernet bridge`
- 新增 `DreCanEthBridge` 模块
- 把默认工具链/下载工具路径升级到 `AURIX-Studio-1.10.36`
- 把工程内 iLLD/Infra/Service 升级到 `iLLD-TC4-v2.6.0`
- 处理了 Windows + TriCore GCC 链接 response-file 问题
- 重新完成 Debug 构建
- 首次实机下载确认本机实际 flasher 版本目录为 `AurixFlasherSoftwareTool_v3.0.18`
- 首次串口启动日志保存在 `tc4dx/ref/log/tc4d7_dre_can_eth_serial_boot_20260807_161013.log`
- 根据启动日志把 bridge 中硬编码的 PHY 地址从 `1` 修正为板级宏 `BOARD_GETH0_P0_PHYADR (= 0)`
- 二次实机下载与串口回归通过，日志保存在 `tc4dx/ref/log/tc4d7_dre_can_eth_serial_boot_20260807_161143.log`
- 二次启动日志确认 `ETH link up: 100M full duplex.`，说明 GETH0 + DP83825I bring-up 已恢复
- 在 `0x22F0` 抓包为空的前提下，增加一次性 `GETH TX` 原始探针帧（EtherType `0x88B5`）用于区分 `GETH TX` 与 `DRE TX` 故障段
- 三次串口日志保存在 `tc4dx/ref/log/tc4d7_dre_can_eth_serial_boot_20260807_162747.log`，已确认启动阶段执行了 `ETH probe frame sent: eth.type=0x88B5, len=33.`
- PC 侧抓包摘要保存在 `tc4dx/ref/log/tc4d7_dre_can_eth_can_to_eth_20260807_161447.txt`、`tc4dx/ref/log/tc4d7_dre_can_eth_ethsrc_20260807_161534.txt`、`tc4dx/ref/log/tc4d7_dre_can_eth_probe_20260807_162834.txt`
- 当前证据显示：板上软件路径已经执行到 `GETH TX probe send`，但 PC 侧仍未观测到任何 `eth.src = 44:B7:D0:ED:AE:B9` 或 `eth.type = 0x88B5/0x22F0` 的出站帧
- 后续通过固定 NPF 设备路径重做抓包，确认 `if5 = \Device\NPF_{F5B81553-F1D6-43D2-AA8E-4CC5364F1375} = 以太网`，并稳定抓到原始探针 `0x88B5`
- 诊断日志 `tc4dx/ref/log/tc4d7_dre_can_eth_serial_can_diag_20260810_103041.log` 显示：`canRx` 和 `dreTrig` 已增长，但 `txReq0/txCnt` 仍为 0，说明问题已收敛到 `RHBUF SWTRIG -> DRE/EOBUF` 这一路
- 基于该结论，当前把 `CRE.CONFIG.DEN` 对应的 `enableDestinationRouting` 从 `FALSE` 改为 `TRUE`，作为下一轮最小可证伪修正
- 进一步排查时发现初始化里遗漏了 `CAD` 映射；当前已补上 `IfxDre_Dre_setCanAddressDatabaseElement()`，把 `CAN0_Node1` 关联到其 CRE 起始地址，作为下一轮验证的关键修正
- 补上 `CAD` 后，串口诊断 `tc4dx/ref/log/tc4d7_dre_can_eth_serial_can_diag_20260810_103556.log` 已显示 `txReq0=1`，说明 `RHBUF SWTRIG -> DRE/EOBUF` 已经成立
- 基于该结果，当前移除轮询里对 `EOBUF0.STATUS.TXREQ` 的主动清除，避免把 DRE 的待发请求过早清掉
- 在移除 `TXREQ` 主动清除后，`0x22F0` 仍未出线，因此继续把下一跳聚焦到 `TETHDL0.CTRL.TRIG`；当前把 `txEthConfig.triggerType` 从 `FALSE` 改为 `TRUE` 做最小验证
- 在 `TETHDL0.CTRL.TRIG` 切换后 `0x22F0` 仍未出现，当前进一步显式启用 `RETHDL0/TETHDL0` 的 `descriptorPointerConfigEnable` 并把指针初始化到 `0`，避免依赖 reset 默认值
- 在固定抓包和多轮诊断后，继续去掉 `frameCount` 自动触发假设：当前把 `EOBUF0` 切到 `IfxDre_TriggerMode_software`，并在每次 `RHBUF SWTRIG` 后显式调用 `IfxDre_Dre_setSoftwareTrigger()`
- 在 `software trigger` 仍未让 `0x22F0` 出线后，当前继续增加 `EOBUF0.STATUS/ERROR` 与 `TETHDL0.CTRL` 的串口诊断，进一步锁定 `DRE -> TETHDL` 这一段
- 当前再收缩一个关键参数：把 `EOBUF0.payloadLength` 从 `1484` 下调到 `64`，避免小 CAN 帧验证时 DRE 长时间停留在超大 ACF 载荷目标上
- 最新串口诊断 `tc4dx/ref/log/tc4d7_dre_can_eth_serial_can_diag_20260810_104412.log` 显示 `eobuf0_error=0` 但 `eobuf0_status` 出现 `TTL`，说明问题更像是 `TETHDL` 触发方式不匹配，而不是 descriptor 本身错误
- 基于该结果，当前保留 `software trigger`，但把 `txEthConfig.triggerType` 从 `TRUE` 改回 `FALSE` 做对照验证
- 对照验证后，`TTL` 消失，但 `txReq0=1` 与 `txCnt=0` 仍并存，说明 DRE 已经形成待发请求但没有真正下发到线缆
- 由于原始 `0x88B5` 探针与 DRE 共用同一个 `GETH Tx DMA channel 0 / descriptor ring`，当前先移除探针，避免 CPU 直发流量继续干扰 DRE 的 Tx ring 占用
- 基于本地用户手册抽取结果，确认 `software trigger` 模式下若 `TXRDY` 在缓冲为空时被置位，会触发 `TTL`；因此当前把 `IfxDre_Dre_setSoftwareTrigger()` 从 `RHBUF SWTRIG` 之后立即调用，改成 `EOBUF0.ACFL > 0` 后再触发
- 当前进一步显式调用 `IfxGeth_configureAccessToGeth()`，用全开放 APU 配置放开 GETH 的 Global/MAC/Channel ACCEN，验证 DRE 作为外部 master 写 GETH Tx tail pointer 时是否存在访问限制
- 串口诊断同时新增 `ME_ERR` 和 `GETH DMA CH0 tail/current descriptor` 观测，便于直接判断 DRE 是否成功推进了 GETH Tx ring
- 最新 map 已确认原 GETH descriptor 和 Tx/Rx buffer 全部位于 CPU0 DSPR 全局地址 `0x7000...`；本轮将 descriptor 和 buffer 迁移到 `lmuram_nc` (`0xB040...`) 的 `.lmubss_nc` 段，并为 LMU memory APU 配置全开放访问，排除 DRE/GETH 外部 master 访问 DSPR 的限制
- LMU 版本实测：DRE 已能改写 GETH Tx tail pointer（由 `0xB040A1A0` 变为 `0xF903B150`），但该值不是合法的 LMU descriptor 地址，`txCnt` 仍为 0；问题已缩小为 DRE 生成/读取 Tx descriptor 或 tail pointer 内容格式异常。本轮新增首个 Tx descriptor 四个 DWORD、descriptor 地址和 buffer 地址日志
- 用户手册进一步确认：`TETHDLi_CTRL.TRIG=0` 时，DRE 在 DRE RAM 内准备 4 个 Tx descriptor，并把 GETH tail pointer 更新为 DRE descriptor list 的地址。实测 DRE 在第一帧后写入 tail `0xF903B150`，且手册规定 tail 为 descriptor base 加 `0x10`，因此 Tx descriptor base 修正为 `0xF903B140`；此前误用 `0xF903D140` 导致 GETH 启动后无串口 banner
- `0xF903B140` 直接作为 GETH DMA descriptor base 的实验会在 GETH 初始化阶段卡住，已回退到可正常启动的 LMU Tx/Rx ring；该地址不能简单视为 CPU 可预配置的 DRE RAM 映射，后续需按 DRE RAM 实际窗口/访问方式继续确认
- 手册精确检索确认 DRE RAM 起始地址为 `0xF9038000`（实测 tail `0xF903B150` = `0xF9038000 + 0x3140 + 0x10`）。修正方案改为：先用 LMU ring 完成 iLLD GETH 初始化，再仅在初始化完成后把硬件 Tx descriptor base/tail 寄存器切换到 DRE TETHDL0 list `0xF903B140`，避免 CPU 侧初始化访问 DRE RAM 窗口
- 最新实测已确认硬件 ring 对接成功：切换后 `tail=curdesc=0xF903B150`，不再出现 LMU/DRE descriptor 指针不一致；但 `txCnt` 仍为 0。本轮新增读取 DRE TETHDL0 四个 descriptor DWORD 和 GETH MAC Tx 状态，继续定位 OWN/长度/Tx 错误
- 最新 descriptor 诊断：DRE `w0=0xF903AB42`（EOBUF0 物理地址）、`w2=0x32`（50 字节）、`w3=0xB0000032`（OWN/FD/LD 已置位）；GETH `STATUS=0xC4` 仅有 TBU/RBU，无 FBE。根据手册的 APU-PETH 访问矩阵，已补调用 `IfxDre_initAp()` 初始化 DRE Ethernet Message RAM APU，开放 GETH master 对 EOBUF/descriptor 区的访问
- APU-PETH 修正已实测生效：`txCnt=1`、`txReq0=0`，DRE descriptor `w3` 从 `0xB0000032` 变为 `0x30000032`，说明 GETH DMA 已读取并回写 descriptor；固定 NPF 接口抓包仍未看到 `0x22F0`，下一步检查 MAC Tx enable/speed/duplex 和硬件 Tx counters
- 最终 MAC 级验证已完成：稳定运行后发送 CAN FD，串口显示 `canRx=1`、`dreTrig=1`、`txCnt=1`、`txReq0=0`、`mac_txpkts=1`、`mac_txoct=64`、`mac_err=0`；这证明 DRE 已生成 descriptor、GETH DMA 已读取并回写、MAC 已接受 64 字节 Tx 帧
- 最终固定接口抓包文件 `ref/log/tc4d7_dre_can_eth_can2eth_final_20260812_104717.pcapng` 大小仅 472 字节且没有 `eth.type == 0x22f0` 帧。因此当前结论是：芯片内部 DRE→GETH DMA→MAC Tx 路径已打通，但 PC 端 `以太网` NPF 抓包仍未观察到线上帧；剩余问题位于 RMII/PHY 物理发送、网卡接收可见性或抓包路径，不能宣称 PC 端已完成收包

## 验证记录

已完成：

- `.\build.ps1 -Action rebuild -BuildType Debug`
- `.\build.ps1 -Action download -BuildType Debug`
- `COM130` 启动串口回归，已确认 banner、EEPROM MAC、链路建立日志
- `COM130` 已确认启动阶段执行一次 `GETH TX` 原始探针发包
- `gs_usb_x can0` 已按 `500K/2M CAN FD+BRS` 发送测试报文
- `tshark` 已在候选有线网卡上完成多轮抓包
- 成功生成：
  - `build/tc4d7_dre_can_eth.elf`
  - `build/tc4d7_dre_can_eth.hex`
  - `build/tc4d7_dre_can_eth.map`

尚未完成：

- 确认 PC 实际连接的是哪一个 `I350` 端口，以及当前 `tshark` 抓包接口是否就是与 TC4D7 相连的物理口
- 确认 `GETH TX` 原始探针帧为何未在 PC 侧出现
- 在 `GETH TX` 被 PC 侧观测到后，再继续确认 `0x22F0` ACF/AVTP 帧内容
- PC 侧主动构造 Ethernet -> CAN 的 ACF 注入测试

## 建议的上板验证步骤

1. 下载 `Debug` 版本到板卡。
2. 打开 `COM130`，观察启动日志是否出现：
   - DRE bridge banner
   - EEPROM MAC 读取结果
   - PHY link up/down 日志
3. 用 `gs_usb_x can0` 以 `500K/2M` 发送 CAN FD 报文到 `CAN01`。
4. 在 PC 侧用 Wireshark 抓包，过滤：

```text
eth.type == 0x22f0
```

5. 反向验证时，从 PC 构造符合 IEEE 1722 ACF 的以太网帧，确认 `CAN01` 能发出对应报文。

## 当前限制

- 当前首版的 `CAN -> Ethernet` 路径使用的是“CPU 轮询 CAN + CRE RHBUF SWTRIG + DRE 封装”，不是完全纯硬件 CRE 自动路由。
- `Ethernet -> CAN` 依赖输入帧符合 DRE 可解析的 ACF/AVTP 格式；本次没有额外附带 PC 侧注入脚本。
- 目前还没有加入 runtime 统计打印或更细的 DRE 错误寄存器诊断日志。