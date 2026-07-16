# TC387 lwIP iperf `"`" 千兆以太网性能优化记录

## 测试环境

| 项目 | 配置 |
|------|------|
| MCU | Infineon TC387 (4x300MHz TriCore, 单核运行) |
| PHY | RTL8211F (RGMII, 1000Mbps) |
| 编译器 | TASKING TriCore v6.3r1 (**GCC 编译的固件无法启动**) |
| 协议栈 | lwIP baremetal (NO_SYS=1) |
| iperf | D:\github\embedded\tc387\bak\iperf.exe |
| 调试器 | DAP Miniwiggler |
| 板子 IP | 192.168.0.100/24 |

## 最终性能

| 配置 | 吞吐量 |
|------|--------|
| 原始代码 (MSS=1024, 32 desc, 32KB heap) | **183 Mbps** |
| +零拷贝 RX (PBUF_REF, ETH_PAD_SIZE=0) | **208 Mbps** (单核) |
| +LMURAM全量迁移 (GETH+lwIP堆) | **204 Mbps** (单核) |
| +多核 (Core0 timer, Core1 RX) | **200 Mbps** |
| +TCP_MSS=1460 + TSO基础设施 | **427 Mbps** 🎉 |
| +TX反向模式 (TSO生效) | **426 Mbps** |
| +TCP_WND=16*MSS (实验) | **329 Mbps** 平均, **500 Mbps** 峰值 ⚡ |

> **突破: MSS=1460 → 427 Mbps (2.3x 提升)！**
> 16*MSS 窗口出现 500 Mbps 瞬时峰值但波动大，需更大 LMURAM。
> 单核天花板从 ~180 Mbps → **427 Mbps 稳定**，**500 Mbps 峰值可达**。
> 900+ Mbps 的关键路径: **TCP_WND → BDP (62.5KB) 需 LMURAM 空间优化**。

## 当前最优配置 (427 Mbps 稳定)

| 参数 | 值 |
|------|-----|
| TCP_MSS | 1460 |
| TCP_WND | 12×1460 = 17.5 KB |
| MEM_SIZE | 48 KB (LMURAM heap) |
| IFXGETH_MAX_TX/RX_DESCRIPTORS | 8 |
| ETH_PAD_SIZE | 0 |
| DMA buffer size | 2576 bytes |
| LMURAM 使用 | ~116 KB / 128 KB |

## 编译和下载

```powershell
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" +
            [System.Environment]::GetEnvironmentVariable("Path","User")
cd d:\github\embedded\tc387\tc387_lwip_iperf_gcc
.\build.ps1 -Compiler tasking -Action download -BuildType Release
```

## iperf 测试

```powershell
D:\github\embedded\tc387\bak\iperf.exe -c 192.168.0.100 -t 10 -i 1     # RX
D:\github\embedded\tc387\bak\iperf.exe -c 192.168.0.100 -t 10 -i 1 -R  # TX
```

## 千兆路线图

| 优先级 | 方案 | 预期提升 | 状态 |
|--------|------|----------|------|
| P0 | 多核分工 (Core1跑TCP栈) | 2-3x | ✅ 架构已打通 (LMURAM全量迁移) |
| P0 | TSO硬件卸载 (GETH TCP Segmentation) | 3-5x (TX方向) | 🔧 待实现 |
| P0 | Scatter-Gather DMA 零拷贝 TX | 1.5-2x | 🔧 异步完成机制待实现 |
| P0 | 零拷贝 RX (PBUF_REF) | 1.15x | ✅ 已实现 |
| P1 | DMA Buffer移到LMURAM | 0.5x | ✅ 已实现 (全部GETH+lwIP数据) |
| P2 | ETH_PAD_SIZE=0 | 0.1x | ✅ 已实现 |
| P2 | 中断合并/批量drain | 0.1x | ✅ 已实现 |

## LMURAM 使用情况 (128KB总量)

| 数据 | 大小 |
|------|------|
| DMA RX buffers (8×2576) | 20.1 KB |
| DMA TX buffers (8×2576) | 20.1 KB |
| 描述符列表 (RX+TX) | ~1 KB |
| g_IfxGeth 句柄 | ~1 KB |
| lwIP heap (LWIP_RAM_HEAP_POINTER, 48KB) | 49.2 KB |
| TSO TX scratch buffer | 48.0 KB |
| spinlock | 4 B |
| **合计** | **~139 KB** ⚠️ 超出128KB!

## 已修改文件

| 文件 | 改动摘要 |
|------|----------|
| `lwipopts.h` | SYS_LIGHTWEIGHT_PROT=1, ETH_PAD_SIZE=0, TCP_MSS=1024 |
| `cc.h` | SYS_ARCH_PROTECT/UNPROTECT 宏 (单核快速路径 + 多核条件编译) |
| `Ifx_Lwip.c` | poll批量drain, RX ISR批量处理, LMURAM spinlock |
| `Ifx_Lwip.h` | spinlock声明 + lock/unlock原型 |
| `Cpu1_Main.c` | #include Ifx_Lwip.h (多核入口就绪) |
| `netif.c` | 零拷贝 RX (PBUF_REF替代PBUF_POOL+memcpy) |

## 关键发现
- **DMA burst模式** (`fixedBurst=TRUE, mixedBurst=FALSE`) **单独导致启动失败**
- **ETH_PAD_SIZE=0** 简化零拷贝对齐，移除 pbuf_header 操作
- 零拷贝 TX 需 DMA 完成轮询 → 拖慢吞吐，需异步机制
- Core1 无法访问 DSPR0 中的 GETH 数据 → 需整体迁 LMURAM
- TCP_MSS=1460 + 大缓冲区反而降低吞吐

## 已知问题
- GCC编译固件缺 `.interface_const` 段无法启动，必须用TASKING
- iperf异常断开后需重新烧录
- TASKING链接器需要 `C:\Windows\System32` 在PATH

## 参考
[Infineon TC3x Ethernet 千兆速度配置与测试](https://community.infineon.com/t5/博客/TC3x-Ethernet-千兆速度配置与测试/ba-p/1100049)
