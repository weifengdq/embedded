# samples/app/can_gateway —— cannelloni v2 网关 + CAN0 监视/回显节点

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

第 28 章「应用层测试台」的主例程：把板端 CAN 总线"搬"到主机 `vcan0`，让上位机工具
（python-can / candump / cansend / udsoncan / canopen / pyxcp）直接测真实 CAN 节点。

## 1. 硬件连接

```
主机 enp6s0 192.168.2.1/24  <--千兆车载以太网盒-->  YT8011AN 板 192.168.2.100/24
                                                        |
                                                   ENET0 / lwIP
                                                        |
   vcan0 <--UDP 4000 (cannelloni)--> CAN1(MCAN1, PD05/PD04, 500k) === 板内总线 === CAN0(MCAN0, PD00/PD01, 500k)
                                                                                    |
                                                                             DUT 演示节点
```

- CAN0 与 CAN1 在板上**物理相连**（两端各 120 Ω，见 `docs/应用层开发约定.md` §2），
  所以主机往 vcan0 发的帧会出现在 CAN0，CAN0 发的帧也会回到 vcan0；
- CAN2/CAN3 是另一对（PC08/PA09、PC15/PC14），`gw full` 时映射到 vcan2(4002)/vcan3(4003)。

## 2. 板端功能

| 功能 | 说明 |
|---|---|
| CAN0 心跳 | 每 100 ms 发一帧 `0x321`，载荷 `[seq u32 LE][CA FE BA BE]` |
| CAN0 回显 | 收到 `0x123` 时把**同样的载荷**以 `0x321` 发回（主机 `cansend vcan0 123#1122334455667788` 会看到 `321#1122334455667788`） |
| CAN0 监视 | 收到的每帧打印到串口（`quiet` 关闭，长测时建议关） |
| 网关 | CAN1 <-> UDP 4000（默认）；CAN2/CAN3 -> 4002/4003（`gw full`） |
| 自检 | 启动时 CAN3 内部环回自测（`loop` 可重跑） |
| 存活 | 每 5 s 打印 `[alive]` 行（长测时判断板子是否还活着） |

## 3. 串口命令

| 命令 | 说明 |
|---|---|
| `info` | 打印配置摘要 |
| `pass` | 重新打印 PASS/FAIL 结论行（抓串口错过上电输出时用） |
| `stat` | CAN0 统计 + 网关统计 + 心跳/回显计数 + peer/link |
| `gw on` / `gw off` | 打开/关闭转发（不发 UDP，但仍收） |
| `gw stat` | 网关统计（udp rx/tx、can rx/tx、drop、bad） |
| `gw full` / `gw lite` | 三路映射（CAN1/2/3）/ 仅 CAN1 |
| `gw peer <ip> <port>` | 固定对端（默认从主机第一个包学习） |
| `echo on\|off` / `hb on\|off` | 回显 / 心跳开关 |
| `send <id> <hex>` | 从 CAN0 发一帧，例 `send 123 1122334455667788` |
| `loop` | CAN3 内部环回自测 |
| `ip` | lwIP netif + PHY 状态 |
| `quiet` / `verbose` | 关/开逐帧打印 |
| `cantest` | 逐项重建 CAN 节点并打印每一步诊断（定位 mcan_init 失败/时钟/环回问题） |

## 4. 主机侧三步启动（host/can_bridge/）

```bash
cd host/can_bridge
sudo ./vcan_setup.sh                                 # 1) vcan0 + 500kbit tbf 限速
./build_cannelloni.sh                                # 2) 只做一次：构建 stock cannelloni
./run_tunnel.sh &                                    # 3) vcan0 <-> 192.168.2.100:4000
candump vcan0                                        # 应看到 10 Hz 的 321#...CA FE BA BE
cansend vcan0 123#1122334455667788                   # 应看到 321#1122334455667788
./../..//host/can_bridge/bench_gateway.py            # 延迟/丢包/心跳三项验收
```

没有 cannelloni 时可用纯 Python 备用桥：`./udp2vcan.py --iface vcan0`。

## 5. 上板命令

```bash
cd hpm5e31_1
bash scripts/hwrun.sh samples/app/can_gateway --secs 45 --send "quiet;gw on;pass" --lock-wait 1800
```

启动打印（示例）：

```
[boot] can_gateway: cannelloni v2 CAN<->UDP gateway + CAN0 demo node
[can0] init OK rc=0 (OK)
[loop] CAN3 loopback PASS rc=0 (OK)
[lwip] IP=192.168.2.100 MASK=255.255.255.0 GW=192.168.2.1 link=UP
[gw] ch1: CAN1 <-> UDP 4000 (local 0.0.0.0)
[pass] CAN0=OK CAN3-loop=PASS lwip=OK link=UP gw=CAN1/UDP4000 rc=0
```

## 6. 实测（第 28 章数据来源）

见 `docs/chapters/29-*.md` 的"上板实测"节与 `logs/*can_gateway*.log`、
`host/can_bridge/README.md` 的真实运行输出。关键指标：ping 通、心跳 10 Hz、`0x123` 回显、
往返延迟 avg/p99、300 帧/s 丢包率。

## 7. 易错点

| # | 现象 | 原因 | 处理 |
|---|---|---|---|
| 1 | 上电后主机 `candump` 什么都收不到 | 板端"自动学习 peer"还没学到主机地址；或网线/以太网盒没起来 | 先 `ping 192.168.2.100`；再 `cansend vcan0 123#00` 触发学习；或 `gw peer 192.168.2.1 4000` |
| 2 | `cansend` 后看不到回显 | 板端 `echo off` 或 CAN0 的 0x321 被网关漏掉（UDP 丢包） | `stat` 看 `rx0` 是否增加；`gw stat` 看 `udp_tx` |
| 3 | 串口刷屏导致高帧率下丢帧 | 逐帧 `printf` 阻塞主循环 | 先发 `quiet` |
| 4 | `gw full` 之后 CAN2/CAN3 没数据 | 主机没建 vcan2/vcan3，或 cannelloni 没加 `-I vcan2` 第二实例 | `sudo CHANNELS="0 2 3" ./vcan_setup.sh`，再为每路起一个 cannelloni |
| 5 | 板子上电死在 `[boot]` 前 | 复位瞬间按住 PA03 会进 ISP | 松开按键再复位（第 1 章） |

## 8. 文件

```
samples/app/can_gateway/
├── CMakeLists.txt
├── src/main.c        # CAN0 演示节点 + 网关 + shell 命令
└── README.md
```

## 9. 上板实测（2026-10-30 17:43，`logs/20261030-174321-can_gateway.log`）

| 项目 | 实测 | 判定 |
|---|---|---|
| 板端自检 | `[pass] CAN0=OK CAN3-loop=PASS lwip=OK link=UP gw=CAN1/UDP4000 rc=0` | PASS |
| ping 192.168.2.100 | 5/5，0% 丢包，rtt 0.129/0.231/0.458 ms（min/avg/max） | PASS |
| 心跳 0x321 | 20 帧 / 2 s = 10 Hz | PASS |
| 回显 0x123→0x321 | 载荷逐字节一致 | PASS |
| 往返延迟（`-t 1000`） | avg 2.24 ms / p50 2.27 / p99 2.48 / max 2.48 ms | PASS |
| 300 帧/s × 5 s 丢包 | 1500/1500，0.00% | PASS |

主机侧日志：`/tmp/can_bridge_accept/{ping,candump_hb,candump_echo,bench}.log`、`bench.json`；
章节证据：第 28 章 表 28-5 / 表 28-6 / 图 28-3 / 图 28-4。

> **易错点速查**：① vcan 必须 `tc tbf` 限速，否则打爆 MCU；② cannelloni 默认 `-t 100000`（100 ms）
> 会把时延抬高两个数量级，`run_tunnel.sh` 已改 `-t 1000`；③ 板端网关是"被动方"，
> 主机必须先发一个包触发 peer 学习；④ HPM5E31 的 MCAN 消息 RAM 在 AHB SRAM，组件已处理。
