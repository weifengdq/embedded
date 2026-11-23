# components/app_someip —— 最小 SOME/IP 协议栈（AUTOSAR PRS SOME/IP over UDP）

面向 HPM5E31 裸机（单线程、无 malloc、无 OS）的 SOME/IP 服务端实现，配套上位机在 `host/someip/`，
书稿见 `docs/chapters/35-someip-vsomeip.md`。

## 1. 能力矩阵

| 特性 | 状态 | 代码 |
|---|---|---|
| 16 B 报文头编解码（含 Length 语义校验、协议版本校验） | ✅ | `src/app_someip.c` |
| REQUEST / REQUEST_NO_RETURN / RESPONSE / ERROR / NOTIFICATION | ✅ | `src/app_someip.c` |
| 方法注册与分发（最多 8 个） | ✅ | `app_someip_register_method()` |
| 事件注册与周期发送（最多 8 个，仅发给订阅者） | ✅ | `app_someip_register_event()` / `send_event()` |
| 字段 getter/setter（用方法号派生，语义约定见 §3） | ✅ | 例程里实现，组件只提供方法注册 |
| SOME/IP-SD：OfferService / StopOffer / FindService / SubscribeEventgroup + Ack | ✅ | `src/app_someip_sd.c` |
| 订阅表 + TTL 过期 + 续约 | ✅ | `src/app_someip.c`（订阅表、`app_someip_poll`） |
| SOME/IP-TP 分段/按序重组（4 KB 逻辑报文） | ✅ | `src/app_someip_tp.c` |
| 协议层自证测试（7 项，无需上位机） | ✅ | `src/app_someip_selftest.c` |
| TCP 传输 / E2E 保护 / 多实例 | ❌（待确认项 U-35-4/5/8） | — |

## 2. 目录与移植

```
components/app_someip/
  inc/app_someip.h          服务对象、方法/事件注册、统计、配置（someip_cfg_t）
  inc/app_someip_sd.h       SD Entry/Option 编解码 + 服务端状态机
  inc/app_someip_tp.h       TP 头/重组缓冲/分段发送
  inc/app_someip_wire.h     大端读写 + CRC32（内部）
  inc/app_someip_net.h      lwIP UDP 传输（ENET + 2 个 socket + 1 ms 时基）
  src/*.c
```

例程 CMakeLists 里 3 行即可接入：

```cmake
set(SOMEIP_COMPONENT ${REPO_ROOT}/components/app_someip)
sdk_inc(${SOMEIP_COMPONENT}/inc)
sdk_app_src(${SOMEIP_COMPONENT}/src/app_someip.c)          # 再加 _sd.c/_tp.c/_net.c/_selftest.c
```

**网络底座**：`src/app_someip_net.c` 已接入共享底座 **`components/app_lwip`**（tp-infra 提供）：
`app_lwip_init(NULL)` 完成 ENET RGMII + YT8011AN + 静态 IP 192.168.2.100/24 + 1 ms 时基，
`app_lwip_poll()` 负责收包与 lwIP 定时，`app_udp_socket(NULL, port)` 建 SD(30490)/服务(30501) 两个 socket，
时间戳用 `app_lwip_now_ms()`。例程 CMakeLists 只需一行 `include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)`。

> 开发过程：本章最初（app_lwip 尚未合入时）本文件自带一套 `enet_init/lwip_init/netif_config/enet_services`
> + YT8011 板级钩子（与 `samples/enet/enet_lwip_iperf` 相同），约 260 行；底座合入后切换，
> 同功能约 130 行，**协议层与例程代码零改动**——这正是"协议层只依赖 send/now_ms 两个回调"的设计收益。

## 3. 用法（服务端）

```c
static app_someip_t      srv;
static someip_tp_reasm_t tp;                  /* 4 KB 重组缓冲（要 TP 才需要，否则传 NULL） */

static uint8_t m_add(app_someip_t *s, const someip_msg_t *req, uint8_t *reply,
                     uint16_t cap, uint16_t *reply_len, void *user)
{
    if (req->payload_len < 8) return SOMEIP_RC_MALFORMED_MESSAGE;
    someip_put_u32(reply, someip_get_u32(&req->payload[0]) + someip_get_u32(&req->payload[4]));
    *reply_len = 4;
    return SOMEIP_RC_OK;                        /* 非 0 会被包成 ERROR + 该 Return Code */
}

someip_cfg_t cfg = {
    .service_id = 0x1234, .instance_id = 0x0001,
    .major_version = 0x01, .interface_version = 0x01, .minor_version = 1,
    .local_ip = {192,168,2,100},
    .sd_port = 30490, .service_port = 30501, .offer_ttl_s = 3,
    .sd_multicast_ip = {224,244,224,245}, .sd_multicast_port = 30490,
    .send = app_someip_net_send, .now_ms = app_someip_net_now_ms,
};
app_someip_init(&srv, &cfg, &tp);
app_someip_register_method(&srv, 0x0001, m_add, NULL);
app_someip_net_open(&srv, 30490, 30501);        /* 建 2 个 UDP socket 并把收包喂给 srv */
app_someip_offer(&srv);                         /* 组播 OfferService */
while (1) {
    app_someip_net_poll();                      /* lwIP 收包 + 定时 */
    app_someip_poll(&srv, app_someip_net_now_ms(NULL));  /* SD 周期 Offer / 订阅 TTL / 事件 */
    bsp_shell_poll();
}
```

约定（与上位机保持一致）：

- 方法号 0x0001~0x7FFF，事件号 0x8000~0xFFFF（字段 notifier = 0x8000 + field_id）；
- 字符串序列化 = `u32 长度 + 字符`（与 vsomeip serializer 一致）；
- `bool` 用 1 字节，回包补齐到 4 B（vsomeip 对齐规则）；
- `f32` 用 IEEE754 大端。

## 4. 诊断命令（例程里注册）

`stat`（计数器）、`selftest`（7 项协议自证）、`run`（例程自证）、`offer`/`stopoffer`（手动 SD）、
`sub`（订阅表）、`ip`（netif/PHY）、`tpstat`（TP 状态，仅 someip_tp）。

## 5. 关键约束与坑（详见第 34 章）

- 报文头全大端；`Length = 8 + 载荷`（不含 Message ID 与 Length 自身）。
- SD option 的 Length 不含 Length/Type 字段（IPv4 endpoint: Length=9，总长 12 B）。
- Ack 送订阅者 SD 端口；事件送 Subscribe option 里的端点。
- 事件只在有订阅者时发送；订阅 TTL 到期自动摘除，订阅者需每 TTL/2 续约。
- 4 KB 逻辑报文上限（`SOMEIP_MAX_PAYLOAD`），TP 段长 1392 B（87×16）。
- 收包回调里不要 printf（会拉长处理时间、增加抖包）。