# host/mqtt —— MQTT 上位机联调工具（broker + 订阅校验 + 命令/压力/LWT/吞吐）

配套板端 `samples/app/mqtt_telemetry` 与组件 `components/app_mqtt`，书稿见
`docs/chapters/37-mqtt.md`。全部脚本只用 **python3 标准库 + paho-mqtt + mosquitto 客户端**，
不额外装包。

## 1. 文件与职责

| 文件 | 作用 | 依赖 |
|---|---|---|
| `run_broker.sh` | 起/停本机 mosquitto 测试实例（只监听 `192.168.2.1:1883`，日志 `$HOME/tmp2/mosq_test.log`） | mosquitto 2.x |
| `mosquitto_test.conf` | 测试实例配置模板（`set_tcp_nodelay true`、anon、不持久化） | — |
| `sub_check.py` | 订阅遥测/状态/回执，校验 1 Hz、seq 连续、字段完整、retained 在线状态 | paho-mqtt |
| `pub_cmd.py` | 下行命令（led/period/cansend/keepalive/reconnect/kill/reboot/bench/echo），测回执 RTT | paho-mqtt |
| `stress.py` | 1000 条消息吞吐 + 逐条 RTT（burst 与限速两种模式） | paho-mqtt |
| `lwt_check.py` | 遗嘱/断线重连/keepalive 保活/软复位 四类验证 | paho-mqtt |
| `bench.sh` | 板→主机 QoS1 吞吐（用 C 客户端 `mosquitto_sub` 计数，避开 paho 单线程瓶颈） | mosquitto-clients |
| `run_suite.sh` | 一次持锁窗口内按阶段跑完上面全部（`basic`/`stress`/`lwt`/`keepalive`/`reboot`/`lwtall`/`bench`） | 以上 |
| `pcap_latency.py` | 拆 tcpdump 文本，算 "上位机→broker / broker→板 / broker→上位机" 各跳 Δt | tcpdump |
| `mqtt_helper.py` | paho 2.x/1.x 回调签名兼容、报告落盘 | paho-mqtt |

## 2. 快速开始

```bash
# 0) 网络前提：主机 enp6s0 有 192.168.2.1/24（已配好，不要改）
ping -c 2 192.168.2.100

# 1) 起测试 broker（系统服务只监听 127.0.0.1，别用它）
bash host/mqtt/run_broker.sh start
bash host/mqtt/run_broker.sh status

# 2) 烧写 + 串口 + 上位机联调（同一把整机锁，避免固件被别的测试换掉）
cd hpm5e31_1
bash scripts/hwrun.sh app/mqtt_telemetry --secs 12 --send "mstat" --lock-wait 1800 \
     --post "bash host/mqtt/run_suite.sh basic"

# 3) 压测/LWT/keepalive（每次重新烧写，避免 --no-flash 串味）
bash scripts/hwrun.sh app/mqtt_telemetry --secs 6 --post "bash host/mqtt/run_suite.sh stress"
bash scripts/hwrun.sh app/mqtt_telemetry --secs 6 --post "bash host/mqtt/run_suite.sh lwtall"

# 4) 测完停 broker
bash host/mqtt/run_broker.sh stop
```

## 3. 各脚本单独用法

```bash
python3 host/mqtt/sub_check.py --secs 10
python3 host/mqtt/pub_cmd.py --cmd led --arg on
python3 host/mqtt/pub_cmd.py --cmd echo --echo-n 30
python3 host/mqtt/stress.py --n 1000 --qos 1 --mode both          # burst
python3 host/mqtt/stress.py --n 1000 --qos 1 --mode throughput --rate 200   # 限速稳态
python3 host/mqtt/lwt_check.py --mode kill
python3 host/mqtt/lwt_check.py --mode keepalive --secs 30 --broker-log $HOME/tmp2/mosq_test.log
python3 host/mqtt/lwt_check.py --mode reboot --secs 40
bash host/mqtt/bench.sh 1000 1 0        # 发布 QoS1，订阅 QoS0
bash host/mqtt/bench.sh 1000 1 1        # 发布/订阅都 QoS1
python3 host/mqtt/pcap_latency.py logs/<ts>-host-mqtt-stress.txt
```

所有脚本退出码：`0` PASS、`1` FAIL、`2` 连不上 broker；JSON 报告默认写
`logs/<时间戳>-host-mqtt-<名字>.json`（`run_suite.sh` 还会额外归档 broker 日志切片、
串口日志、pcap/pcap 文本）。

## 4. 关键结论（同第 37 章）

| 现象 | 原因 | 处理 |
|---|---|---|
| 单条 RTT 41 ms、pcap 显示 broker→上位机这一跳 39.97 ms | mosquitto 客户端 socket 默认走 Nagle + Linux delayed ACK（40 ms） | 配置 `set_tcp_nodelay true`，p50 降到 0.02 ms（见 `pcap_latency.py` 前后对比） |
| 1000 条瞬时压测时订阅端 e2e 时延 1.4 s、QoS0 丢包 | paho 单线程回调处理不过来（上位机自身瓶颈） | 用 `bench.sh`（mosquitto_sub）或 `--rate` 限速 |
| `mosquitto -c` 报 `Unable to open config file` | Ubuntu 24.04 的 AppArmor 只允许读 `/etc/mosquitto/**` | `run_broker.sh` 把二进制复制到 temp2 下运行（不改系统策略） |
| 系统 mosquitto 只监听 127.0.0.1 | 发行版默认配置 | 用本目录的测试实例，只绑 192.168.2.1，避免暴露 |

## 5. 证据（logs/）

| 文件 | 内容 |
|---|---|
| `logs/20261102-160234-host-mqtt-broker-basic.log` | broker 侧 CONNECT(k1080)/SUBSCRIBE/PUBACK 逐条日志 |
| `logs/20261102-161549-host-mqtt-sub-check.json` | 订阅校验（1 Hz、字段、retained） |
| `logs/20261102-161551-host-mqtt-pub-echo.json` | 回声 RTT 30 条 |
| `logs/20261102-162009-host-mqtt-stress.json` | 1000 条 QoS1 burst + 200 条逐条 RTT |
| `logs/20261102-161106-host-mqtt-stress.json` | Nagle 未关时的 RTT（41 ms 级） |
| `logs/20261102-161054-host-mqtt-stress.{pcap,txt}` | 默认 broker 的抓包（broker→上位机 p50 39.97 ms） |
| `logs/20261102-161616-host-mqtt-stress.{pcap,txt}` | `set_tcp_nodelay` 后抓包（p50 0.02 ms） |
| `logs/20261102-162530-host-mqtt-lwt-kill.json` | LWT 1.6 ms + 自动重连 |
| `logs/20261102-162620-host-mqtt-lwt-reboot.json` | 软复位 LWT 304 ms + 重新 online |
| `logs/20261102-162915-host-mqtt-lwt-keepalive.json` | keepalive=10 s 空闲保活 PINGREQ/PINGRESP |
