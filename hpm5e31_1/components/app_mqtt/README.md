# components/app_mqtt —— MQTT 3.1.1 客户端（裸机 lwIP raw TCP）

面向 HPM5E31 裸机（NO_SYS=1、无 OS、无 malloc）的 MQTT 3.1.1 客户端。所有收发都是非阻塞的，
主循环调用 `app_mqtt_poll()` 推进 TCP / keepalive / QoS1 重传 / 自动重连状态机；
lwIP 回调里只做"拆包 + 回 PUBACK"，不打印、不阻塞。

配套例程 `samples/app/mqtt_telemetry`、上位机 `host/mqtt/`，书稿见 `docs/chapters/37-mqtt.md`。

## 1. 能力矩阵

| 特性 | 状态 | 代码位置 |
|---|---|---|
| CONNECT / CONNACK（用户名口令、LWT 遗嘱、clean session） | ✅ | `app_mqtt.c` mq_send_connect / mq_handle_packet |
| PUBLISH QoS0 / QoS1（QoS1 未确认重发 DUP=1，窗口 8） | ✅ | app_mqtt_publish / mq_build_publish_from_slot |
| PUBACK（收：自动回；发：匹配未确认表） | ✅ | mq_send_puback / MQ_PUBACK 分支 |
| SUBSCRIBE / SUBACK、UNSUBSCRIBE / UNSUBACK | ✅ | app_mqtt_subscribe / unsubscribe |
| PINGREQ / PINGRESP（keepalive 保活 + 超时断链） | ✅ | app_mqtt_poll 的 CONNECTED 分支 |
| DISCONNECT（优雅关闭，broker 不发遗嘱） | ✅ | app_mqtt_disconnect(graceful=true) |
| 异常断链自动重连（指数退避 1 s → 30 s） | ✅ | mq_schedule_reconnect |
| 剩余长度 1~4 字节变长编码/解码 | ✅ | app_mqtt_remlen_encode / decode |
| 主题过滤器通配符匹配（+/#，$ 主题规则） | ✅ | app_mqtt_topic_match |
| packet id 分配（跳过 0 与未确认 id） | ✅ | mq_alloc_pid |
| 发送环形队列（TCP sndbuf 满时暂存） | ✅ | txq[1024] + mq_tx_flush |
| Nagle 关闭（MQTT 小报文必需） | ✅ | mq_on_connected → tcp_nagle_disable |
| QoS2（PUBREC/PUBREL/PUBCOMP） | ❌ 收到即判协议错误断链 | 见第 37 章待确认项 |
| TLS（MQTTS 8883） | ❌ 需 mbedTLS | 见第 37 章待确认项 |
| clean_session=0 的会话持久化/离线消息 | ❌ 本板不缓存 | 见第 37 章待确认项 |
| broker 域名解析（DNS） | ❌ 只接受 IPv4 点分十进制 | app_mqtt_connect |

## 2. 目录与容量

```
components/app_mqtt/
  inc/app_mqtt.h     配置/统计/回调/实例结构 + 纯函数（剩余长度、主题匹配）
  src/app_mqtt.c     报文编解码 + lwIP TCP 回调 + 状态机
  app_mqtt.cmake     例程接入（sdk_inc + sdk_app_src）
```

| 编译期容量 | 值 | 说明 |
|---|---|---|
| `APP_MQTT_MAX_PACKET` | 512 B | 单条报文上限，超长断链 |
| `APP_MQTT_RX_BUF` | 2048 B | 接收重组缓冲，需 ≥ 一个 TCP MSS(1460)+最大报文 |
| `APP_MQTT_TXQ_SIZE` | 1024 B | 发送环形队列 |
| `APP_MQTT_INFLIGHT_MAX` | 8 | QoS1 未确认窗口 |
| `APP_MQTT_INFLIGHT_PAYLOAD` | 192 B | 单条 QoS1 未确认载荷上限 |
| `app_mqtt_t` 实例 | **6228 B**（`nm -S` 实测） | 静态分配，放在例程里 |

RAM 主要花在 `rxbuf`(2 KB)、`txq`(1 KB)、8 条 QoS1 未确认副本（约 2.6 KB）。

## 3. 最小用法

```c
#include "app_lwip.h"
#include "app_mqtt.h"

static app_mqtt_t mq;

static void on_msg(app_mqtt_t *m, const app_mqtt_msg_t *msg, void *user)
{
    /* 回调里可以 publish（tcp_write 允许在 recv 回调里调用），但不要 printf/阻塞 */
    app_mqtt_publish(m, "dev/ack", "ok", 2, 0, false);
}

int main(void)
{
    board_init(); bsp_init();
    app_lwip_init(NULL);                    /* ENET+YT8011AN+静态 IP+1 ms 时基 */
    app_mqtt_init(&mq, NULL);               /* 默认 192.168.2.1:1883, cid=hpm5e31, keepalive 1080 s */
    app_mqtt_set_message_cb(&mq, on_msg, NULL);
    app_mqtt_connect(&mq);
    while (1) {
        bsp_shell_poll();
        app_lwip_poll();
        app_mqtt_poll(&mq);                 /* 必须频繁调用（<=10 ms 一次） */
    }
}
```

## 4. 关键行为约定

1. **连接后要自己订阅**：组件不保存订阅列表，重连后需在 `on_state(CONNECTED)` 里重新 `app_mqtt_subscribe()`。
2. **QoS1 只保证"会话内至少一次"**：clean_session=1，断链后未确认消息丢弃（`stats.drops` 增加），不跨会话重发。
3. **publish 返回值**：`>=0` packet id（QoS0 为 0）；`-2` 未连接；`-3` 窗口/队列满；`-4` 不支持 QoS2。
4. **回调上下文**：`on_message` 在 `app_mqtt_poll()` 里、lwIP recv 回调内触发；payload 指针只在回调内有效。
5. **统计**：`app_mqtt_get_stats()` 取快照，含重传/丢包/PING RTT/协议错误计数。

## 5. 上板实测（详见第 37 章）

| 项 | 数据 | 日志 |
|---|---|---|
| CONNACK 成功 / keepalive 协商 | k1080（broker 日志） | `logs/20261102-160234-host-mqtt-broker-basic.log` |
| 命令往返 RTT（默认 broker） | 1.05~1.19 ms | `logs/20261102-160246-host-mqtt-pub-led.json` |
| 回声 RTT（broker 开 TCP_NODELAY） | avg 0.412 / p50 0.331 / max 1.17 ms | `logs/20261102-161551-host-mqtt-pub-echo.json` |
| 板→主机 QoS1 1000 条（板端自报） | 1000/1000，114 ms，8771 msg/s | `logs/20261102-161054-host-mqtt-serial-stress.log` |
| LWT（abortive kill） | 1.6 ms | `logs/20261102-162530-host-mqtt-lwt-kill.json` |
| keepalive=10 s 空闲保活 | 30 s 内 PINGREQ/PINGRESP 各 3 次 | `logs/20261102-162915-host-mqtt-lwt-keepalive.json` |
| 静态 RAM | 6228 B（`g_mq` 符号） | `build/mqtt_telemetry-debug/output/demo.elf` |