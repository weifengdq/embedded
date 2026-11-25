# eth_freertos_lwip —— FreeRTOS 10.5.1 + lwIP 2.1.2(NO_SYS=0) + MCAL Eth

把 lwIP 从裸机（NO_SYS=1）搬到 FreeRTOS 上：自写 sys_arch（`components/freertos_lwip_port/`），
以太网收发仍走 **Eth MCAL**（Eth + EthTrcv）。这是第 49 章 §49.8 的配套例程，与
`samples/mcal/eth_lwip` 共用同一份 netif 适配层（`components/app_lwip_eth/`），
所以两者的差别只来自 RTOS 这一层（多一次 `tcpip_input` 投递、一把互斥量、一个 1 kHz tick）。

- 板：`192.168.2.100/24`；主机 `enp6s0 = 192.168.2.1/24`；iperf 用 **v2**（lwiperf 是 iperf2 协议）
- 任务：`ethrx`(6) 收包 / `tcpip`(5) 协议栈 / `app`(4) 初始化 / `ethmon`(2) 统计 / `shell`(1) / IDLE(0)

## 1 构建与烧写

```bash
cd hpm5e31_1
bash scripts/build.sh samples/mcal/eth_freertos_lwip
# 短跑（看启动、链路、上电自检 ping）
bash scripts/hwrun.sh samples/mcal/eth_freertos_lwip --secs 15 --lock-wait 1800
# 320 s 长测：一次持锁里烧写 + 跑完（不要用 --no-flash，避免板上是别人的固件）
bash scripts/hwrun.sh samples/mcal/eth_freertos_lwip --secs 320 --lock-wait 1800 \
  --send "sleep 3000;eth;eth iclient 192.168.2.1 udp"
```

启动打印（日志里 banner 对不上就作废重跑）：

```
=== mcal_eth_freertos_lwip: FreeRTOS V10.5.1 + lwIP 2.1.2 + MCAL Eth ===
[init] Eth MCAL v1.0.0
[init] lwIP sys_arch: components/freertos_lwip_port (自写移植)
[lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
[init] app_lwip_eth_init rc=0 (0=link up, 1=link down)
```

## 2 串口命令

| 命令 | 作用 |
|---|---|
| `eth ...` | 组件自带命令（`eth` / `eth ping` / `eth iserver` / `eth iclient` / `eth istop`） |
| `ethprio <0..7>` | 改收包任务优先级（优先级-延迟实验用） |
| `ethload <prio> <ms>` | 起一个指定优先级的忙任务压 CPU，到点自删；`ethload stop` 提前停 |
| `ethrt` | CPU 占用 / FreeRTOS 堆余量 / sys_arch 统计 / 任务栈水位 / 网络统计 |
| `sleep <ms>` | `vTaskDelay`，脚本化测试用 |

## 3 实测数据（真实日志）

### 3.1 sys_arch 原语开销

来源 `logs/20261102-153226-rtos_lwip_selftest.log`（无以太网的裸 RTOS 自检固件，480 MHz、rdcycle 测量）：

| 原语 | cycle | µs | 说明 |
|---|---|---|---|
| `SYS_ARCH_PROTECT`+`UNPROTECT` | 63.03 | 0.131 | lwIP 内存/PCB 热路径 |
| 临界区本身（平均/最大） | 25.02 / 1 234 | 0.052 / 2.57 | 最大值是被 tick 顶住的那次 |
| `sys_sem` give+take | 192.13 | 0.400 | 同任务不阻塞 |
| `sys_mbox` trypost+tryfetch | 3 917.09 | 8.16 | 偏高，见第 5 节易错点 2 |
| 跨任务 sem 乒乓（含 2 次切换） | 7 807.4 | 16.27 | ≈8.1 µs/次上下文切换 |

同日志：1 kHz tick 采样空闲率 **99.6%**；`tcpip` 栈水位 948/1 024 字；`malloc` 失败 0、栈溢出 0。

### 3.2 以太网（`logs/20261102-164203-eth_freertos_lwip.log`）

| 指标 | 结果 |
|---|---|
| 链路 | `link=UP`、1000 Mbit/s、Slave，PHY `BMCR=0140 BMSR=000D GBSR=3000 PHYSR=8400` |
| 上电自检 ping 网关 | 2/5 收到，RTT **1015/1015/1016 µs**（抖动 1 µs） |
| 说明 | RTT 被 1 ms tick 量化：ping 等待循环用 `vTaskDelay(1)`，所以分辨率就是 1 tick；
|  | 裸机版同一块板是 136~383 µs。要测亚毫秒 RTT 应把等待改成 `taskYIELD()`/信号量 |
| 丢包 | 40~67%，与裸机版同源（MCAL Tx 侧问题，见第 49 章 §49.12 #1） |

### 3.3 320 s 长测

长测命令见 §1（`--secs 320`，一次持锁内完成烧写与运行）。日志取 `logs/` 下最新的
`*-eth_freertos_lwip.log`，判据是 banner 与 `link=UP` 两行都对得上。要看的三类数：
`[rtos] cpu: idle=...`（CPU 占用）、`freertos_lwip_port_stats_print`（sys_arch 调用计数与
临界区最长关中断时间）、`freertos_lwip_port_print_task_stack`（各任务栈水位）。

> 本轮实测：长测命令已就绪，但上板时段与其它例程抢整机锁（4 个 hwrun 排队），
> 320 s 长测未能完成归档，属"已知限制 + 可复跑"，不编数据。

## 4 与裸机版 / 第 13 章基线的对照

| 指标 | SDK enet 驱动（第 13 章） | MCAL + 裸机 lwIP | MCAL + FreeRTOS lwIP |
|---|---|---|---|
| 上行吞吐 | TCP 466 / UDP 641 Mbit/s | UDP 92.0 Mbit/s（lwIP 计数，`tx_busy=9981`） | 待长测 |
| ICMP RTT | — | 136~383 µs | 1015/1016 µs（tick 量化后） |
| 单帧发送 | 756 cycle（描述符直指 pbuf） | 36 µs 上限（含整帧 memcpy） | 同量级 + 互斥量 |
| 空闲 CPU | — | 忙轮询 | 99.6% 空闲（0.4% 占用） |

## 5 易错点

1. **`sys_msleep` 重复定义**：lwIP 2.1.2 的 `src/core/sys.c` 已实现，移植层再写会在链接期
   报 `multiple definition of sys_msleep`。只保留 `sys_now()/sys_jiffies()`。
2. **`sys_mbox` trypost+tryfetch 3 917 cyc/对**（比 sem 高 20 倍）：已登记复查，怀疑测量窗口
   被 1 kHz tick 中断摊薄；不影响功能。
3. **不开 `CONFIG_LWIP_NETCONN_API` 就没有 `tcpip_init()`**（SDK 条件编译），报错是链接期
   `undefined reference to tcpip_init`。
4. **`LWIP_TCPIP_CORE_LOCKING=1` 必须开**；命令里调 raw API 要用 `LOCK_TCPIP_CORE()`。
5. **收包任务不能让 CPU**：有包时连续排空，连续 64 次空转才 `vTaskDelay(1)`；1 Gbit/s 下
   16 个 Rx 缓冲不到 1 ms 就填满。
6. **`--no-flash` 长测有串味风险**：先看 banner 是不是本例程，再引用日志（板上可能是别人的固件）。

## 6 复跑命令

```bash
# 短跑：启动 + 链路 + 上电自检
bash scripts/hwrun.sh samples/mcal/eth_freertos_lwip --secs 15
# CPU / 栈水位 / sys_arch 统计
bash scripts/hwrun.sh samples/mcal/eth_freertos_lwip --secs 12 --send "sleep 2000;ethrt"
# 优先级实验：ethrx 降到 4，加一个优先级 7 的忙任务，再 ping
bash scripts/hwrun.sh samples/mcal/eth_freertos_lwip --secs 45 \
  --send "sleep 2000;ethprio 4;eth ping 192.168.2.1 10 200;ethload 7 15000;eth ping 192.168.2.1 10 200;ethrt"
# 320 s 长测
bash scripts/hwrun.sh samples/mcal/eth_freertos_lwip --secs 320 --send "sleep 3000;eth;eth iclient 192.168.2.1 udp"
```
