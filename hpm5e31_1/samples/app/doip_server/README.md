# doip_server —— DoIP(ISO 13400-2) 服务端 + UDS 诊断（单固件）

> 章节：第 31 章；组件：`components/app_doip`；上位机：`host/doip`。
> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>

一块板跑完整链路：**lwIP（ENET0 RGMII + YT8011AN）+ DoIP 服务端 + UDS 服务端**。
主机 `enp6s0` = 192.168.2.1/24，板 192.168.2.100/24，端口 UDP/TCP 13400。
逻辑地址：本板 0x1000，测试仪 0x0E00。

## 1. 硬件与网络

| 项 | 值 |
|---|---|
| 板 | HPM5E31_1，ENET0 RGMII ↔ YT8011AN（1000BASE-T1）↔ 车载以太网盒 ↔ 主机 `enp6s0` |
| 板 IP | 192.168.2.100/24，网关 192.168.2.1 |
| 主机 IP | 192.168.2.1/24（已配好，不要改） |
| 端口 | UDP 13400（发现/声明/实体状态/电源模式）、TCP 13400（路由激活/诊断/alive check） |
| 串口 | /dev/ttyACM0 921600（letter-shell） |

## 2. 构建与烧写

```bash
cd hpm5e31_1
# 编译
bash scripts/build.sh samples/app/doip_server

# 烧写 + 抓 45 s 串口 + 主机侧跑全部回归（整机锁，等锁属正常）
bash scripts/hwrun.sh app/doip_server --secs 45 \
     --send "doip timer period 1000;doip timer general 3000" \
     --post "timeout 140 python3 host/doip/run_tests.py" --lock-wait 1800
```

串口日志进 `logs/<时间戳>-doip_server.log`；主机侧结果建议同时 `tee` 到 `logs/<时间戳>-host-doip-*.log`。
**抓完先看日志开头 banner** 确认是 `doip_server` 固件（板上有别人的固件时 `--no-flash` 会串味）。

## 3. 板端 shell 命令

| 命令 | 作用 |
|---|---|
| `doip` | 用法 + 全局统计 |
| `doip start` / `doip stop` | 起停 TCP+UDP 13400 |
| `doip stat` | 收发/路由激活/超时/alive check/大报文计数 |
| `doip conn` | 连接表（状态/对端/SA/收发计数） |
| `doip announce [n]` | 立即发 n 次车辆声明 |
| `doip vin [17字符]` / `doip eid [12位HEX]` | 读写 VIN / EID |
| `doip route` | 路由激活策略 |
| `doip timer <initial/general/alive/announce/period> <ms>` | 改定时器 |
| `doip alive [idx]` | 主动发 0x0007 |
| `doip selftest` | 编解码 49 项自检 |
| `doip bench [n] [cnt]` | DoIP 大报文吞吐注入 |
| `doip uds` | UDS 服务端统计 |

UDS 服务（复用第 30 章 `components/app_uds`）：0x10、0x11、0x14、0x19、0x22、0x27、0x28、0x2E、0x2F、0x31、0x3E、0x85，
另加本章专用例程：0x0203 自检、0x0301 慢例程（150 ms，验证 NRC 0x78）、0x0204 DoIP 大报文注入、0x0302 DoIP 计数器。

## 4. 主机侧命令

```bash
python3 host/doip/doip_client.py discover --count 5     # UDP 车辆发现，统计 RTT
python3 host/doip/doip_client.py announce --secs 5      # 监听广播声明
python3 host/doip/doip_client.py entity                 # 0x4001 + 0x4003
python3 host/doip/doip_client.py route --sa 0x0E00      # 路由激活
python3 host/doip/doip_client.py uds                    # 完整 UDS 序列
python3 host/doip/doip_client.py bulk --size 4000 --count 20
python3 host/doip/doip_client.py err                    # 异常用例
python3 host/doip/run_tests.py                          # pytest 风格回归（16 用例）
bash host/doip/run_client.sh all                        # 封装 + 自动落 logs/
```

## 5. 实测（2026-11-02）

串口日志：`logs/20261102-163909-doip_server.log`；主机侧日志：`logs/20261102-163907-host-doip-runtests.log`。
（RTT 只认 host 日志；串口日志里只有板端计数与 selftest 输出。）

| 项目 | 实测 |
|---|---|
| UDP 0x0001 车辆发现 RTT | 均值 129.6 µs / 最大 289.9 µs（n=5） |
| UDP 0x4001 实体状态 / 0x4003 电源模式 | 99.8 µs / 81.5 µs |
| 周期声明（`doip timer period 1000`） | 4 s 收 5 条，间隔 1000.05 ms、偏差 0.89 ms |
| 路由激活 RTT | 1219.7 µs |
| 0x8002 肯定应答时延 | 167.0 µs（回调只入队） |
| 慢服务 0x31 01 0301 | 150.6 ms，过程中 1 条 7F 31 78 |
| initial / general inactivity 断链 | 2000.40 ms / 2998.70 ms |
| 入向 4008 B 帧（4000 B UDS） | 266.0 µs 回 0x8003/0x04（超 UDS 能力），帧被完整重组 |
| 入向 516 B 帧（512 B UDS） | 236.3 µs（ACK + UDS 响应） |
| 出向 4008 B 帧 | 375.3 µs / 85.4 Mbit/s（单帧） |
| 回归套件 | 16 用例 15 PASS / 1 已知限制（多帧突发） |
| DoIP 编解码自检 | 49 项 0 失败 |

## 6. 易错点（详见第 31 章 §31.13）

1. 路由激活请求可能是 7 B（EcuBus-Pro 默认），别只按 11 B 校验。
2. 电源模式载荷类型是 0x4003/0x4004，不是 0x0009/0x000A。
3. lwIP 回调里阻塞会让 0x8002 ACK 迟到（实测 150 ms），UDS 处理要入队到主循环。
4. 0x8002/0x8003 里第一个地址是实体（发送方），不是原报文的 SA。
5. `TCP_SND_BUF=11.68 KB` 只够 2 条 4 KB 帧，批量发送要在帧间跑 `app_lwip_poll()`。
6. 上位机收广播声明要 bind 0.0.0.0，bind 到 192.168.2.1 收不到受限广播。
7. 单条 UDS 不能超过 512 B（`APP_UDS_MAX_REQ`）：本固件在 DoIP 层就回 0x8003/0x04，
   不会把超长请求塞给 UDS（塞过去的表现是"有 ACK 没响应"，见第 31 章易错点 10）。

## 7. 复跑与排障

| 现象 | 先看什么 |
|---|---|
| 主机连不上 13400 | 先 `ping 192.168.2.100`（本板实测 0.15~0.37 ms）；再看串口有没有 `[doip] listen TCP+UDP 13400 rc=0` |
| 路由激活一律 0x00 | 板端 `doip route` 看 tester_addr；主机侧 SA 必须匹配 |
| 连上就断 | 2 s 内没发 0x0005（initial inactivity），见第 31 章易错点 6 |
| 大报文收不到 | 看 `doip stat` 的 `big_msg`/`diag_rx`；单帧上限 4096 B |
| 声明收不到 | 板端 `doip timer period 1000`；主机 bind 0.0.0.0（第 31 章易错点 7） |
