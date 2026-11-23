# samples/app/mqtt_telemetry —— MQTT 3.1.1 遥测上报 + 下行命令

板端把 MQTT 3.1.1 客户端跑在裸机 lwIP 上，向主机 broker（`192.168.2.1:1883`）周期上报遥测，
同时订阅下行命令（点灯 / 改周期 / 发 CAN / 复位 / 断链测试）。上位机工具见 `host/mqtt/`，
书稿见 `docs/chapters/37-mqtt.md`。

## 1. 硬件与网络

| 项 | 连接 |
|---|---|
| 以太网 | ENET0 RGMII + YT8011AN(1000BASE-T1) ↔ 千兆车载以太网盒 ↔ 主机 `enp6s0` |
| 地址 | 板 `192.168.2.100/24`，主机 `192.168.2.1/24`（**不改主机网络**） |
| broker | 主机测试实例：`bash host/mqtt/run_broker.sh start`（只监听 192.168.2.1:1883） |
| CAN | CAN0(PD00/PD01) ↔ CAN1(PD05/PD04) 板内两两相连，遥测里的 can_tx/can_rx 来自这条真实总线 |
| ADC | ADC0.IN1 = PF26（J3 排针，悬空为噪声底） |
| 温度 | 片内 TSNS（1 LSB = 1/256 °C） |
| LED | 用户灯 PA02，`hpm5e31/cmd/led` 控制 |

## 2. 主题与载荷

| 方向 | 主题 | QoS | retain | 载荷 |
|---|---|---|---|---|
| 上行 | `hpm5e31/telemetry` | 1 | 否 | `{"dev":"hpm5e31","seq":..,"up_ms":..,"can_tx":..,"can_rx":..,"adc":..,"temp_mc":..,"loop_us":..,"loop_max_us":..,"mqtt_rx":..,"mqtt_tx":..}`（143~148 B） |
| 上行 | `hpm5e31/status` | 1 | **是** | `{"dev":"hpm5e31","online":true,"up_ms":..,"fw":"1.0.0"}` |
| 上行 | `hpm5e31/ack` | 1 | 否 | `{"dev":"hpm5e31","cmd":"led","ok":1,"led":1}` |
| 上行 | `hpm5e31/echo/reply` | 0 | 否 | 原样回声（命令行往返时延测试） |
| 上行 | `hpm5e31/bench` / `bench/done` | 1 / 0 | 否 | 吞吐测试报文 / 板端自报结果 |
| 下行 | `hpm5e31/cmd/#` | 1 | — | 命令主题，见下表 |
| 遗嘱 | `hpm5e31/status` | 1 | 是 | `{"dev":"hpm5e31","online":false,"reason":"lwt"}`（47 B） |

下行命令（`hpm5e31/cmd/<x>`，QoS1）：

| 命令 | 载荷 | 动作 |
|---|---|---|
| `led` | `on` / `off` / `toggle` | 控制 PA02 |
| `period` | 毫秒（100~60000） | 遥测周期，默认 1000 ms |
| `cansend` | `0x123,11,22,33,44` | 从 CAN0 发一帧（CAN1 收） |
| `keepalive` | 秒 | 改 keepalive（下次 CONNECT 生效，配合 `reconnect`） |
| `reconnect` | 任意 | 优雅 `DISCONNECT` 后立即重连（broker **不**发遗嘱） |
| `kill` | 任意 | 直接 `tcp_abort`（不发 DISCONNECT），broker **发遗嘱**；1 s 后自动重连 |
| `reboot` | 任意 | `ppor_sw_reset()` 软复位 |
| `echo` | 任意 | 原样回发到 `echo/reply` |
| `bench` | `<n>,<qos>` | 向 `hpm5e31/bench` 连发 n 条，测板→云吞吐 |

## 3. shell 命令

```
mqtt                      # 状态 + 统计（连接/发布/重传/PING/丢弃/协议错误）
mqtt conn|close|kill      # 重连 / 优雅断开（无遗嘱）/ 直接断链（有遗嘱）
mqtt stat|reset           # 打印统计 / 清零
mqtt hex                  # 最近一条发出报文的 HEX（逐字节对照第 37 章表 37-5）
mqtt match                # 主题通配符匹配自测（6 组，不依赖 broker）
mqtt cfg period <ms> | qos <0|1> | keepalive <s>
mqtt pub <topic> <payload> [qos]
mqtt sub <filter> [qos] | mqtt unsub <filter>
mqtt bench <n> [qos]      # 板端自发自测（结果打印 + 回 hpm5e31/bench/done）
mtele                     # 立即发一条遥测
mstat                     # 状态汇总 + PASS/FAIL 结论行
```

## 4. 构建与上板

```bash
# 1) 主机起测试 broker（只绑 192.168.2.1，测完 stop）
bash host/mqtt/run_broker.sh start
# 2) 编译
cd hpm5e31_1 && bash scripts/build.sh samples/app/mqtt_telemetry
# 3) 烧写 + 抓串口 + 上位机联调（同一把整机锁）
bash scripts/hwrun.sh app/mqtt_telemetry --secs 12 --send "mstat" --lock-wait 1800 \
     --post "bash host/mqtt/run_suite.sh basic"
# 4) 测完停 broker
bash host/mqtt/run_broker.sh stop
```

## 5. 实测现象（2026-11-02）

串口（`logs/20261102-162800-mqtt_telemetry.log`，banner 含 `mqtt_telemetry`/`192.168.2.100`）：

```
[mqtt] STAT st=CONNECTED up=0s sub=OK conn=1 ok=1 pub_tx=1 pub_rx=0 retx=0 ping=0/0 rtt=0ms
[mqtt] STAT seq=1 can_tx=1 can_rx=0 adc=34698 temp=34000mC loop_max=1224us evt=1 => PASS
```

上位机（同一次持锁）：

| 测试 | 结果 | 日志 |
|---|---|---|
| 订阅校验（1 Hz/字段/seq/retained） | PASS：11 条，1.000 Hz，间隔 999.7~1000.6 ms，148 B | `logs/20261102-161549-host-mqtt-sub-check.json` |
| 命令回执 RTT | `led` 1.19 ms、`period` 1.05 ms、`cansend` 20.9 ms（broker 默认配置） | `logs/20261102-160247-host-mqtt-pub-period.json` |
| 回声 RTT（broker `set_tcp_nodelay true`） | 30/30，avg 0.412 / p50 0.331 / p95 0.814 / max 1.17 ms | `logs/20261102-161551-host-mqtt-pub-echo.json` |
| 1000 条 QoS1 回声 | 1000/1000，无丢包（含发布端排队 e2e 26.49 ms） | `logs/20261102-162009-host-mqtt-stress.json` |
| 板→主机 QoS1 1000 条（板端自报） | 1000/1000，114 ms，8771 msg/s（主机侧单次计时 55 ms/18179 msg/s，未归档） | `logs/20261102-161054-host-mqtt-serial-stress.log` |
| LWT（kill） | broker 1.6 ms 内发出遗嘱，板端 1 s 后自动重连 | `logs/20261102-162530-host-mqtt-lwt-kill.json` |
| keepalive=10 s 空闲 | 30 s 内 PINGREQ/PINGRESP 各 3 次 | `logs/20261102-162915-host-mqtt-lwt-keepalive.json` |
| 软复位 LWT | broker 304 ms 后发遗嘱，复位后重新 online | `logs/20261102-162620-host-mqtt-lwt-reboot.json` |

## 6. 坑（详见第 37 章 §37.10）

1. **不清 Nagle 会多 40 ms**：PUBACK 之后紧跟的小 PUBLISH 被 Nagle + 对端 delayed ACK 卡住。板端 `tcp_nagle_disable()`、broker 侧 `set_tcp_nodelay true` 都要做（实测单条 RTT 41 ms → 0.27 ms）。
2. **接收缓冲 ≥ 一个 TCP MSS**：`APP_MQTT_RX_BUF` 若小于 1460+报文头，批量 PUBLISH 一到就判协议错误断链（本板实测过 1000 条压测时断链）。
3. **QoS1 窗口只有 8**：板端连发时 `app_mqtt_publish` 返回 -3 是正常的流控，例程的 bench 循环要 `app_lwip_poll()` 后再重试。
4. **`mqtt bench` 第一次可能只发出几十条**：TCP 发送队列（lwIP `TCP_SND_QUEUELEN=16`）与 broker delayed ACK 的起步阶段叠加，第二次同参数即可 1000/1000；书中按"已知限制"记录。
5. **系统 mosquitto 被 AppArmor 限制**：配置只能读 `/etc/mosquitto/**`，用自己的 conf 会报 `Unable to open config file`；`host/mqtt/run_broker.sh` 把二进制复制到 temp2 下运行规避。
6. **pcntl/shell 命令名冲突**：BSP 里已有 `stat`/`help` 等命令，本示例统一用 `mqtt ...` / `mtele` / `mstat` 前缀。