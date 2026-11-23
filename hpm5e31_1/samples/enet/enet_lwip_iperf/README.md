# enet_lwip_iperf —— YT8011AN 1000BASE-T1 + lwIP 2.1.2 + iperf2 吞吐测试

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 1. 功能

在 hpm5e31_1 上跑 **lwIP 2.1.2**（baremetal、轮询、零拷贝 RX），通过 YT8011AN（1000BASE-T1）
与主机千兆网卡对打 **iperf2** 流量；并提供 netif/PHY 状态、CPU 占用、驱动层发送微基准、L1 cache 状态等诊断命令。

协议栈与网卡适配层基于 HPM SDK v1.13.0，本样例额外提供 2 个文件做本板适配与**发送路径优化**：

| 文件 | 作用 |
|---|---|
| `src/phy_glue.c` | 把 YT8011 驱动接到 lwIP 移植层的 3 个板级钩子（不启用 SDK 的 CONFIG_ENET_PHY） |
| `src/common_fast.c` | SDK `samples/lwip/common/single/common.c` 的副本，仅增加 TX 路径 D-cache 回写裁剪（见第 6 节） |

## 2. 网络拓扑

```
host enp6s0 (192.168.2.1/24, 1000Mb/s Full)
   │  RJ45
   ▼
1000BASE-T1 ↔ RJ45 转换盒（拨码 1000M Master）
   │  单对差分线
   ▼
YT8011AN (1000BASE-T1, Slave, PHYADDR=1) ── RGMII ── HPM5E31 ENET0 (192.168.2.100/24)
```

## 3. 编译/运行

```bash
cd hpm5e31_1
bash scripts/build.sh enet/enet_lwip_iperf
# 上行（板→主机）：主机先起 iperf2 服务端，板端 12 s 后发起客户端
bash scripts/hwrun.sh enet/enet_lwip_iperf --secs 45 --send 'cacheinfo;link;sleep 12000;iclient 192.168.2.1 tcp'
# 下行（主机→板）
bash scripts/hwrun.sh enet/enet_lwip_iperf --secs 45 --send 'cacheinfo;ip;link;iserver tcp'
# 主机侧
ip addr add 192.168.2.1/24 dev enp6s0; ip neigh flush dev enp6s0
iperf -s -p 5001 -t 30                 # 上行时用
iperf -c 192.168.2.100 -p 5001 -t 10   # 下行时用
```

## 4. shell 命令

| 命令 | 说明 |
|---|---|
| `ip` | netif 名/序号、MAC、IP/掩码/网关、link |
| `link` | PHY 链路状态（link/速率/主从/PHYSR/GBSR/BMSR）+ netif link |
| `iserver [tcp\|udp]` | 启动 iperf2 服务端（5001） |
| `iclient <ip> [tcp\|udp]` | 启动 iperf2 客户端 |
| `istop` | 中止会话 |
| `cpu` | 打印上次调用以来的 CPU 占用（非阻塞，主循环累计 mcycle） |
| `sleep <ms>` | 阻塞延时，供 `--send 'sleep 12000;iclient ...'` 脚本化测试 |
| `txbench [frames] [len]` | 驱动层直发微基准：绕过 lwIP，测 ENET TX+DMA 上限与每帧周期 |
| `cacheinfo` | 打印 `MCACHE_CTL`（DC/IC 使能、write-around 模式） |
| `mode <1000\|100>` | 重配 PHY 速率（配合转换盒拨码做 1000M/100M 对比） |

iperf 运行期间固件每 5 s 自动打印 `[cpu] busy=...%`。

## 5. 当前配置

| 项 | 值 | 说明 |
|---|---|---|
| lwIP 选项 | SDK `samples/lwip/lwip_iperf/inc/lwipopts_app.h` 默认 | SND_BUF 20×MSS、SEG 40、MEM 32 KB、PBUF_POOL 30 |
| ENET TX/RX 描述符 | 30 / 30（SDK `common_cfg.h`） | TX 环加到 64 实测无收益 |
| TX 校验和 | ENET 硬件插入（`-DCHECKSUM_GEN_*=0`） | 实测对吞吐无影响，保留以省 CPU |
| TX D-cache 回写 | **跳过**（`-DNET2_TX_WB_SKIP_ALL=1`） | 前提：`cacheinfo` 显示 write-around=3；若为 write-back 请去掉该宏 |
| DLM 占用 | 127424 B / 128 KB（97.2%） | 余量很小，加缓冲前必须先腾空间 |
| 主机工具 | iperf2 2.2.1（端口 5001） | **不是 iperf3** |

## 6. 发送路径优化（2026-10-30 第二轮，详见第 13 章 6.3 节）

**诊断证据**（`txbench` / `cacheinfo`）：

| 测量 | 结果 | 结论 |
|---|---|---|
| `txbench 20000 64` | 634,875 pkt/s，756 cycles/帧 | ENET 驱动固定 CPU 开销 ~756 cycles/帧 |
| `txbench 20000 1460` | 74,460 pkt/s，**869.7 Mbit/s**，6,446 cycles/帧 | 驱动+DMA 上限 ≈870 Mbit/s，不是驱动能力问题 |
| `cacheinfo` | `MCACHE_CTL=0x00007F03`，write-around=0b11 | 写直达内存，TX 回写可以跳过 |

**优化项与结果（板端统计）**：

| 优化 | TCP 上行 | UDP 上行 | 结论 |
|---|---|---|---|
| 基线 | 415.3 Mbit/s | 493 Mbit/s | — |
| 关闭软件 TX 校验和（ENET 硬件插入） | 415.3 | — | 无变化，校验和不是瓶颈 |
| lwiperf 发送缓冲 Flash→RAM | 390.4 | — | 无变化，排除 XIP DMA 假设 |
| SND_BUF 40×MSS + TX 环 64 | 367 | 494 | 无收益 |
| RX 缓冲降到 12 | 266 | 479 | 回退：ACK 接收受影响 |
| **跳过 TX D-cache 回写（最终）** | **466.3（+12.3%）** | **641.5（+30.1%）** | 有效，最终采用 |

**结论**：物理层（UDP 下行 944）、ENET 驱动/DMA（txbench 870）、校验和、数据源位置都不是瓶颈；
TCP 上行剩余瓶颈是 **lwIP 每段处理**（约 12,000 cycles/1460 B 段，驱动仅 ~750）。
要突破 700 Mbit/s 需要 Jumbo Frame（受 128 KB DLM 限制）、TCP 发送泵批处理或硬件 TSO（本芯片无）。

## 7. 实测现象（2026-10-30 最终配置）

串口日志：`logs/20261030-153556-enet_lwip_iperf.log`（下行）、`logs/20261030-153645-enet_lwip_iperf.log`（TCP 上行）、
`logs/20261030-153733-enet_lwip_iperf.log`（UDP 上行）

```
=== enet_lwip_iperf: YT8011AN(1000BASE-T1) + lwIP + iperf2 ===
lwIP 2.1.2
[phy] YT8011AN link up: 1000M SLAVE
[net] netif  : en (index=1)
[net] IP     : 192.168.2.100
[cache] MCACHE_CTL=0x00007f03 dc_en=1 ic_en=1 writearound=3
[phy] link=UP speed=1000M role=SLAVE PHYSR=8400 GBSR=3000 BMSR=000D thread=0
```

| 项目 | 结果（板端统计） |
|---|---|
| ping 20×56B | 20/20，0% 丢包 |
| TCP 下行 | **902.3 Mbit/s**（CPU 33.3%） |
| TCP 上行 | **466.3 Mbit/s**（CPU 99.7%） |
| UDP 下行 | 944 Mbit/s 发送，丢包 ≤0.25% |
| UDP 上行 | **641.5 Mbit/s**，丢包 0.017% |

> 说明：下行吞吐在 660~935 Mbit/s 之间波动，主要受主机侧负载影响（同机还有其他构建/测试任务）；
> 上行受板端 CPU 限制更稳定。

## 8. 坑

1. **lwiperf 是 iperf2 协议（端口 5001）**，iperf3 打不通，主机必须用 `iperf`（v2）。
2. **UDP 客户端必须每轮调用 `lwiperf_poll_udp_client()`**，否则只发首包（本样例已加）。
3. **不要打开 `CONFIG_ENET_PHY`**：会与 `phy_glue.c` 的同名钩子冲突。
4. **静态 IP 用 `-DIP0_CONFIG=...` 注入**：SDK 的 `lwip_iperf/inc/common_cfg.h` 与样例同名，
   `lwipopts_app.h` 用同目录优先，会静默覆盖样例里的定义。
5. **RX 缓冲不能太小**：降到 12 时 TCP 上行从 415 掉到 266（ACK 接收受影响）。
6. **DLM 只有 128 KB**：TX/RX 缓冲、pbuf pool、lwIP 堆都在这，改大任何一项都要先腾空间。
7. **跳过 TX 回写的前提是 write-around 已开启**：`cacheinfo` 显示 `MCACHE_CTL[14:13]=0b11` 才安全；
   若被改成 write-back，请去掉 CMake 里的 `-DNET2_TX_WB_SKIP_ALL=1`（`common_fast.c` 会退回只跳过 Flash/non-cacheable）。

## 9. 参考

- `hpm_sdk/samples/lwip/lwip_iperf/`、`samples/lwip/ports/baremetal/`、`samples/lwip/common/single/`
- `hpm_sdk/middleware/lwip/src/apps/lwiperf/lwiperf.c`
- `hpm_sdk/arch/riscv/l1c/hpm_l1c_drv.c`（`l1c_op` 的 CCTL 实现）
- 第 12 章（YT8011AN PHY/RGMII）、第 13 章（lwIP 移植与性能，含 6.3 发送路径优化）