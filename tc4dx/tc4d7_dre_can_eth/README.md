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