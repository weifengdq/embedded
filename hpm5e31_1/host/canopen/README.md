# host/canopen —— CANopen 上位机工具（python-can + python-canopen）

对 hpm5e31_1 上 CAN0 的 CANopen 节点 1（`components/canopen_node`）做 NMT / SDO / PDO / 心跳 / EMCY
测试与性能实测。三个脚本相互独立，都直接操作 SocketCAN 的 **vcan0**（经板端 cannelloni 网关映射到真实 CAN 总线）。

## 1. 依赖

```bash
# 统一使用项目 venv（已装好）
$HOME/pyenv/bin/python -c "import can, canopen; print(can.__version__, canopen.__version__)"
# 输出：4.6.1 2.4.1
```

| 包 | 版本 | 用途 |
|---|---|---|
| python-can | 4.6.1 | SocketCAN 原始帧收发（SYNC/TPDO 时序、jitter 测量） |
| python-canopen | 2.4.1 | SDO client / NMT / EDS 对象字典 |

## 2. Bench B 拓扑与 vcan0 准备

```
主机 vcan0  <-- UDP 4000 (cannelloni v2) -->  板端 CAN1 网关  <-- 同一段 CAN 总线 -->  板端 CAN0 节点 1
```

1) 板端：烧写带网关的固件（tp-infra 的 `components/app_can_gw` + `app_lwip`，见第 28 章），
   板 IP `192.168.2.100`，主机 `enp6s0 = 192.168.2.1`；
2) 主机：

```bash
# 一次性：加载 vcan、拉起 cannelloni、限速
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan 2>/dev/null || true
sudo ip link set up vcan0
# vcan 默认没有速率限制，不限速会淹没 MCU：限到 500 kbit/s
sudo tc qdisc add dev vcan0 root tbf rate 500kbit latency 100ms burst 4000 2>/dev/null || true
# cannelloni（与板端 192.168.2.100:4000 对接）
cannelloni -I vcan0 -R 192.168.2.100 -r 4000 -l 4000
```

`host/can_bridge/`（tp-infra 提供）里有封装好的 `run_bridge.sh`，用那个更省事。

## 3. EDS 文件

- `node1.eds` 由 `components/canopen_node/tools/gen_od.py` 与固件对象字典**从同一张表生成**，
  固件改了对象字典必须重跑生成器（CI 可用 `gen_od.py --check`）。
- python-canopen 加载方式：

```python
import canopen
network = canopen.Network()
network.connect(interface="socketcan", channel="vcan0")
node = network.add_node(1, "node1.eds")     # 节点号 1 + EDS
print(node.sdo[0x1000].raw)                 # 按名称/索引访问对象
node.nmt.state = "OPERATIONAL"              # 发 NMT Start
```

- 也可用 `canopen.import_od("node1.eds")` 单独检查对象表（不连总线）。
- CANopenEditor 可直接打开本 EDS（对象类型/数据类型/默认值/PDO 映射齐全）。

## 4. 脚本

| 脚本 | 功能 | 关键输出 |
|---|---|---|
| `co_node_test.py` | NMT 启停/复位、SDO 加速+分段读写（0x1000/0x1008/0x1017/0x1018/0x2106）、心跳周期、EMCY | PASS/FAIL 表 + 心跳间隔统计 |
| `co_pdo_bench.py` | 10 ms SYNC 驱动 TPDO1，统计**端到端延迟 / 周期抖动 / 丢帧**；TPDO2 事件型；RPDO1 控制回环 | 指标表 + 可选 CSV |
| `co_sdo_seg.py` | 1 KB / 4 KB 分段 SDO upload+download 吞吐、图案校验、加速读速率、1KB vs 4KB 对比 | 对比表 + 可选 CSV |

```bash
cd host/canopen
$HOME/pyenv/bin/python co_node_test.py                    # 全流程功能测试
$HOME/pyenv/bin/python co_pdo_bench.py --period-ms 10 --duration 5
$HOME/pyenv/bin/python co_sdo_seg.py --iterations 5
```

所有脚本支持 `--help`、`--interface/--channel/--node-id/--eds`，退出码 0 = 全部 PASS。

## 5. 实测输出

### 5.1 co_node_test.py（Bench B，真实 vcan0 + cannelloni 网关）

日志 `logs/20261030-174516-canopen_node.log`：

```
===== co_node_test.py =====
  NMT start -> heartbeat state 5         PASS  hb=1
  SDO read 0x1000 deviceType             PASS  0x00000191
  SDO read 0x1018:1 vendor-ID            PASS  0x0000A531
  SDO read 0x1017 heartbeat              PASS  1000 ms
  SDO segmented upload 0x1008            PASS  32 B 'HPM5E31 CANopen node 1'
  SDO write 0x1017 = 200                 PASS  read back 200
  heartbeat 200 ms (2.2 s)               PASS  10 frames, avg=206.3 ms, max|dev|=7.5 ms
  SDO write 0x1017 restore 1000          PASS  read back 1000
  EMCY 0x081 on 0x2106 write             PASS  count=1 code=0x0001
  EMCY counter 0x2107                    PASS  1
  NMT stop -> heartbeat state 4          PASS
  NMT start -> heartbeat state 5         PASS
  NMT reset communication -> boot-up     PASS
  NMT start after reset                  PASS
=== RESULT: 14 PASS / 0 FAIL ===
```

### 5.2 co_pdo_bench.py（Bench B）

```
  TPDO1 同步: 丢帧                 FAIL  0.20% (491/492)
  TPDO1 同步: 平均延迟              PASS  avg=2.30 ms p99=2.75 max=2.83
  TPDO1 同步: 周期抖动 σ            PASS  σ=0.60 ms max|dev|=12.62 ms avg=10.20 ms
  TPDO2 事件型 1000 ms (5 s)        PASS  5 frames
  RPDO1 -> 0x2100 回环              PASS  0001->0001 0002->0002 0000->0000
```

0.2% 丢包是 cannelloni **UDP fire-and-forget** 的固有行为（tp-infra 网关实测 0.13%~0.2%），
不是 CANopen 节点丢帧；判据建议放宽到 ≤1%（脚本当前阈值 0.1%，会如实报 FAIL）。

### 5.3 co_sdo_seg.py（Bench B，已知限制）

1 KB 分段需要 294 帧、4 KB 需要 1172 帧；0.2% 丢包下必然出现 SDO 超时（CiA 301 的 SDO
分段本身不带重传）。因此 **Bench B 下本脚本会失败**，请用 Bench A（板内主站）测吞吐
（14.0 KB/s，见第 32 章表 32-11），或用 cannelloni TCP 模式/主机侧重试。

## 6. 坑

1. **vcan 不限速会淹 MCU**：`tc qdisc ... tbf rate 500kbit` 必须加，否则板端网关丢帧、SDO 超时；
2. **NMT Reset Application 会让板子重启**：`co_node_test.py` 默认只测 Reset Communication，
   测 App 复位要加 `--no-nmt-reset` 之外的单独流程（重启后 vcan0 仍在线，等 boot-up 帧即可）；
3. **EDS 与固件必须同源**：手工改 OD.c 而不改 `gen_od.py`/`node1.eds`，python-canopen 会因
   长度/类型不符报 `0x06070010`（长度不匹配）或直接 KeyError；
4. **两个 SocketCAN 套接字同时收**：`co_pdo_bench.py` 里 python-can 与 python-canopen 各开一个
   socket，vcan 会把帧复制给两者，互不干扰（真实 CAN 硬件只有一个 socket 也没问题）；
5. **时间戳**：vcan 上 `msg.timestamp` 是内核 softirq 时间，抖动较大；脚本的端到端延迟用
   `time.perf_counter()` 前后夹住 send/recv，测的是"进程视角"的真实往返。
