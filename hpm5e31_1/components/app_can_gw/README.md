# components/app_can_gw —— cannelloni v2 兼容 CAN<->UDP 网关

> 第二阶段共享底座（tp-infra）。把板端 CAN 通道映射到主机 `vcan`，让 python-can / udsoncan /
> canopen / pyxcp / candump 等上位机工具**直接操作"真实 CAN 总线"**（第 28 章 bench B）。
> 线上格式与主机 stock cannelloni v2.1.2 逐字节兼容，也可用 `host/can_bridge/udp2vcan.py` 纯 Python 对接。

## 1. 数据通路

```
主机: python-can / candump / cansend
        |  SocketCAN
      vcan0
        |  cannelloni (-I vcan0 -R 192.168.2.100 -r 4000 -l 4000)   或 udp2vcan.py
        |  UDP 4000 + cannelloni DATA 包
   192.168.2.100 : 板端 ENET / lwIP / app_can_gw
        |  CAN1 (MCAN1, PD05/PD04, 500 kbit/s)
   板内 CAN 总线 (CAN0 <-> CAN1 物理相连)
        |  CAN0 = DUT 节点
```

- 默认映射 **CAN1 <-> UDP 4000 <-> vcan0**；
- `gw full` 可同时开 **CAN2 -> 4002 -> vcan2**、**CAN3 -> 4003 -> vcan3**；
- 回包目的地是"最近一个发数据包给板端的源 IP:PORT"（自动学习），不依赖固定主机端口，
  主机重启 cannelloni 换端口也能自动跟上。

## 2. 线上格式（cannelloni v2，全部多字节字段 **大端/网络字节序**）

UDP 载荷 = 5 字节包头 + count 个 Linux `struct canfd_frame` 的序列化：

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| +0 | 1 | `version` | 固定 2 |
| +1 | 1 | `op_code` | 0 = DATA（UDP 路径只有 DATA；ACK/NACK 未实现） |
| +2 | 1 | `seq_no` | 发送方按通道自增；接收方不校验 |
| +3 | 2 | `count` | 本包帧数，`htons`（大端） |
| +5 | 4 | `can_id` | `htonl`（大端）；bit31=CAN_EFF_FLAG(扩展帧)，bit30=CAN_RTR_FLAG(远程帧) |
| +9 | 1 | `len` | bit7=0x80 表示 CAN FD；低 7 位 = 真实字节数 0..64 |
| +10 | 1 | `flags` | 仅 FD 帧存在：Linux `canfd_frame.flags`（bit0=BRS，bit1=ESI），原样透传 |
| ... | len | `data` | 载荷；RTR 帧**没有**数据段（但 len 仍表示 DLC） |

单帧开销：经典帧 5 B/帧；FD 帧 6 B/帧。单包最多 16 帧（组件上限），最大包长
5 + 16×(4+1+1+64) = 1125 B < 1400 B，不会触碰 MTU。

`canfd_len()` 语义（ref2/cannelloni/cannelloni.h）：

```cpp
/* Helper function to get the real length of a frame */
inline uint8_t canfd_len(const struct canfd_frame *f) {
  return f->len & ~(CANFD_FRAME);   /* CANFD_FRAME = 0x80 */
}
```

也就是说 cannelloni 把"是不是 FD 帧"编码进了 `len` 的最高位，真实长度必须
`len & 0x7F`；解码时先看 bit7 决定要不要再读 1 字节 `flags`（为 BRS/ESI 透传），
再按真实长度取数据。**漏掉 `& 0x7F` 会把 0x88 当成 136 字节 → 越界/解析失败**。

## 3. API（与 `docs/应用层开发约定.md` §4.3 一致）

| 函数 | 说明 |
|---|---|
| `int app_can_gw_init(const app_can_gw_cfg_t *cfg)` | 建 CAN 通道 + UDP socket 并 bind；`NULL` = 默认（CAN1<->4000，peer 自动学习）。返回 0 成功，-1 参数，-2 CAN 初始化失败，-3 UDP socket 失败 |
| `void app_can_gw_poll(void)` | 主循环调用：把 CAN 收到的帧打包成 UDP 发出去（UDP->CAN 在 udp 回调里完成） |
| `void app_can_gw_stats(app_can_gw_stats_t *st)` | 统计快照（rx/tx UDP 包与帧、CAN 帧、drop、bad） |
| `void app_can_gw_print(void)` | 打印一行统计 |
| `void app_can_gw_set_forward(bool on)` | 运行期开关转发（shell `gw on/off`） |
| `bool app_can_gw_forwarding(void)` | 当前是否转发 |
| `int app_can_gw_set_peer(const char *ip, uint16_t port)` | 固定对端（默认自动学习） |
| `const char *app_can_gw_peer_str(void)` | 对端字符串（调试/日志） |

## 4. 配置与接入

```c
app_can_gw_cfg_t cfg;
memset(&cfg, 0, sizeof(cfg));
cfg.local_ip = NULL;                                   /* bind 0.0.0.0 */
cfg.peer_ip  = NULL;                                   /* 自动学习 */
cfg.bus_speed = APP_CAN_SPEED_CLASSIC_500K;
cfg.max_frames_per_pkt = APP_CAN_GW_MAX_FRAMES_PER_PKT; /* 16 */
cfg.ch_count = 3;
cfg.ch[0] = (app_can_gw_chan_cfg_t){ .can_ch = 1, .udp_port = 4000, .enabled = true };
cfg.ch[1] = (app_can_gw_chan_cfg_t){ .can_ch = 2, .udp_port = 4002, .enabled = true };
cfg.ch[2] = (app_can_gw_chan_cfg_t){ .can_ch = 3, .udp_port = 4003, .enabled = true };
app_can_gw_init(&cfg);
```

主循环顺序（重要）：

```c
while (1) {
    bsp_shell_poll();
    app_lwip_poll();        /* 必须先：收 UDP 包 → 网关回调 → 写 CAN */
    app_can_gw_poll();      /* 再把 CAN 收到的帧打包发 UDP */
}
```

CMake 依赖三份（都在 find_package 之后）：

```cmake
include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)
include(${REPO_ROOT}/components/app_can/app_can.cmake)
include(${REPO_ROOT}/components/app_can_gw/app_can_gw.cmake)
```

完整例程：`samples/app/can_gateway/`（网关 + CAN0 监视/回显节点 + shell 命令）。

## 5. 实测（本板，详见第 28 章与 `logs/*can_gateway*`）

| 项 | 方法 | 结果 |
|---|---|---|
| 主机 candump 看到板端心跳 | `candump vcan0`，板端 0x321 10 Hz | 待确认（需实测）——见第 28 章实测表 |
| cansend 回显 | `cansend vcan0 123#1122334455667788` -> 板端 CAN0 回 0x321 | 待确认（需实测） |
| 往返延迟 | python-can 200 次，`host/can_bridge/bench_gateway.py` | 待确认（需实测） |
| 300 帧/s 丢包 | cangen / bench_gateway.py 10 s | 待确认（需实测） |

## 6. 易错点

| # | 现象 | 原因 | 处理 |
|---|---|---|---|
| 1 | 主机 cannelloni 起来了但板端一条帧也发不出去 | 板端还没收到过主机的包，"peer" 未知，帧被丢（避免无限缓存） | 先 `cansend vcan0 ...` 或等主机周期发；或 `gw peer <ip> <port>` 固定对端 |
| 2 | 主机 candump 看到重复/突发帧 | vcan 没限速，cannelloni 攒包 100 ms（默认 `-t 100000`）一次性吐出 | `sudo ./vcan_setup.sh` 限速；隧道加 `-t 1000`（1 ms） |
| 3 | 高帧率时丢帧 | MCU 端 CAN TX FIFO 只有 8 个元素，UDP 回调里逐帧下发 | 组件已用 `timeout_us=0` 写 FIFO，满了计 `drop_can`；控制发送速率或用 `gw stat` 观察 |
| 4 | 主机端口换了以后板端不回包 | 组件会跟随新的源 IP:PORT（如果发过包） | 仍不通时 `gw peer` 手动指定 |
| 5 | 用 CAN FD 帧时主机解析错位 | 忘了 `len & 0x7F` / FD 帧多 1 字节 flags | 见 §2；主机端用 stock cannelloni 或本仓库 `udp2vcan.py`（已实现） |
| 6 | UDP 包丢失后没有重传 | cannelloni UDP 只有 DATA，没有 ACK/NACK | 由上层协议（UDS N_Ar/N_As、J1939 TP、CANopen SDO 重传）保证；文档见第 28 章 §28.8 |

## 7. 文件

```
components/app_can_gw/
├── inc/app_can_gw.h     # 协议常量 + 配置/统计/API
├── src/app_can_gw.c     # 编解码、UDP 回调、CAN->UDP 打包、统计
├── app_can_gw.cmake
└── README.md
```
