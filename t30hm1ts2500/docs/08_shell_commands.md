# Shell 命令手册（所有平台通用）

上电后 115200 串口进入 letter-shell，提示符类似 `s2500>`。输入 `help` 查看命令列表，
`help <cmd>` 查看参数。所有命令共享同一套实现（`common/s2500/s2500_shell.c` 与 `s2500_app.c`），
因此 STM32/HPM/S32K/TC387 行为一致。

> **约定**：下面所有命令都在板端串口输入，也可以用
> `./tools/s25.py --auto <board> -c "<命令>"` 从主机发。
> **注意**：letter-shell 只认 **CR**（`\r`），用 `\r\n` 会报 `Command not Found`。

## 本篇目录

| 章节 | 内容要点 |
| --- | --- |
| 1 | 芯片信息与状态 |
| 2 | 寄存器直接访问 |
| 3 | PLCA |
| 4 | 诊断（SQI / HDD / TD） |
| 5 | 时间戳 / TC6 |
| 6 | 睡眠 / 唤醒 / TC10 |
| 7 | LED / 模拟参数 / 环回 |
| 8 | 网络与压测 |
| 9 | DIO 功能码 |
| 10 | 常用组合 |
| 11 | 命令速查表（一页版） |
| 12 | 常见错误与排查 |

---

## 1. 芯片信息与状态

| 命令 | 说明 | 示例输出 |
| --- | --- | --- |
| `s2500 info` | 版本、OUI、MAC、PHY 状态、PLCA、复位原因 | `IDVER : 0x00000011 (MAJVER=1 MINVER=1)` |
| `s2500 status` | 链路、PLCA、SQI、诊断位、驱动统计 | `link : UP` |
| `s2500 counters [clear]` | 35 个 MAC 统计计数器 | `TX: octets=... frames=...` |
| `s2500 irq` | PHY 中断状态（LSI/PSI/LJI/RJI/PRI/PCI/CRDI） | |
| `s2500 mac [xx:xx:xx:xx:xx:xx]` | 读/写本机 MAC | |

## 2. 寄存器直接访问

```
s2500 reg <mms> <addr> [value]
```

| 示例 | 作用 |
| --- | --- |
| `s2500 reg 0 0` | 读 IDVER（应为 0x11） |
| `s2500 reg 0 3 1` | 写 RESET 软复位 |
| `s2500 reg 1 0` | 读 MCONTROL0 |
| `s2500 reg 12 4096` | 读 ONICVER（0x1000） |

MMS：0=标准，1=MAC，2=PCS，3=PMA，4=PLCA/诊断，12=厂商。

## 3. PLCA

```
s2500 plca [id N] [ncnt N] [totmr N] [en 0|1] [cr 0|1] [prec 0|1]
```

| 命令 | 作用 |
| --- | --- |
| `s2500 plca id 3 ncnt 8 totmr 32 en 1` | 配置为跟随者 3 |
| `s2500 plca id 0 en 1` | 配置为协调者（全网只能一个） |
| `s2500 plca cr 1` | 打开协调者冗余（需 ncnt 与协调者一致） |
| `s2500 plca prec 1` | 优先级模式（低 ID 优先，全部节点需一致） |

配合 `s2500 status` 查看 `PST`（PLCA 活动中）、`CRPS`（代发 BEACON）、
`RXINTO`（ID 冲突）、`UNEXPB`（多协调者）、`BCNBFTO`（NCNT 不匹配）。

## 4. 诊断

| 命令 | 说明 |
| --- | --- |
| `s2500 sqi` | 读 SQI（0..7）与 SQI+（0..255），以及 DCQ_TOID |
| `s2500 hdd` | 运行 HDD 线束检测（需总线静默），输出开路/短路/单线短路 |
| `s2500 ts tx a|b|c [len]` | 发送时间戳捕获（置 FTSE → 发帧 → 读 TTSCx） |

| `s2500 td ref [dur]` | 作为参考节点做拓扑发现（⚠️ 封装命令在 DLYM_DONE 提前返回，距离阶段被掐断；要测距离请按 [`docs/results/2026-11-20_features.md`](results/2026-11-20_features.md) §2.3 手动写 TD_CTRL） |
| `s2500 td meas [dur]` | 作为被测节点 |
| `s2500 td dly [dur]` | 仅做内部延时测量 |

## 5. 时间戳 / TC6

| 命令 | 说明 |
| --- | --- |
| `s2500 time` | 读 PTP 时间（秒.纳秒） |
| `s2500 time set <sec> <nsec>` | 设置时间 |
| `s2500 ts a|b|c` | 读 TTSCA/B/C 发送时间戳 |
| `s2500 ts tx <a|b|c> [len]` | 置 FTSE + 发一帧带 TSC 的广播帧，等捕获后读 TTSCx（2026-11-20 新增；实测 TSC=1/2/3 **只落 TTSCC**） |

## 6. 睡眠 / 唤醒（TC10）

| 命令 | 说明 |
| --- | --- |
| `s2500 sleep` | 请求进入 Sleep（之后 SPI 不再响应，需唤醒音或 WAKE 引脚） |
| `s2500 sleep wake` | 发送全局唤醒音（需先置 LCTL=1） |

## 7. LED / 模拟参数 / 环回

| 命令 | 说明 |
| --- | --- |
| `s2500 led <fn0> <fn1>` | 设置 DIO0(黄 ACT)/DIO1(绿 LINK) 功能码，见 §9 |
| `s2500 led off` | 关闭两路 LED |
| `s2500 analog <amp> <edth> <edthcd> <slw> <clko> <cmc>` | 发射幅度/门限/斜率/时钟输出 |
| `s2500 loop 0|1|2` | 关闭 / PCS 环回 / PHY 模拟环回 |

## 8. 网络与压测

| 命令 | 说明 |
| --- | --- |
| `netif` | 显示 MAC/IP/网关/链路/PLCA ID |
| `ping <ip> [count]` | 板端发起 ICMP ping |
| `iperf -s` | 启动 TCP iperf 服务端（端口 5001） |
| `iperf -c <ip>` | 作为客户端发起 iperf |
| `udpecho on|off` | UDP 回显服务（端口 7） |
| `tcpstats` | lwIP 内存/pbuf/link 统计 |
| `reboot` | 软件复位 |
| `s2500 bench tx <count> [len]` | 直接经 MAC 发送测试帧，测驱动吞吐 |

## 9. DIO 功能码（Table 11）

| 码 | 功能 | 码 | 功能 |
| --- | --- | --- | --- |
| 0x0 | 关闭（高阻） | 0x8 | 25 MHz 时钟输出 |
| 0x1 | GPIO | 0x9 | 比较模块输出 |
| 0x2 | SFD-TX 脉冲 | 0xA | 捕获输入 |
| 0x3 | SFD-RX 脉冲 | 0xB | SFD RX&TX |
| 0x4 | LED Link Control | 0xC | BEACON 脉冲 |
| 0x5 | LED PLCA 状态 | 0xD | LED TX+BCN |
| 0x6 | LED TX | 0xF | LED TX&RX |
| 0x7 | LED RX | | |

> 本板 DIO0=黄 ACT（默认 0xF），DIO1=绿 LINK（默认 0x5）。高电平点亮，故 VAL=1。

## 10. 常用组合

```
# 单板自检
s2500 info
s2500 status
s2500 reg 0 0
s2500 counters clear

# 加入 PLCA 网络 (ID=3)
s2500 plca id 3 ncnt 8 totmr 32 en 1
s2500 status

# 诊断链路质量
s2500 sqi
s2500 hdd
s2500 td ref

# 压测
iperf -s
```

---

## 11. 命令速查表（一页版）

| 想干什么 | 命令 |
| --- | --- |
| 看所有命令 | `help` |
| 看某命令参数 | `help s2500` |
| 确认芯片在 | `s2500 reg 0 0` |
| 看芯片全貌 | `s2500 info` |
| 一键自检 | `s2500 selftest` |
| 看链路/PLCA/SQI | `s2500 status` |
| 看统计计数 | `s2500 counters`，清零加 `clear` |
| 看网络配置 | `netif` |
| 加入 PLCA 网络 | `s2500 plca id N ncnt 8 totmr 32 en 1` |
| 当协调者 | `s2500 plca id 0 en 1` |
| 开协调者冗余 | `s2500 plca cr 1` |
| 测信号质量 | `s2500 sqi` |
| 测线束 | `s2500 hdd` |
| 测距离 | `s2500 td ref` / `s2500 td meas` |
| 看时间戳 | `s2500 time` / `s2500 ts tx a 60` |
| 睡眠 | `s2500 sleep` |
| 发唤醒音 | `s2500 sleep wake` |
| 点灯 | `s2500 led 15 5` |
| PCS/PHY 环回 | `s2500 loop 1` / `s2500 loop 2` |
| ping | `ping 192.168.10.10 10` |
| iperf 服务端 | `iperf -s` |
| iperf 客户端 | `iperf -c 192.168.10.10` |
| UDP 回显 | `udpecho on` |
| 纯发送压测 | `s2500 bench tx 1000 500` |
| 芯片自愈看门狗 | `s2500 wd` / `s2500 wd on|off` / `s2500 wd reset`（默认开，2 s 周期） |
| 软件复位 | `reboot` |

### 11.1 各平台专属命令（2026-11-20 新增）

公共命令集（§1–§10）在所有平台一致；下面这些是各平台工程师加的**诊断命令**，只在该平台固件里存在。

| 平台 | 命令 | 作用 |
| --- | --- | --- |
| HPM5361 | `spi speed` | 打印 SPI1 实际时钟、分频、DMA 通道状态 |
| HPM5361 | `spi dma` | 对比 DMA / 阻塞式读 IDVER，确认 DMA 生效 |
| HPM5361 | `spi bench` | 68B 原始事务往返耗时（µs/块） |
| HPM5361 | `spi reg / spi loop / spi selftest` | 单寄存器读写、环回、自检 |
| TC387 | `tcqspi` | f_SPB / ECON / BACON / 实际波特率 / DMA 与中断计数 |
| TC387 | `tcspeed` | 68B 原始交换耗时（实测 27.0 µs/块 = 20.1 Mbit/s 总线） |
| STM32H723 | `spi` | SPI1 时钟、DMA 状态、12B 事务耗时 |
| STM32H723 | `bench` | UDP 发送 bench（不依赖 ping 通路） |
| S32K312 | `spi` | LPSPI0 时钟/状态、12B 事务耗时（`s32k hw on|off` 切换后端） |
| RK3506 | sysfs `/sys/class/net/t1s0/` | `plca_id` / `plca_enable` / 统计（无 shell 命令集） |

> 平台命令的实现分别在 `<board>/src/diag.c` 或对应 board 文件里；
> 公共命令永远在 `common/s2500/s2500_shell.c`。

## 12. 常见错误与排查## 13. 相关章节

| 想看什么 | 去哪里 |
| --- | --- |
| 命令背后的寄存器 | [`docs/04_register_map.md`](04_register_map.md) |
| 完整测试例子 | [`docs/11_test_examples.md`](11_test_examples.md) |
| PLCA 原理 | [`docs/13_10baset1s_primer.md`](13_10baset1s_primer.md) |
| 串口设备与调试器 | [`docs/09_debug_env.md`](09_debug_env.md) |
