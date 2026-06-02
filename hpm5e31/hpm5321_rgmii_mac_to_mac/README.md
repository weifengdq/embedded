# hpm5321_rgmii_mac_to_mac

## 1. 项目定位
本工程用于 HPM5E31 双板 RGMII MAC-to-MAC 直连验证，目标是提供一个可重复下载、可自动化测试的 100Mbps 固定链路基线。

当前发布基线策略：
- SDK 侧 lwIP iperf 源码已恢复为 HPM SDK v1.11.0 官方原版。
- 工程侧保持 100Mbps fixed-link、RGMII delay 0/0、ENET ring 30/30。
- 默认 UDP 客户端速率下调为 10Mbps 档，优先保证双向稳定回归。

## 2. 工程架构

### 2.1 目录结构
- `boards/hpm5e31_lite/`：板级初始化、时钟、引脚、RGMII 延时配置。
- `common/single/`：ENET fixed-link 初始化与轮询驱动主流程。
- `ports/baremetal/single/`：lwIP baremetal 适配层（`ethernetif.c`）。
- `src/lwip.c`：应用入口、菜单、iperf 启动与 report 打印。
- `inc/common_cfg.h`：ENET ring 与 buffer 参数。
- `tools/serial_iperf_test.ps1`：双串口自动测试脚本。
- `build.ps1`：configure/build/flash/gdbserver 一体脚本。

### 2.2 角色与地址
- 角色 A：`192.168.10.13`
- 角色 B：`192.168.10.61`

编译时通过 `-Role A|B` 切换。

### 2.3 链路配置
- fixed-link：100Mbps full duplex
- RGMII TX/RX delay：0/0
- ENET ring：TX=30, RX=30

## 3. 环境与依赖
- OS：Windows
- SDK：`D:/hpm/embedded/hpm5e31/sdk_env_v1.11.0/hpm_sdk`
- J-Link：`C:/Program Files/SEGGER/JLink_V844`

双板映射（本仓库验证环境）：
- A 板：J-Link SN `24060502`，串口 `COM13`
- B 板：J-Link SN `1120000012`，串口 `COM61`

## 4. 如何构建与下载

### 4.1 构建
在工程根目录执行：

```powershell
./build.ps1 -Action build -Config Debug -Role A
./build.ps1 -Action build -Config Debug -Role B
```

### 4.2 下载（推荐按 SN 精准烧录）
`build.ps1` 已支持 `-JLinkSerial`，双板并行时必须显式指定：

```powershell
./build.ps1 -Action flash -Config Debug -Role A -JLinkSerial 24060502
./build.ps1 -Action flash -Config Debug -Role B -JLinkSerial 1120000012
```

## 5. 如何测试

### 5.1 UDP 自动测试

```powershell
./tools/serial_iperf_test.ps1 -Protocol udp -ClientBoard A -OutputPath C:/Windows/Temp/release_udp_A_to_B_10m.txt
./tools/serial_iperf_test.ps1 -Protocol udp -ClientBoard B -OutputPath C:/Windows/Temp/release_udp_B_to_A_10m.txt
```

### 5.2 TCP 自动测试

```powershell
./tools/serial_iperf_test.ps1 -Protocol tcp -ClientBoard A -OutputPath C:/Windows/Temp/release_tcp_A_to_B_10mcfg.txt
```

## 6. 当前测试结果（2026-06-02）

### 6.1 UDP（发布基线，10Mbps 配置）
- A -> B：PASS
  - `type=7`, `total_bytes=14974008`, `duration_ms=10501`, `kbits_per_s=11407`
- B -> A：PASS
  - `type=7`, `total_bytes=14974008`, `duration_ms=10501`, `kbits_per_s=11407`

对应日志文件：
- `C:/Windows/Temp/release_udp_A_to_B_10m.txt`
- `C:/Windows/Temp/release_udp_B_to_A_10m.txt`

### 6.2 TCP（当前状态）
- A -> B：未通过（超时）
  - `type=2`, `total_bytes=7324`, `duration_ms=9762`, `kbits_per_s=0`

对应日志文件：
- `C:/Windows/Temp/release_tcp_A_to_B_10mcfg.txt`

## 7. 发布结论
- 已完成 SDK 恢复：`lwiperf.c` 回到官方 v1.11.0 原版。
- 已完成工程回稳：给出可重复构建/下载/测试流程，并固化双向 UDP 稳定配置。
- TCP 方向仍存在已知问题，暂不作为本版本发布通过项。

## 8. 后续建议
- 在保持 SDK 原版前提下，优先定位 TCP 控制路径（SYN/SYN-ACK/ACK）阶段性丢失或状态推进异常。
- 如需要量产前 TCP 通过，建议建立独立 `debug` 分支做最小探针，不污染当前发布基线。
