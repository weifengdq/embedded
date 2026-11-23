# components/app_isotp —— ISO-TP(ISO 15765-2:2016) 传输层

## 1. 文件

| 文件 | 说明 |
|---|---|
| `include/app_isotp.h` | 协议栈 API（配置、帧、统计、send/on_frame/poll） |
| `src/app_isotp.c` | SF/FF/CF/FC 状态机、BS/STmin、N_Bs/N_Cr 超时、padding、29 位、CAN FD、AE |
| `include/app_isotp_port.h` | CAN 端口抽象（SDK MCAN 直驱） |
| `src/app_isotp_port.c` | MCAN 初始化/位时序/收发/时基/帧分发 |

## 2. 设计要点

1. **全非阻塞状态机**：`app_isotp_send()` 只发 SF 或 FF，CF 发送/FC 发送/超时判定都在 `app_isotp_poll()`。
   这样一颗 MCU 可以同时当 DUT 和测试节点（板内双节点自测），不会因为嵌套阻塞互相卡死。
2. **链路抽象**：`app_isotp_link_t` 只有"发一帧"和"取单调时基"两个回调，因此可以接 SDK MCAN、
   tp-infra 的 `app_can`，或主机上的 SocketCAN 实现。
3. **缓冲由调用者提供**：组件里没有 4 KB 静态数组，多通道时可以精确控制 RAM。

## 3. 与 tp-infra `app_can` 的关系

`app_isotp_port.c` 是一份"SDK MCAN 直驱"的参考端口（Bench A 用它跑通）。
tp-infra 的 `components/app_can` 落地后，只需再写一个 ~60 行的适配文件，
把 `tx_frame/recv/now_us` 转调 `app_can_send/app_can_recv/app_can_now_us` 即可，ISO-TP 逻辑零改动。

## 4. 最小用法

`@c
static app_isotp_t s_tp;
static uint8_t s_tx[4096], s_rx[4096];

static const app_isotp_link_t link = { .ctx = &port, .tx_frame = my_tx, .now_us = my_now };

app_isotp_cfg_t cfg = {
    .rx_id = 0x7E0u, .tx_id = 0x7E8u,
    .fd = false, .tx_pad = true, .pad_byte = 0xCCu,
    .bs = 0u, .stmin = 0u,
    .on_msg = my_on_msg, .user = NULL,
};
app_isotp_init(&s_tp, &cfg, &link, s_tx, sizeof(s_tx), s_rx, sizeof(s_rx));

/* 主循环 */
while (1) {
    app_isotp_frame_t f;
    while (app_isotp_port_recv(&port, &f)) { app_isotp_on_frame(&s_tp, &f); }
    app_isotp_poll(&s_tp);
}
`@

## 5. 实测

见第 29 章与 `samples/app/isotp_echo/README.md`：经典 500 kbit/s 下 1~4095 B 回显全 PASS，
4095 B RTT 273.5 ms；CAN FD 1M/2M 下 42.2 ms（6.5×）。
