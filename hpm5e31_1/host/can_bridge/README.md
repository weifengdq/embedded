# host/can_bridge —— 主机侧 CAN 网关链路（vcan + cannelloni + 验收脚本）

把板端 `samples/app/can_gateway` 的 CAN1 网关映射成主机 `vcan0`，让
`candump` / `cansend` / python-can / udsoncan / canopen / pyxcp 直接当"CAN 分析仪"用。
协议是 **cannelloni v2**（5 B 头 + Linux `canfd_frame` 序列化，大端），与板端
`components/app_can_gw` 逐字节兼容。

## 1. 文件

| 文件 | 作用 |
|---|---|
| `vcan_setup.sh` | 建 `vcan0/2/3` + `tc tbf` 限速（默认 500 kbit/s，latency 10 ms） |
| `build_cannelloni.sh` | 从 `ref2/cannelloni` 构建 stock cannelloni（`-DSCTP_SUPPORT=OFF`，产物 `build/`） |
| `run_tunnel.sh` | 起隧道：`vcan0 <-> 192.168.2.100:4000`，默认 `-t 1000`（低延迟） |
| `acceptance.sh` | 主机侧一键验收：ping → 心跳 → 回显 → 延迟/丢包，输出 PASS/FAIL 汇总 |
| `bench_gateway.py` | python-can 验收：心跳频率、往返延迟 avg/p50/p95/p99、300 帧/s 丢包率，可 `--json-out` |
| `udp2vcan.py` | 纯 Python 备用桥（无 cannelloni 时用，同一协议，逐字段解析，`--verbose` 可打印每帧） |
| `build/` | cannelloni 构建产物（.gitignore 已排除，不入 git） |

## 2. 三步启动

```bash
cd host/can_bridge
sudo ./vcan_setup.sh          # 1) vcan0 + 500 kbit/s 限速（默认 CHANNELS=0）
./build_cannelloni.sh         # 2) 只做一次：cmake+ninja 构建 cannelloni
./run_tunnel.sh &             # 3) vcan0 <-> 192.168.2.100:4000（-t 1000us）
```

验证（板端固件已跑起）：

```bash
candump vcan0                          # 板端 CAN0 心跳：321 [8] .. .. .. .. CA FE BA BE，10 Hz
cansend vcan0 123#1122334455667788     # 板端 CAN0 收到后回显，candump 出现 321#1122334455667788
python3 bench_gateway.py              # 延迟/丢包/心跳三项验收
bash acceptance.sh                    # 上面全部 + ping 的一键版
```

多路（`gw full` 固件）：

```bash
sudo CHANNELS="0 2 3" ./vcan_setup.sh
./run_tunnel.sh &                     # CAN1 -> vcan0:4000
IFACE=vcan2 LOCAL_PORT=4002 REMOTE_PORT=4002 ./run_tunnel.sh &
IFACE=vcan3 LOCAL_PORT=4003 REMOTE_PORT=4003 ./run_tunnel.sh &
```

没有 cannelloni 时（例如只装了 python-can）：

```bash
./udp2vcan.py --iface vcan0 --board 192.168.2.100 --board-port 4000 --listen-port 4000
```

## 3. cannelloni 参数说明（v2.1.2，`-h` 实测）

| 参数 | 含义 | 本仓库取值 | 说明 |
|---|---|---|---|
| `-I` | 本机 CAN 接口 | `vcan0` | |
| `-R` | 远端 IP | `192.168.2.100` | 板端 |
| `-r` | 远端 UDP 端口 | `4000` | 板端 `app_can_gw` 监听口 |
| `-l` | 本机监听端口 | `4000` | 板端回包目的端口 |
| `-t` | **攒包超时(µs)** | `1000` | 默认 100000 = 100 ms，会显著抬高时延（见 §4） |
| `-x` | 丢弃超时(µs) | 默认 | 帧在缓冲里超过该时间丢弃 |
| `-p` | 关闭对端检查 | 不用 | 板端会跟随源地址，不需要 |
| `-d c` / `-d u` | 打印 CAN / UDP 调试 | 调试用 | 高帧率下会拖慢 |

## 4. 实测（2026-10-30，板端 `samples/app/can_gateway`）

### 表 主机侧验收结果

| 项目 | 命令 | 结果 | 判定 |
|---|---|---|---|
| ping | `ping -c 5 -i 0.2 192.168.2.100` | 5/5，0% 丢包，rtt 0.129/0.231/0.458 ms | PASS |
| 心跳 | `timeout 2 candump vcan0` | 20 帧 / 2 s = 10.0 Hz（0x321，载荷 CA FE BA BE） | PASS |
| 回显 | `cansend vcan0 123#1122334455667788` | `candump` 看到 `321#1122334455667788` | PASS |
| 往返延迟（`-t 1000`） | `bench_gateway.py --lat-n 100` | avg **2.24 ms** / p50 2.27 / p95 2.40 / p99 **2.48** / max 2.48 / min 1.72 ms | PASS |
| 300 帧/s 丢包 | `bench_gateway.py --loss-rate 300 --loss-secs 5` | 1500 帧发出，1500 回显，**0.00%** | PASS |
| 往返延迟（`-t 100000`） | 同上，`BUF_US=100000` | avg **79.89 ms** / p99 101.0 ms | 对照 |

> `-t 100000` 时 300 帧/s 的"丢包率"实测 46.27%：这不是 CAN/UDP 丢包，而是 100 ms 攒包 + 200 ms 超时窗口
> 造成的"上一批还在主机缓冲里、下一批已判定超时"。它说明**应用层联调必须把 `-t` 改小**。

原始日志：板端 `logs/20261030-174321-can_gateway.log`（最终 PASS 那次）、`logs/20261030-173424-can_gateway.log`（首轮 PASS）、`logs/20261030-174200-can_gateway.log`（`-t 100000` 对照）；
主机侧 `/tmp/can_bridge_accept/{ping,candump_hb,candump_echo,bench}.log`、`bench.json`。

## 5. 坑

| # | 现象 | 原因 | 解决 |
|---|---|---|---|
| 1 | `candump` 什么都收不到，但 ping 通 | 板端网关是"被动方"：没收到主机 UDP 包前不知道 peer，不会回发 CAN 帧 | 先 `cansend vcan0 7FE#00` 触发学习（`acceptance.sh` 已内置），或 `gw peer 192.168.2.1 4000` |
| 2 | 延迟 ~100 ms | cannelloni 默认 `-t 100000` | `BUF_US=1000 ./run_tunnel.sh`（脚本默认已是 1000） |
| 3 | 高帧率时大量丢帧 | vcan 无速率限制，瞬间灌包打爆 MCU CAN TX FIFO | `sudo ./vcan_setup.sh`（tbf 500kbit）；`gw stat` 看 `drop_can` |
| 4 | `build_cannelloni.sh` 找不到源码 | ref2 目录不在默认路径 | `CANNELLONI_SRC=/path/to/cannelloni ./build_cannelloni.sh` |
| 5 | `cannelloni` 启动报 `CAN_FD is not supported on vcan0` | cannelloni 试图在 vcan 上开 FD 模式失败（vcan 不支持 FD 配置 ioctl），但**不影响**收发 | 可忽略；本板用经典帧模式 |
| 6 | 板端回包发到旧端口 | 主机重启 cannelloni 换了 `-l` 端口 | 板端会跟随新源地址；若仍不对 `gw peer <ip> <port>` 固定 |
| 7 | 防火墙开启后收不到 UDP | 主机 ufw/iptables 拦 UDP 4000 | `sudo ufw allow 4000/udp` |
| 8 | 用 FD 帧时主机解析长度错 | 忘记 `len & 0x7F` / FD 帧多 1 字节 flags | 用 `bench_gateway.py` 或 `udp2vcan.py` 的解析（已处理） |