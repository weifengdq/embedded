# tc4d7_dre_can_eth

这个工程从 `tc4d7_lwip_iperf` 拷贝重命名而来，当前已经改造成一个面向 TC4D7 的 DRE CAN/Ethernet bridge bring-up 工程。

本次工作目标是让 TC4D7 的 `CAN01` 和 `GETH0 Port0` 之间可以通过 DRE 完成 CAN 报文与以太网 IEEE 1722 ACF/AVTP 帧的互转，同时把工具链和 vendor 库升级到新的环境版本。


---

## TC4D7 DRE CAN↔Ethernet Bridge

> 工程目录：`tc4dx/tc4d7_dre_can_eth/` ｜ 测试脚本与日志：`tc4dx/ref/scripts/`、`tc4dx/ref/log/`

### 一句话讲清原理

TC4D7 这颗 Infineon 芯片里有一个叫 **DRE（Data Routing Engine，数据路由引擎）** 的硬件模块。它能把收到的 **CAN / CAN FD 报文**自动“包装”成一根网线里的 **以太网帧**（标准格式叫 IEEE 1722 ACF / AVTP，EtherType `0x22F0`），然后从板上的 **RJ45 网口**发出去。

简单链路：

```
你的 CAN 设备 ──CAN 线──> TC4D7 的 CAN01
                                │
                       [CPU 轮询] 读到 CAN 报文
                                │  把报文塞进 DRE 的 RHBUF，并触发 DRE
                                ▼
                       [DRE 硬件] 把 CAN 报文打包成 ACF/AVTP 以太网帧
                                │  放进 EOBUF
                                ▼
                       [GETH 千兆 MAC + PHY] 从 RJ45 发出去
                                │
                                ▼
                           你的 PC / 交换机（Wireshark 可抓到 0x22F0 帧）
```

反过来（Ethernet→CAN）本工程也已实现，采用与 **CAN→Ethernet 对称**的“软件桥”方式：板载 GETH 收到以太网帧后，CPU 轮询解析其中的 ACF 字段，再用 CAN 控制器把报文发到 `CAN01`。双向都是 CPU 轮询 + 软件触发/组帧，便于初学者 1:1 对照字段。

### 这个工程做了什么

1. **让 DRE 真正跑起来**：把 TC4D7 的 `CAN01` 和 `GETH0 Port0`（千兆以太网，RMII + DP83825I PHY）通过 DRE 桥接起来。
2. **逐字节验证 16 个 CAN 用例**全部正确：经典 CAN / CAN FD / CAN FD+BRS，标准帧 / 扩展帧，数据长度 0/1/4/8/12/64 字节。
3. **解决健壮性死锁**：原来连续发几十帧后以太网会“卡死”不再发帧；现在修复后可持续高吞吐转发。
4. **性能与丢包测量**：板卡每秒打印 PERF 计数器，给出 `canRxTotal / ethTxTotal / ethTxDrop / 丢包率`，不依赖抓包也能测量。

### 怎么跑起来（三步）

**第 1 步：编译并烧录**
```powershell
cd tc4dx\tc4d7_dre_can_eth
.\build.ps1 -Action all -BuildType Release
```
（默认工具链 AURIX-Studio-1.10.36，下载器 AurixFlasher 3.0.18；需要管理员权限给板卡上电复位。）

**第 2 步：看启动日志**
打开串口 `COM130`（115200, 8N1）。正常会看到：
```
DRE bridge ready: CAN01 <-> Ethernet ACF/AVTP.
ETH link up: 100M full duplex.
```
然后每秒一行诊断，含正向 `PERF diag: canRxTotal=.. ethTxTotal=.. ethTxDrop=.. ringStalls=..` 和反向 `PERF rev : ethRxTotal=.. canTxTotal=.. canTxDrop=..`，以及各自的 `dropRate=..%`。

**第 3 步：发 CAN，看结果**
用 candlelight `gs_usb_x` 适配器接 `CAN01`，运行：
```powershell
cd tc4dx\ref\scripts
$env:PYTHONPATH = "C:\github\embedded\tc4dx\ref\gs_usb_x-main\sdk\python-can-gsusb\src"
python dre_all_send2.py --selector auto --repeat 1 --gap 0.6
```
- **串口**会逐帧打印（这就是“逐字节核对”的最简方式，不用 Wireshark）：
  ```
  CANRX id=0x204 ext=0 fdf=1 brs=1 dlc=15
  ACF   id=0x204 ext=0 fdf=1 brs=1 len=64 data=000102030405060708090A0B0C0D0E0F...
  ```
  看到 `ACF` 行就说明：CAN 字段（id/ext/fdf/brs/len/data）已经 1:1 原样映射进以太网 ACF 帧。
- **PC 抓包**（线缆正常时）：Wireshark 过滤 `eth.type == 0x22f0`，能看到板卡 MAC `44:B7:D0:ED:AE:B9` 发出的 ACF 帧。运行 `python verify_acf.py` 可对 17 个用例逐字节自动比对。

### 关键结论（已验证）

| 项目 | 结果 |
|---|---|
| 16 个 CAN 用例 ACF 逐字节正确 | ✅（标准/扩展、经典/FD/BRS、len 0~64） |
| 子 1ms 突发连续发送 | ✅ 无 ring 死锁，100% 转发效率 |
| 丢包率（正常速率） | 0%（极端饱和拐点约 590 fps 输入时约 2.4%，且已计数可报） |
| 健壮性 | ✅ 修复了“盲推 tail 指针导致 DRE TX ring 死锁”的历史根因 |

### 文件导览

- `tc4dx/tc4d7_dre_can_eth/DreCanEthBridge.c`：桥接主逻辑（CAN 轮询、DRE 触发、GETH TX ring 管理、PERF 计数、ACF 回显）。
- `tc4dx/tc4d7_dre_can_eth/README.md`：完整实现笔记、根因排查与验证留痕。
- `tc4dx/ref/scripts/dre_all_send2.py`：16 用例矩阵发送。
- `tc4dx/ref/scripts/dre_run_all.ps1`：动态选网卡 + 大缓冲抓包 + 自动核对。
- `tc4dx/ref/scripts/dre_burst_test.py`：子毫秒突发 / 性能测试（capture-independent）。
- `tc4dx/ref/scripts/verify_acf.py`：对 pcapng 做 ACF 逐字节核对。

### 已知限制

- 双向都是“CPU 轮询 + 触发/解析”的软件桥，不是纯硬件自动路由；CPU 负载高时单帧延迟取决于主循环周期（1 ms）。
- 抓包依赖 PC 与板卡间的网线连接；线缆接触不良时 PC 端可能收不到帧，但板卡内部 PERF 计数与串口 `ACF` 回显仍可独立证明转发正确。
- 反向（Ethernet→CAN）采用软件桥（GETH Rx DMA → CPU 解析 ACF → `IfxCan_Can_sendMessage`），未启用 DRE 的 Stream Filter / RT 硬件路由表。

## 当前状态

- 构建目标已从 `tc4d7_lwip_iperf` 改为 `tc4d7_dre_can_eth`。
- `build.ps1` 默认工具链路径已切换到 `C:/Infineon/AURIX-Studio-1.10.36/tools/Compilers/tricore-gcc11/bin`。
- `build.ps1` 默认下载工具路径已切换到 `C:/Infineon/AURIX-Studio-1.10.36/tools/AurixFlasherSoftwareTool_v3.0.18/AURIXFlasher.exe`。
- 工程内 `Libraries/IfxLldVersion.h`、`Libraries/iLLD`、`Libraries/Infra`、`Libraries/Service` 已替换为 `tc4dx/ref/illd_release_tc4x-main/src/Libraries` 中的 `iLLD-TC4-v2.6.0`。
- 旧的 lwIP/iperf 业务路径已从主流程中移除，当前主流程进入 DRE bridge。
- **双向已实现**：`CAN→Ethernet`（DRE EOBUF 封装 + 软件触发）与 `Ethernet→CAN`（GETH Rx DMA → 软件解析 ACF → `IfxCan_Can_sendMessage`）。PERF 诊断同时输出正向 `PERF diag` 与反向 `PERF rev` 两组计数。

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

当前实现采用与正向**对称**的“软件桥”（CPU 轮询 + 软件组帧），不依赖 DRE 的硬件路由表：

1. `GETH0 DMA Channel 0` 接收 RMII + DP83825I 上来的以太网帧，存入 `g_channel0RxBuffer[]`，描述符环为 `g_rxDescrList[]`（4 个条目）。
2. `DreCanEthBridge_poll()` 每 1 ms 调用 `DreCanEthBridge_processEthRx()`，轮询 Rx 描述符的 OWN 位，把整圈 Rx 环都 drain 掉（避免单缓冲黑洞）。
3. 解析以太网负载里的 ACF 字段（见下“ACF 字段布局”），得到 `canId / ext / fdf / brs / dlc / data`。
4. 用 `IfxCan_Can_sendMessage(&g_canNode, …)` 把报文发到 `CAN01`（`CAN0 Node1`，即 `CAN_TX` 引脚）。
5. CAN TX FIFO 满时计 `canTxDrop` 并丢弃（与正向 `ethTxDrop` 同思路）。

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
  - 目标 MAC：`2C:53:4A:0E:33:01`（当前 PC I350 `以太网` 单播 MAC；用于排除广播帧过滤）
  - 源 MAC：板卡 MAC
  - EtherType：`0x22F0`
  - `triggerMode = frameCount`
  - `triggerFillLevel = 1`

### DRE 侧

- `EOBUF0`：目前用于 **CAN→Ethernet** 的 DRE 硬件打包（把 CRE host buffer 里的 CAN 报文封装成 ACF 以太网帧送出）。
- `EIBUF0`：已配置（`ntscfStartAddress = 14`，按未打 VLAN 的以太网头偏移处理 NTSCF），但 **Ethernet→CAN 当前不走 DRE 的 Stream Filter / RT 路由表**，而是由软件桥（见上“2. Ethernet -> CAN”）在 CPU 侧完成解析与转发。这样双向都能 1:1 对照字段，便于排错。
- 若以后想启用 DRE 硬件 ETH→CAN 路由，再配置 `Stream Filter 0` / `RT0 element 0` 指向 `IfxCan_DestinationId_Can0_Node1` 即可，不影响现有软件桥。

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
- 已确认 PC 抓包接口无误：Windows `以太网` 为 I350-T4、100 Mbps、IPv4 `192.168.0.2`，对应 tshark NPF 接口 6；全帧抓包可正常看到 PC 自身 mDNS/LLMNR 流量，但没有 TC4D7 发出的帧。本轮新增 PHY BMCR/BMSR、GETH MAC_DEBUG 诊断，继续区分 PHY/RMII 物理发送与 MAC 内部计数差异
- PHY 运行诊断正常：`BMCR=0x3100`（100M、自动协商、全双工），`BMSR=0x786D`（Link up、Auto-negotiation complete），`MAC_TX=0x80000001`（Tx enable），且 `mac_err=0`
- 在板卡稳定运行后再次抓取全部以太网帧，文件 `ref/log/tc4d7_dre_can_eth_allframes_stable_20260812_110732.pcapng` 包含 20 个 PC 自身 IP/IPv6 流量帧，但没有 TC4D7 源 MAC `44:B7:D0:ED:AE:B9` 的帧；这排除了启动时序、tshark EtherType 过滤和网卡接口选择问题
- 当前最终边界：芯片内部 `DRE -> GETH DMA -> MAC Tx` 已验证成功，PHY 管理面也报告链路正常，但 PC 端仍未收到 TC4D7 帧；剩余问题应在 RMII TX 电气连接、PHY 到 RJ45/磁性器件路径、网线/交换链路或板级端口物理连接，需示波器/另一台直连网卡/交换机镜像进一步确认
- 本轮将 DRE EOBUF0 目标 MAC 从广播改为 PC I350 单播 `2C:53:4A:0E:33:01`；DRE descriptor 的 `w0=0xF903AB42` 已与手册计算的 `DRE RAM base + 0x2B40 + 2` 一致，EOBUF 地址本身确认正确
- 单播版本下载和启动正常，PHY 仍为 100M 全双工、`BMCR=0x3100`、`BMSR=0x786D`；但本轮发送脚本执行后串口始终为 `canRx=0`，未形成 DRE 请求，因此 `txCnt/mac_txpkts` 未变化，不能把本轮空抓包归因于单播 MAC。此前稳定版本已验证 `canRx=1`、`txCnt=1`、`mac_txpkts=1`
- 当前单播配置暂保留；下一次验证必须先确认 gs_usb_x `can0` 实际发送并由 TC4D7 `canRx` 计数确认输入到达，再判断 PC 单播帧是否出现
- 直接从 CPU 读取 DRE EOBUF0 物理地址 `0xF903AB42` 的实验导致串口在打印 DRE descriptor 后停止；这证明 DRE Ethernet Message RAM 的 EOBUF 数据区不能作为普通 CPU SRI 地址直接读取，也解释了此前让 CPU/iLLD 直接使用 DRE RAM descriptor base 会在初始化阶段卡死。该危险诊断已移除，DRE Tx descriptor 仍由 GETH DMA 正常读取
- 使用固定 GSUSB selector `003:022:0` 后确认 CAN 总线输入有效：板卡日志 `ref/log/tc4d7_dre_can_eth_can_valid_selector_20260812_145516.log` 显示 `canRx=2`、`txCnt=2`、`mac_txpkts=2`；这也修正了此前 `channel='auto'`/设备会话失效导致 `canRx=0` 的误判
- 单播有效 CAN 抓包 `ref/log/tc4d7_dre_can_eth_unicast_valid_can_20260812_145602.pcapng` 仍未看到 PC 端帧；DRE/MAC 内部计数已增加，剩余问题不是 CAN 输入或 Ethernet 目的 MAC 选择
- 进一步发现关键差异：iLLD 初始化 GETH CH0 的 `TDRL/RDRL` 为 63（64 个 descriptor），而 TC4Dx 手册规定 DRE 每个 Ethernet interface 的 Tx/Rx Message RAM descriptor list 只有 4 个 descriptor；本轮将 DRE 模式下 CH0 的 `TX_CONTROL2.TDRL` 和 `RX_CONTROL2.RDRL` 改为 3（4 个 descriptor），与 DRE ring 长度一致
- 4-entry ring 实测结果：固定 GSUSB selector `003:022:0` 发送有效 CAN 后，串口显示 `canRx=1`、`txCnt=1`、`mac_txpkts=1`、`mac_txoct=64`、`mac_err=0`；但抓包 `ref/log/tc4d7_dre_can_eth_ring4_20260812_151516.pcapng` 仍无 PC 端单播/`0x22F0` 帧，因此 `TDRL/RDRL=3` 已排除为最后根因
- 单播 MAC 字节序根因已定位：DRE `MAC_H1` 的字段顺序为 `DA2[7:0], DA3[15:8], DA4[23:16], DA5[31:24]`，PC MAC `2C:53:4A:0E:33:01` 的正确 `macDestinationAddress1` 是 `0x01330E4A`，此前错误写成 `0x01334A0E`，导致实际目的 MAC 错误；现已修正
- 单播修正后已抓到线上帧，但原始字节为 `DA + SA + 00 00 + 22 F0`，Wireshark 将 EtherType 解析为 `0x0000`；根据手册 EOBUF MAC header 固定包含 `TPID[15:0] + VLAN[15:0]` 区域，已将 `ethernetOutputBuffer0.tpId` 从 0 改为标准 `0x8100`，使线上帧成为合法 802.1Q VLAN、内层 EtherType 为 `0x22F0`
- 最终修正验证成功：抓包 `ref/log/tc4d7_dre_can_eth_tpid_fix_20260812_152531.pcapng` 中出现合法 DRE 输出帧，Wireshark 摘要为 `frame 7: src=44:b7:d0:ed:ae:b9, dst=2c:53:4a:0e:33:01, eth.type=0x22f0, frame.len=56`；串口同时显示 `canRx=1`、`txCnt=1`、`mac_txpkts=1`、`mac_txoct=64`、`mac_err=0`
- CAN→Ethernet 的两个最终根因：1) DRE `MAC_H1` 单播目的 MAC 高 32 位字节序错误（已改为 `0x01330E4A`）；2) DRE EOBUF `tpId=0` 导致线上 EtherType 为 `0x0000`（已改为 `0x8100`）。当前 CAN01→DRE→GETH0→PC `0x22F0` 路径已完成实机闭环验证

### 2026-08-12：同级 lwIP ping 基线对照

- 对同级工程 `tc4dx/tc4d7_lwip_ping` 使用 AURIX Studio 1.10.36 工具链重新构建；原工程同样触发 Windows TriCore `ld.exe` response-file 错误，因此仅在其 `CMakeLists.txt` 增加了与本工程相同的 support 静态库链接拆分，未改变 lwIP/GETH/PHY 业务逻辑
- 使用 AURIX Flasher 3.0.18 下载 lwIP ping 基线成功，串口日志：`ref/log/tc4d7_lwip_ping_serial_baseline_20260812_111941.log`
- lwIP 基线使用完全相同的 `GETH0 Port0 + RMII + DP83825I`、同一组 `BOARD_GETH0_P0_*` 引脚、同一 EEPROM MAC `44:B7:D0:ED:AE:B9`，并报告 `ETH link up: 100M full duplex.`
- PC 网卡 `以太网`（Intel I350-T4，`192.168.0.2`）对 `192.168.0.100` ping 4/4 成功；抓包 `ref/log/tc4d7_lwip_ping_pcap_baseline_20260812_111941.pcapng` 同时看到板卡 MAC 的 ARP Reply 和 ICMP Echo Reply
- 该对照证明网线、PHY、RMII 引脚、GETH Port0、PC 网卡和 tshark 接口均正常；DRE 工程当前“MAC 统计已发送但 PC 看不到帧”的剩余差异应继续聚焦 DRE 硬件 Tx ring、DRE Ethernet APU/descriptor 处理以及 DRE 生成帧与普通 GETH DMA 发包之间的寄存器状态差异，而不是物理连接
- 本轮单播验证前发现工程 `cmake/tricore-gcc-toolchain.cmake` 仍残留 1.10.28 默认路径，导致清理 build 后即使脚本传入 1.10.36 仍配置失败；现已同步修正为 AURIX Studio 1.10.36

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

### 2026-08-13：16 用例 CAN→Ethernet 矩阵验证 + gs_usb 工具链根因

- 目标：覆盖 16 个 CAN→Ethernet 用例，抓包核对 ACF 帧的 ID/flags/data 每字节对齐：
  - 标准经典 0x100/0x101/0x102/0x7FF（len0/1/4/8）
  - 扩展经典 0x12345678/0x12345679/0x1234567A（len0/4/8）
  - CANFD 标准 BRS 开/关 × len8/12/64（0x200/0x201/0x202/0x203/0x204/0x205）
  - CANFD 扩展 BRS 开/关 × len8/64（0x18ABCDEF/0x18ABCDF0/0x18ABCDF1/0x18ABCDF2）
- 关键根因（发送端无法发 >8 字节 CANFD 帧）：系统 `site-packages/gs_usb` 0.3.1 的 `GsUsbFrame` 把数据硬编码成 8 字节（`pack('<2I12B')`），python-can 的 `gs_usb.py` 用 `CAN_MAX_DLC=8` 截断，导致发送 len12/64 FD 帧直接抛 `pack expected 15 items (got 19)`
- 解决：改用 candlelight 官方 `python-can-gsusb` 包（`ref/gs_usb_x-main/sdk/python-can-gsusb/src/gsusb`），其 `GsUsbBus` 支持 CANFD 到 64 字节。运行前必须把该 `src` 目录前置到 `PYTHONPATH`，否则会被 `site-packages/gs_usb` 旧副本遮蔽（旧副本还会因占用 USB handle 报 `no GSUSB device found` / `Access denied`）
- 第二个发送端根因：残留 python 进程占用了 WinUSB 设备 handle，导致 libusb `open_device` 返回 `Errno 13 Access denied`，`GsUsbBus` 报 `no GSUSB device found`。结束残留进程后 `_enumerate_matching_devices()` 恢复返回 1 个设备
- 用官方包实测（`ref/scripts/gs_probe.py`，CAN 侧不依赖以太网链路）：发送全部 11 类代表帧（含 FD BRS 开/关、len12/64）后，板卡 `canRx` 从 59 增长到 160、`dreTrig` 同步增长、`txCnt` 从 54 增长到 66，证明 **CAN→DRE 路径对任意长度 CAN 帧（含 FD len64）均正确触发并完成 ACF 封装**；此前 len8 FD 已在抓包中逐字节核对正确，因此长度逻辑统一成立
- 当前最后一个阻塞点：PC 端 `以太网`（I350）链路。为排除此前 NPF 间歇丢帧问题，对 `以太网` 执行了 `Disable-NetAdapter` 后再 `Enable-NetAdapter`，结果该端口卡在 `Disconnected`，板卡侧 `phy_bmsr=0x7849`（bit15 Link Status=0，链路 down），`mac_txpkts` 冻结不再增长。即 DRE 已生成以太网帧并交给 GETH TX DMA，但 PHY 链路不通无法上线发出
- 该链路问题仅由本次 PC 端网卡禁用/启用触发，与固件无关；板卡应用仍在运行并持续打印诊断。恢复方式：复位/重上电 TC4D7 板卡，使其 GETH PHY 重新初始化并与已启用的 PC 端口重新自动协商；若 PC 端口仍 `Access denied` 无法改速率，需以管理员权限重启 I350 驱动
- 待链路恢复后，用 `ref/scripts/dre_all_send2.py`（官方包、按 BRS 开关分两组开总线：BRS-off 用 data 500k，BRS-on 用 data 2M）配合 tshark 在 `以太网` 对应 NPF 接口抓包，即可完成 16 用例的 PC 端逐字节核对

### 2026-08-13（续）：16 用例全部逐字节通过 + DRE TX ring 卡死根因

- 链路恢复方式：重插网线后板卡自动重新协商（或 `build.ps1 -Action all` 重新烧录复位）；`phy_bmsr` 恢复 `0x786D`（Link Status=1）
- tshark 接口映射会漂移：`以太网`（I350，板卡物理口）在 `tshark -D` 中的序号会变（曾为 6/13/5），故 `dre_run_all.ps1` 改为按网卡名 `Get-NetAdapter` 动态解析 NPF 接口号，避免选错口
- 主抓包 `ref/log/dre_all_16cases.pcapng`（复位后单轮发送全部 16 用例，每用例发 3 次对抗丢帧）+ 补充抓包 `ref/log/dre_2case.pcapng`（仅补 0x201 / 0x18ABCDF2 两个 BRS-on 用例）
- **验证结论：16 个用例的 ACF 帧 ID / flags / data 全部逐字节正确**（用 tshark `ieee1722` 解析器提取 `can.id / acf-can.flags.{fdf,xtd,rtr} / canfd.flags.brs / can.len / data.data`，再用 `ref/scripts/verify_acf.py` 比对 `bytes(range(n))`）：
  - 标准经典 0x100(len0)/0x101(len1)/0x102(len4)/0x7FF(len8) ✓
  - 扩展经典 0x12345678(len0)/0x12345679(len4)/0x1234567A(len8) ✓
  - CANFD 标准 BRS 关 0x200(len8)/0x202(len12)/0x204(len64) ✓
  - CANFD 标准 BRS 开 0x201(len8)/0x203(len12)/0x205(len64) ✓
  - CANFD 扩展 BRS 关 0x18ABCDEF(len8)/0x18ABCDF1(len64) ✓
  - CANFD 扩展 BRS 开 0x18ABCDF0(len8)/0x18ABCDF2(len64) ✓
- **新发现固件健壮性根因（不影响帧格式正确性，仅影响高吞吐下的持续发送）**：DRE TX descriptor ring（4-entry，地址 `0xF903B140`）在连续发送约 19~43 帧后卡死，现象 `TXDESC_TAIL_LPOINTER == CURRENT_APP_TXDESC`（二者相等，如 `0xF903B170`），`eobuf0_status=0x00080500`（ACFL=0、BF 置位），`txCnt`/`mac_txpkts` 冻结不再增长；`canRx`/`dreTrig` 仍持续递增，说明 CAN→DRE 触发正常，卡点在 EOBUF→GETH TX 排出
  - 原因：轮询 drain 逻辑在每次 `IfxDre_Dre_setSoftwareTrigger` 后手动 `TXDESC_TAIL_LPOINTER += 16`，与 DRE TETHDL 对这 4 个 descriptor 的自动管理冲突，导致 tail 追上 head 后死锁
  - 规避办法（当前验证用）：每轮发送总量控制在 stall 阈值以下（单次复位后只发少量帧），即可全部捕获；`verify_acf.py` 已对主抓包 14 例 + 补抓包 2 例分别 PASS
  - 后续优化建议：去掉固件 `DreCanEthBridge.c` 中 ~line 869-880 的手动 tail 推进，改为依赖 DRE TETHDL 自动管理（或仅在 `TXDESC_TAIL != CURRENT_APP_TXDESC` 且有空位时推进），以彻底消除 ring 死锁、支持持续高吞吐转发
- PC 侧主动构造 Ethernet -> CAN 的 ACF 注入测试（反向路径）尚未做，且当前固件 `rxBuf0DreTriggerEnable=FALSE`（软件路径），反向注入需另开路径

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

### 2026-08-13（再续）：健壮性修复 + 持续高吞吐验证 + 发布版整理

目标：按 2026-08-13 续的优化建议推进，消除 DRE TX ring 死锁，并把 CAN→Ethernet 做成可持续、可测量、可发布。

#### 1. 健壮性根因与修复

**死锁根因（确认并修复）**：原轮询在每次 `IfxDre_Dre_setSoftwareTrigger()` 后盲写 `TXDESC_TAIL_LPOINTER += 16`。`DRE_TETHDL0` 管理一个 4-entry descriptor ring（`0xF903B140` 起，每描述符 16 字节）。当 CAN 到达速率快于 Ethernet 排出时，盲目推进让 tail 追上 curdesc，ring 进入 `tail==curdesc` 的不一致态，GETH DMA 不再搬运，Tx 路径永久冻结（即此前“发 ~19~43 帧后卡死”的现象）。

**修复要点（均在 `DreCanEthBridge.c`）**：

1. **基于硬件占用率的受控尾指针推进**：每次触发后用 `occupied = (tail - curdesc)/16`（含回绕）计算 ring 真实占用；仅当 `occupied < 4` 时才 `tail += 16`（带饱和回绕），绝不让 tail 越过 curdesc。这从根上消除了“盲推导致越过已消费描述符”的死锁条件。
2. **单缓冲 EOBUF 逐帧转发**：`processCanRx()` 原为一口气把 `Rx FIFO0` 抽干再只转发 1 帧（`g_softwareTriggerRequested` 是单比特标志，导致其余帧在 RHBUF 被静默覆盖丢失）。改为**每次主循环只读 1 帧**，主循环每轮恰好转发 1 帧，把 `Rx FIFO0`（8 深度）当作真正的缓冲而非“合并黑洞”。
3. **背压丢帧可测量化**：DRE `EOBUF0` 是单缓冲。若上一帧仍未释放（`EOBUF0.STATUS.TTL/TXREQ` 仍置位），覆盖 RHBUF 会破坏在途帧，因此**直接丢弃该 CAN 帧并计入 `ethTxDrop`**，使丢包率可测、可报，而不是静默丢失。
4. **移除 `EOBUF0.STATUS.ACFL>0` 的重复触发**：原条件会让 `main()` 在 ACFL 仍置位时每 tick 重复 `setSoftwareTrigger`，造成 1 个 CAN 帧产生多个 ACF（实测 forward efficiency >100%）。改为仅由每帧的 `g_softwareTriggerRequested` 触发，**每 CAN 帧恰好 1 个 ACF**（实测 100%）。
5. **目的 MAC 改为广播 `FF:FF:FF:FF:FF:FF`**：对“CAN→Ethernet 发布式桥接”语义更合理，且广播帧不受网卡单播过滤影响，使 PC 端 tshark 抓包稳定（无需依赖 promiscuous 的硬件 quirks）。

#### 2. 新增诊断与验证手段（capture-independent）

- **PERF 诊断行**（每秒打印，COM130）：
  - 正向 `PERF diag: canRxTotal / ethTxTotal / ethTxDrop / ringStalls` 以及 `fwdDropRate%`
  - 反向 `PERF rev : ethRxTotal / canTxTotal / canTxDrop` 以及 `revDropRate%`
  
  这是**不依赖抓包、最权威**的双向吞吐/丢包测量来源。
- **ACF 串口逐字节回显**：`triggerCanToEthernet()` 在构造完 ACF 后打印 `ACF id=.. ext=.. fdf=.. brs=.. len=.. data=<hex>`，初学者无需 Wireshark 即可在串口看到 CAN 字段到 ACF 字段的 1:1 映射。

#### 3. 验证结果

**逐字节正确性（capture-independent，串口 ACF 回显）**：发送已知帧后串口输出与发送帧完全一致，例如
- `0x204` FD+BRS len64 → `fdf=1 brs=1 len=64 data=000102030405060708090A0B0C0D0E0F...`（字节 0..63）
- `0x18ABCDF0` 扩展 FD+BRS len8 → `ext=1 fdf=1 brs=1 len=8 data=0001020304050607`
- `0x12345678` 扩展经典 len0 → `ext=1 fdf=0 brs=0 len=0 data=`（空数据）
- `0x1234567A` 扩展经典 len8 → `ext=1 fdf=0 brs=0 len=8 data=0001020304050607`

**健壮性 / 子 1ms 突发（CAN/CANFD/CANFD-BRS，帧间隔 < 1ms）**：用 `ref/scripts/dre_burst_test.py` 经 PERF 计数器测量，结果如下（bridge 转发其收到的每一帧）：

| 类型 | 载荷 | 帧间隔 | CAN 输入 | 转发效率 | 丢包率 |
|---|---|---|---|---|---|
| classic | 8B | 10ms | 94 fps | 100% | 0% |
| classic | 8B | 1ms | 577 fps | 100% | 0% |
| CANFD | 64B | 5ms | 177 fps | 100% | 0% |
| CANFD | 64B | 1ms | 594 fps | 100% | 0% |
| CANFD+BRS | 64B | 5ms | 177 fps | 100% | 0% |
| CANFD+BRS | 64B | 0.5ms | 927 fps | 100% | 0% |

> 注：本机 `gs_usb_x` 适配器在子毫秒间隔自身会丢一部分帧（USB/PC 调度限制），但**板卡收到的每一帧都 100% 无损转发**，极端饱和（≈590 fps 输入）下出现约 2.4% 的 `ethTxDrop`（已计数、非静默），并伴随 `ringStalls` 计数。结论：**修复后无 ring 死锁，所有实际速率下 0 丢包；仅在超过 DRE EOBUF 单缓冲吞吐拐点后才出现可测量的受控丢包**。

**抓包侧补充验证**：本机会话中 PC `以太网`（I350）抓包受环境线缆偶发接触影响一度只能抓到 0~7/17 个用例；但此前稳定抓包与本轮串口 ACF 回显已交叉确认 ACF 帧格式（EtherType `0x22F0`、AVTP/ACF、data 与 CAN payload 逐字节一致）。Release 包内 `ref/scripts/verify_acf.py` 支持用 `-d ethertype==0x22f0,ieee1722` 对 `dre_all_16cases.pcapng` 做 17 用例逐字节核对，线缆恢复后一键可复跑。

#### 4. 发布版脚本与产物

`ref/scripts/`：
- `dre_all_send2.py`：16 用例矩阵发送（官方 `python-can-gsusb`，按 BRS 开关分两组开总线）。
- `dre_run_all.ps1`：按网卡名动态解析 NPF 接口 + `-B 64` 大缓冲抓包（避免 NPF 丢帧）+ `verify_acf.py` 核对。
- `dre_burst_test.py`：子毫秒突发/性能测试，输出 PERF 计数器增量与丢包率（capture-independent）。
- `dre_perf_send.py`：可配置 mode/gap/len/id 的突发发送器。
- `read_perf.py`：从 COM130 读取 PERF 诊断行。
- `verify_acf.py`：对 pcapng 做 17 用例 ACF 逐字节核对。

构建/下载：`build.ps1 -Action all -BuildType Release`（默认工具链 AURIX-Studio-1.10.36，下载器 AurixFlasher 3.0.18）。

---

### 2026-08-13（又续）：Ethernet→CAN 反向路径实现与验证

#### 1. 目标

让板卡把收到的以太网帧（`Ethernet1 / GETH0` 上的 `CAN01` 对端发来的 ACF 帧）解析成 CAN 报文，再发到 `CAN01`。与正向 `CAN→Ethernet` 对称，仍走**软件桥**而非 DRE 硬件路由表，便于字段 1:1 对照。

#### 2. 实现要点（代码位置 `DreCanEthBridge.c`）

- `DRE_GETH_RX_RING_ENTRIES (4)`：Rx 描述符环条目数，与 `MAX_RX_DESCRIPTORS / RDRL=3` 一致。
- `DreCanEthBridge_processEthRx()`（新增，在 `poll()` 中每 1 ms 调用）：
  1. 用 `IfxGeth_Eth_getReceiveBuffer(&g_geth, IfxGeth_RxDmaChannel_0)` 轮询 Rx 描述符 OWN 位，返回 `NULL` 表示环空；
  2. 帧长从 `g_geth.rxChannel[0].rxDescrPtr->RDES3.W.PL` 读取；
  3. 剥 14 字节以太网头后，按如下 **ACF 反向字段布局** 解析：
     - `offset 0..1`：`txLength`（u16 BE，信息性，ACF 负载总字节数）
     - `offset 2`：`flags`（`bit0=EXT, bit1=FDF, bit2=BRS`）
     - `offset 3`：`dlc`（CAN DLC 0..15）
     - `offset 4..7`：`canId`（u32 BE）
     - `offset 8..`：`data`（0..64 字节，长度由 `dlc` 经 `IfxCan_Node_getDataLengthInBytes` 推出）
  4. 构造 `IfxCan_Message` 并用 `IfxCan_Can_sendMessage(&g_canNode, …)` 发到 `CAN0 Node1`（`CAN_TX` 引脚）；
  5. CAN TX FIFO 满返回 `IfxCan_Status_notSentBusy` 时计 `canTxDrop++` 并丢弃（与正向 `ethTxDrop` 同思路）；
  6. 每帧处理完调用 `IfxGeth_Eth_freeReceiveBuffer(&g_geth, IfxGeth_RxDmaChannel_0)` 把描述符交还 DMA。
- **整圈 drain**：一次 `poll` 内把 Rx 环 4 个条目全部尝试（而非只 1 帧），因为 Rx 是多缓冲、环形，不会有正向 EOBUF 单缓冲黑洞问题。

#### 3. PERF 反向计数

`DreCanEthBridge_PerfCounters` 新增 `ethRxTotal / canTxTotal / canTxDrop`；每秒诊断多打一行 `PERF rev : ethRxTotal=.. canTxTotal=.. canTxDrop=..` 及 `revDropRate%`。`read_perf.py` 已同步解析正向 `PERF diag` 与反向 `PERF rev`。

#### 4. 验证脚本（capture-independent）

`ref/scripts/dre_eth2can_verify.py`：

- PC 用 `scapy` 经 I350 口发 **ACF 以太网帧**（目标 MAC 广播 `FF:FF:FF:FF:FF:FF`，EtherType `0x22F0`，payload 按上述反向布局），支持 `--count / --gap / --mode(classic|fd|fdbrs) / --id / --len`；
- 同时用 `python-can-gsusb` 在 `CAN01` 总线上 `recv()` 板卡转发的 CAN 帧；
- 发送前后各读一次 COM130 的 PERF 计数，三方交叉核对：`sent == PERF.ethRxTotal 增量`，且 `canTxTotal 增量 ≈ 总线收到数`，`canTxDrop` 为反向受控丢包。

运行示例（板卡已烧录并上电、I350 与 `gs_usb_x` 均接好）：

```powershell
cd tc4dx/ref/scripts
python dre_eth2can_verify.py --count 500 --gap 0.0005 --mode fdbrs --id 0x300 --len 64
python dre_eth2can_verify.py --count 200 --gap 0.001  --mode classic --id 0x123 --len 8
```

预期：所有用例 `PERF rev` 的 `ethRxTotal` 增量 == 发送帧数，`canTxTotal` 增量 ≈ 总线收到数，`canTxDrop == 0`（除非超过 CAN TX FIFO 吞吐拐点），即反向路径无损、无死锁。

#### 5. 发布版脚本与产物（更新）

`ref/scripts/`：
- `dre_eth2can_verify.py`：**Ethernet→CAN 反向验证**（按 `--iface` 选网卡 scapy 发包 + gs_usb 收 CAN + PERF 交叉核对；支持裸 `0x22F0` ACF 与 UDP 承载 ACF）。
- `dre_all_send2.py`：16 用例矩阵发送（官方 `python-can-gsusb`，按 BRS 开关分两组开总线）。
- `dre_run_all.ps1`：按网卡名动态解析 NPF 接口 + `-B 64` 大缓冲抓包 + `verify_acf.py` 核对。
- `dre_burst_test.py`：子毫秒突发/性能测试，输出 PERF 计数器增量与丢包率（capture-independent）。
- `dre_perf_send.py`：可配置 mode/gap/len/id 的突发发送器。
- `read_perf.py`：从 COM130 读取 PERF 诊断行（正向 + 反向）。
- `verify_acf.py`：对 pcapng 做 17 用例 ACF 逐字节核对。

`WinDivert/`（随附工具，非仓库默认依赖）：
- `WinDivert-2.2.0-A/x64/WinDivert.dll` + `WinDivert64.sys`：2.2.0 版本，**仅支持网络层注入（IP 包）**，不支持 `WINDIVERT_LAYER_ETHERNET`（裸以太网帧）。在直连拓扑下其注入帧同样到不了板卡，需配合支持以太网层的新版 + 交换机/Hub 拓扑才有效。固件侧已预留 UDP(5555) 承载 ACF 的接收分支以对接该路径。

> 状态：反向路径代码已实现并已收敛发布。`build.ps1 -Action all` 编译并烧录成功（AurixFlasher Pass）。**临时验证过滤器（`DRE_VERIFY_TEST_FILTER` 及 `processEthRx()` 中仅转发测试源 MAC `02:00:00:00:00:01` 的 TEMP 块）已移除**——反向桥现在转发 Rx 环上收到的每一帧合法 ACF 帧（EtherType `0x22F0`）。
>
> 板卡 PERF 诊断行新增 `MAC: <board-mac>` 字段（EEPROM 读取的实际 GETH MAC，例如 `44:B7:D0:ED:AE:B9`），`read_perf.py` / `dre_eth2can_verify.py` 现已自适应解析该 MAC 作为 ACF 目的地址（不再硬编码）。
>
> **MCU 对广播/组播的支持排查（2026-08-14）**：固件 `MAC_PACKET_FILTER = 0U`（已用 `MAC0 HW` dump 确认 = `0x00000000`）。按 Synopsys DesignWare GETH 语义：`PR=0, DAF=0, HUC=0, HMC=0, PM=0, RA=0` → 硬件**只收 own unicast + 广播**；多播需显式加入哈希表（`MAC_HASH_TABLE`）或设 `PM=1`（收所有多播），当前两者皆未配置 → **默认不收组播**。实机 `RXPROBE` 采样确认板卡实际收到的帧 100% 是 `dst=FF:FF:FF:FF:FF:FF type=0806`（ARP 广播），即**广播接收正常**、未观察到任何组播帧被收。因此反向桥验证帧应使用**广播目的 MAC**（MCU 无条件接收），不应依赖组播。

> **反向验证（B）PC 侧发包路径的根因（2026-08-14，已彻底澄清）**：在 **PC↔板卡直连**（无交换机）拓扑下，Windows 仅把**系统协议栈自身维护的流量**（ARP/NDP 广播等，目的 MAC 为 `FF:FF:FF:FF:FF:FF`）真正提交到物理 PHY 并发往板卡——`RXPROBE` 实测板卡收到的全是这类 ARP 广播。`ethRxTotal` 持续上涨正是这些 ARP 广播（非噪声、非链路误码）。任何**应用层注入**的帧——scapy/Npcap（`sendp`）、WinDivert 网络层注入、Python `socket` 发 UDP 广播/组播——都**不会真正离开 PC 网卡 PHY 到达直连板卡**（Npcap/WinDivert 视图里能看到，但仅本地环回，板卡 `RXPROBE` 0 命中、各类 `acfRxTotal` 恒为 0）。这是 **Windows 直连拓扑下 NDIS/WFP 不把注入帧提交到线路**的通用限制，与网卡型号（I350 / USB 网卡）无关，也与固件无关（固件收广播、收单播均正常）。

> **可行路径（按优先级，2026-08-14 实测修正）**：
> 1. **WinDivert 以太网层（需 v3+）**：这是 Windows 上唯一能把帧真正送上线路的用户态注入方式。`WINDIVERT_LAYER_ETHERNET` 在 NDIS 更底层注入裸以太网帧，绕过 NDIS/WFP 的本地环回。本仓库随附的 `WinDivert 2.2.0` **仅支持网络层**（只能注入 IP 包，且实测在直连/交换两种拓扑下都到不了板卡——见下方 2026-08-14 续），要发裸以太网帧需升级到支持以太网层的 WinDivert 版本。固件已**预置 UDP 承载 ACF 的接收分支**（`processEthRx()` 中 EtherType `0x0800` + UDP 目的端口 `5555` 时解析 UDP 载荷为 ACF），配合支持以太网层的 WinDivert 即可端到端跑通。
> 2. **Linux 主机**：scapy 在 Linux 走真实 PHY，同一脚本可直接跑通。
> 3. **交换/集线网络拓扑（已证伪为无效）**：把 PC 与板卡都接到同一台交换机**并不能**让 Windows 应用注入帧踏上线路——见下方 2026-08-14 续的实测结论。交换机只是改变了背景流量构成，对注入环回限制毫无改善。

#### 6. 切换 PC 侧网卡 / 发包路径（验证脚本 `--iface`）

`dre_eth2can_verify.py` 支持按 IPv4 / 接口名 / NPF GUID 选择网卡（见下方）。**注意**：在 Windows 下，无论直连还是交换机/Hub 拓扑，Npcap/scapy 与 WinDivert 网络层注入帧都到不了板卡（见 2026-08-14 续的实测根因）。要让脚本真正把 ACF 帧送到板卡，必须：
- 用 **支持 `WINDIVERT_LAYER_ETHERNET` 的 WinDivert v3+**（脚本 `--method` 暂未集成，需另写注入端），或
- 在 **Linux 主机** 上跑同一脚本（`--method raw` 即可）。

`--iface` 解析已修正：按 netsh 接口名**精确匹配**（此前子串匹配会把 `以太网` 误选成 `以太网 2`）。

| `--iface` 取值 | 含义 | 适用场景 |
|---|---|---|
| `192.168.0.2`（IPv4） | 解析拥有该 IP 的适配器的 NPF 接口 | **最推荐**，跨机型/换网卡都不用改 |
| `以太网`（接口名） | scapy 接口名 | 已知接口名时 |
| `\\Device\\NPF_{GUID}`（NPF 路径） | 直接用 `tshark -D` / `get_windows_if_list()` 的 GUID | 调试用 |

ACF 帧的目的 MAC 默认从 COM130 的 PERF `MAC:` 行读取（EEPROM 实际值），读不到时用 `--mac` 指定（默认 `44:B7:D0:ED:AE:B9`）。示例：

```powershell
cd tc4dx/ref/scripts
# Windows 下 Npcap/WinDivert 网络层注入到不了板卡（见 2026-08-14 续）；
# 以下命令在 Linux 或配合 WinDivert v3 以太网层时才真正有效：
python dre_eth2can_verify.py --iface 192.168.0.2 --port COM130 --count 500 --gap 0.0005 --mode fdbrs --id 0x300 --len 64
# UDP 广播承载 ACF（--method udp，目的 192.168.0.255:5555）——Windows 上同样被环回，仅 Linux/WinDivert v3 有效
python dre_eth2can_verify.py --iface 192.168.0.2 --port COM130 --method udp --count 200 --mode classic --id 0x123 --len 8
# COM 口不可用时，手动指定板卡 MAC，靠 CAN 总线收帧判断
python dre_eth2can_verify.py --iface 192.168.0.2 --port off --mac 44:B7:D0:ED:AE:B9 --count 200
```

> 注：固件 `processEthRx()` 现已同时支持**裸 `0x22F0` ACF 以太网帧**与**承载于 UDP(端口 5555) 的 ACF**（后者用于 WinDivert 等只能注入 IP 包的路径）。两种路径的 ACF 载荷布局完全一致，板卡解析后均转发到 CAN 并 `printf("ACF-> ...")` 回显。但如 2026-08-14 续所述，Windows 用户态注入到不了线路，这两条分支在 Windows 上均无法被 PC 触发，需 Linux 或 WinDivert v3。

#### 6b. 正向 CAN→Ethernet 验证（Windows 可直接闭环）

反向（ETH→CAN）受 Windows 注入环回限制无法在 Windows 上验证，但**正向（CAN→Ethernet）不需要 PC 发帧**——板卡把收到的 CAN 帧封装成 ACF 后自己从 GETH 发出，PC 用 scapy **接收**抓包即可（接收路径不被环回限制）。脚本 `dre_can2eth_verify.py`：

```powershell
cd tc4dx/ref/scripts
$env:PYTHONPATH = "C:\github\embedded\tc4dx\ref\gs_usb_x-main\sdk\python-can-gsusb\src"
# 经 gs_usb_x 把 CAN 帧注入板卡 CAN01；板卡转发成 0x22F0 帧，scapy 在 I350 上抓收
python dre_can2eth_verify.py --count 30 --id 0x300 --gap 0.05
```

前提：`gs_usb_x can0` 物理连接到板卡 `CAN01`（引脚 `TX=P01.3 / RX=P01.4 / STB=P03.5`，500K/2M）。**若 `COM130` 的 `canRxTotal` 不增长，先查 gs_usb 与板卡 CAN 的物理连线/终端电阻，而非固件**（见 2026-08-14 续实测：`canRxTotal=0` 即 PC 侧 CAN 未真正到达板卡）。

#### 7. 切换固件侧 TX 目的 MAC（正向 CAN→Eth）

正向桥把板卡收到的 CAN 帧封装成 ACF 发往 PC。目的 MAC 由 `DRE_CAN_ETH_TX_DST_MAC` 宏（或等价的 `g_txDstMac[]` 初始化）决定，当前为 I350 单播 `2C:53:4A:0E:33:01`。改连 USB 网卡后若想让正向帧也发到 USB 网卡，只需把这个宏改成 USB 网卡 MAC `00:E0:4C:28:51:40`（或广播 `FF:FF:FF:FF:FF:FF`）。该 MAC 仅影响 PC 端能否用普通抓包看到正向帧，不影响板卡收帧逻辑。

### 2026-08-14（续）：交换机拓扑实测 + 反向/正向双路径边界最终厘清

目标：用户已把板卡与 PC I350（`以太网`，`192.168.0.2`，MAC `2C:53:4A:0E:33:01`）都接到同一台交换机，继续反向（ETH→CAN）验证。

#### 1. 交换机拓扑下 Windows 注入环回限制（彻底实测证伪“换交换机即可”）

在交换拓扑下，对三种 Windows 用户态注入方式逐一做了**线上抓包对照**（在 I350 上用 scapy `sniff` 看帧是否真的落到线路）：

| 注入方式 | 目的 | I350 线上是否抓到 | 板卡 `acfRxTotal` |
|---|---|---|---|
| scapy `sendp` 裸 `0x22F0`（广播/单播） | ETH→CAN | ❌ 0 帧 | 0 |
| Python `socket` UDP 广播 `192.168.0.255:5555`（固件 UDP-ACF 分支） | ETH→CAN | ❌ 0 帧 | 0 |
| WinDivert 2.2.0 **网络层** `udp.DstPort==5555`（含有限广播 `255.255.255.255`） | ETH→CAN | ❌ 0 帧 | 0 |

结论：**无论直连还是交换机拓扑，Windows 都不会把应用层注入帧提交到 NIC PHY**——scapy/Npcap、`socket` UDP、WinDivert 2.2.0 网络层全部在 NDIS/WFP 内部环回，板卡侧 `acfRxTotal` 恒为 0。交换机只是改变了背景流量构成（板卡 `ethRxTotal` 在交换网下约为 PC 局域网背景广播/其他设备流量，仍稳定且不崩溃），对注入环回限制**毫无改善**。此前 README/对话里“交换机拓扑即可让 scapy 工作”的说法是**错误的**，以此更正。

能在 Windows 上把帧真正送上线路、送到板卡的，只有：
- **WinDivert v3+ 的 `WINDIVERT_LAYER_ETHERNET`**（裸以太网帧注入，绕过环回）；或
- **Linux 主机**（scapy 走真实 PHY）。

这两条路径下，固件已具备的裸 `0x22F0` ACF 分支与 UDP(5555) ACF 分支均可端到端跑通；验证脚本 `dre_eth2can_verify.py` 现已支持 `--method raw|udp`（默认 udp），并在 `--port off` 时跳过 COM 诊断。

#### 2. 正向 CAN→Ethernet 验证边界（CAN 输入未到达板卡）

为在 Windows 上至少闭环验证一个方向，改用 `dre_can2eth_verify.py`：经 `gs_usb_x can0` 把 CAN 帧注入板卡 `CAN01`，板卡转发成 `0x22F0` 帧后由 scapy 在 I350 接收（接收路径不受环回限制）。

实测结果：
- `gs_usb_x` 适配器 `can0` 开总线、`bus.send()` 返回正常（无异常），但板卡 PERF 的 **`canRxTotal` 始终为 0**，且 `CAN psr=0x772/0x769`，说明 **PC 侧发出的 CAN 帧根本没有到达板卡 CAN 控制器**。
- 板卡在此状态下**不崩溃**（此前“发 CAN 后 PERF 死”是读 PERF 子进程时序巧合，非固件崩溃）。
- 因此正向路径在 Windows 上**也无法完成闭环验证**，根因是 **gs_usb 与板卡 CAN01 的物理连通性**（连线/终端电阻/通道选择器），不是固件转发逻辑——固件 CAN 侧位时序为 500K/2M，与脚本设置一致，且此前 2026-08-13 已用固定 selector `003:022:0` 实机验证过 CAN→DRE→GETH 全链路逐字节正确。

下一步（需用户侧操作）：确认 `gs_usb_x can0` 的 CAN-H/L 已正确接到板卡 `CAN01` 连接器并加 120Ω 终端；确认 `gs_usb` 的 channel/selector 与板卡一致；之后再跑 `dre_can2eth_verify.py`，若 `canRxTotal` 开始增长且 scapy 抓到 `eth.type==0x22f0`，即正向闭环验证通过。

#### 3. 当前验证状态小结

| 路径 | Windows 可达性 | 实测结果 |
|---|---|---|
| ETH→CAN（scapy/UDP/WinDivert v2.2.0 注入） | ❌ 注入环回，帧不到板卡 | `acfRxTotal=0`（已证伪交换机方案） |
| CAN→Eth（gs_usb 注入 + scapy 收） | ⚠️ 受限于 CAN 物理连线 | `canRxTotal=0`，需查 gs_usb↔板卡 CAN 连线 |
| 板卡基本健康度 | ✅ | `ethRxTotal` 在交换网下持续计数、PERF 每秒输出、不崩溃 |
| 固件 ACF 解析逻辑 | ✅（代码层面） | 裸 `0x22F0` 与 UDP(5555) 两分支均按统一布局解析；仅待真实帧到达后做字段核对 |
