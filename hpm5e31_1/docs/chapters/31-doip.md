# 第 31 章 DoIP：以太网诊断（ISO 13400-2）

## 31.1 文档信息

表 31-1 文档信息

| 项 | 内容 |
|---|---|
| 章号 / 主题 | 第 31 章 / DoIP（Diagnostic communication over Internet Protocol，ISO 13400-2）服务端 |
| 对应例程 | `samples/app/doip_server`（单固件：lwIP + DoIP + UDS） |
| 组件 | `components/app_doip`（新增）、复用 `components/app_lwip`、`components/app_uds` |
| 上位机 | `host/doip/doip_client.py`、`host/doip/run_tests.py`、`host/doip/run_client.sh` |
| 图生成脚本 | `tools/figs/app32_doip_arch.py`、`app32_doip_frames.py`、`app32_doip_fsm.py`、`app32_doip_seq.py`、`app32_doip_perf.py` |
| 硬件 | HPM5E31（480 MHz，RV32）+ YT8011AN（1000BASE-T1，RGMII）；主机 `enp6s0` = 192.168.2.1/24 |
| 软件基线 | 板 IP 192.168.2.100/24、lwIP 2.1.2（NO_SYS=1）、UDS 服务端见第 30 章 |
| 实测日期 | 2026-11-02 |
| 可信度 | 上板实测（数据全部来自 `logs/` 与 `host/doip` 的日志，命令可复跑）；0x0009/0x000A 的载荷类型编号按 ISO 13400-2 更正为 0x4003/0x4004，见 §31.13 坑 1 |
| 主要日志 | `logs/20261102-155909-doip_server.log`、`logs/20261102-154435-doip_server.log`、`logs/20261102-155150-doip_server.log`，主机侧 `logs/*-host-doip-*.log` |

## 31.2 本章导览

这一章把"诊断仪怎么用一根网线跟 ECU 说话"讲完：从 UDP 广播找车、TCP 建连做路由激活、把 UDS 报文塞进 DoIP 帧、一直到 4 KB 大报文和超时重连。读完之后你应该能回答：

- 为什么整车诊断从 CAN 走到以太网，DoIP 在架构里到底站在哪一层（图 31-1、§31.12 的对比表）；
- 通用头那 8 个字节每一个是干什么的，0x0001 到 0x8003 十几个载荷类型分别用 UDP 还是 TCP（表 31-3、表 31-4）；
- 一条 TCP 连接从建立到被踢掉经过哪些状态、2 s 和 300 s 这两个定时器分别管什么（图 31-3、表 31-5）；
- 我们的实现哪里跟标准有偏差、哪里有坑（§31.13 一共 9 条，带 tcpdump 证据）。

本章不是"协议翻译"：所有数字都是本板跑出来的，代码行号、日志路径、抓包结论都能回查。

## 31.3 概述：诊断为什么要从 CAN 走到以太网

CAN 时代的诊断走 ISO-TP（ISO 15765-2，第 29 章）：8 字节一帧，经典 500 kbit/s 下一条 4 KB 的刷写数据要拆成几百帧，光搬数据就要上百毫秒。刷一个 1 MB 的固件镜像，CAN 上要几十秒到几分钟。到了域控制器、中央计算平台这一代，ECU 里本来就有百兆/千兆以太网，诊断自然搬到网上：**ISO 13400-2 规定的 DoIP 就是"UDS 报文在 IP 网络上的运输层"**。

DoIP 只做四件事：找到车（车辆发现/声明）、确认"这条连接被允许诊断"（路由激活）、把 UDS 字节流按帧收发（诊断报文 0x8001 + ACK/NACK）、管住连接不泄漏（alive check 与两个 inactivity 定时器）。UDS 服务本身（0x10/0x22/0x2E/0x27/0x31……）一行都不用改——这正是它能替代 ISO-TP 的原因：**上层诊断数据库（CDD/ODX）复用，只换运输层**。

![图 31-1 DoIP 在整车诊断架构中的位置](figures/31/fig-31-1-arch.png)

图 31-1 里有两个细节值得注意。第一，DoIP 实体可以是一个"节点"（node，本身就有 UDS 服务），也可以是一个"网关"（gateway，把诊断转发到车内 CAN 子网）。本板实现的是**节点**：诊断报文直接送本板 `components/app_uds`，不做跨网段转发（`node_type = 0x00`，见 0x4001 响应）。第二，外部测试仪的**逻辑地址**（0x0E00~0x0EFF）和车内 ECU 的逻辑地址（0x0001~0x0DFF）是分开的地址段——0x8001 里的 SA/TA 就是这两个地址，不是 IP 地址，也不是 CAN ID。

表 31-2 本章术语

| 术语 | 含义 | 本实现取值 |
|---|---|---|
| DoIP 实体 | 提供 DoIP 服务的 ECU/网关 | 本板，逻辑地址 0x1000 |
| 测试仪（tester） | 诊断仪/OTA 服务器 | 主机 Python 脚本，逻辑地址 0x0E00 |
| 逻辑地址（LA） | 2 字节诊断地址，替代 CAN ID | 0x1000 / 0x0E00 |
| EID | 实体标识，6 B，推荐等于 MAC 地址 | 板 MAC（本板读出来是 `00:00:00:00:D0:0E`） |
| GID | 组标识，6 B，同"车"内多个实体共用 | `00:00:00:00:00:01` |
| 车辆声明 / 识别响应 | 0x0004，把 VIN/EID/GID/逻辑地址告诉测试仪 | 上电发 3 次 + 可配周期声明 |

## 31.4 工作原理

### 31.4.1 通用头与载荷类型

DoIP 每条报文都是"8 字节通用头 + 载荷"。通用头只有 4 个字段，但**版本取反字节**是它最容易被抓到的特征：0x02 的取反必须是 0xFD，写错就直接回一个 0x0000 NACK 并断链。这一点在 §31.13 坑 1 有实测。

载荷类型不是连续编号：0x0001~0x0008 是"会话类"，0x4001~0x4004 是"实体状态/电源模式类"，0x8001~0x8003 是"诊断报文类"。中间空出来的号段是给 ISO 以后扩展留的，所以看到 0x0009 不要以为是电源模式——标准里电源模式是 0x4003/0x4004。

表 31-3 DoIP 载荷类型与运输层分工（ISO 13400-2 表 6~表 9 摘要）

| 载荷类型 | 名称 | UDP | TCP | 载荷长度 | 本实现 |
|---|---|---|---|---|---|
| 0x0000 | 通用头否定应答（NACK） | ✔ | ✔ | 1 B | ✔ 5 种 NACK 码 |
| 0x0001 | 车辆识别请求（无参数） | ✔ | ✘ | 0 | ✔ |
| 0x0002 | 车辆识别请求（带 EID） | ✔ | ✘ | 6 B | ✔ 全 FF 视为广播 |
| 0x0003 | 车辆识别请求（带 VIN） | ✔ | ✘ | 17 B | ✔ 全 0 视为广播 |
| 0x0004 | 车辆声明 / 车辆识别响应 | ✔ | ✘ | 32 B（2019 为 33 B） | ✔ |
| 0x0005 | 路由激活请求 | ✘ | ✔ | 7 B 或 11 B | ✔ 两种都收 |
| 0x0006 | 路由激活响应 | ✘ | ✔ | 13 B | ✔ 0x00~0x11 响应码 |
| 0x0007 | alive check 请求 | ✘ | ✔ | 0 | ✔ |
| 0x0008 | alive check 响应 | ✘ | ✔ | 2 B | ✔ |
| 0x4001 / 0x4002 | 实体状态请求 / 响应 | ✔ | ✘ | 0 / 7 B | ✔ 上报 max data size=516 |
| 0x4003 / 0x4004 | 诊断电源模式请求 / 响应 | ✔ | ✘ | 0 / 1 B | ✔ |
| 0x8001 | 诊断报文 | ✘ | ✔ | 4 B + n | ✔ 转 UDS |
| 0x8002 / 0x8003 | 诊断报文肯定 / 否定应答 | ✘ | ✔ | 5 B | ✔ |

### 31.4.2 TCP 与 UDP 的分工

为什么发现用 UDP、诊断用 TCP？因为**发现是"一问多答、允许丢"**：UDP 广播问一次，网上所有 DoIP 实体都回，丢一条也无所谓，重问就行；而且 UDP 不需要先建连，测试仪在还不知道车在哪的时候就能发。**诊断是"一问一答、不能丢、可能很长"**：4 KB 的刷写块、几秒的 0x78 pending，都需要 TCP 的可靠有序流。

代价是 TCP 带来"流"的问题：一条 TCP 连接里字节是连续不断的，**DoIP 帧边界必须自己按通用头的长度字段切**。我们踩过一次：第一版把 `recv()` 返回的整块当一帧，主机把 0x0006 响应（21 B）一次读出来就 `struct.unpack` 失败——那是上位机的 bug，但它说明"流式传输 + 定长头"这个组合必须两边都按状态机写。

表 31-4 UDP 与 TCP 在 DoIP 里的职责

| 维度 | UDP 13400 | TCP 13400 |
|---|---|---|
| 承载载荷 | 0x0001~0x0004、0x4001~0x4004、0x0000 | 0x0005~0x0008、0x8001~0x8003、0x0000 |
| 连接 | 无连接，临时 socket | 需要三次握手，一条连接一个 tester |
| 帧边界 | 一个数据报一帧，天然有边界 | 必须按 8 B 头 + 长度字段切帧 |
| 广播 | 车辆声明/识别响应可以广播 | 不能广播 |
| 上限 | 受 MTU 限制（本实现请求 ≤64 B） | 受 max data size 限制（本实现帧 4096 B，交 UDS 516 B） |
| 本实现关键 API | `app_udp_socket()`、`udp_recv()`、`udp_sendto()` | `tcp_new/bind/listen/accept/recv/write/output` |

### 31.4.3 TCP 分帧：一次 recv 不等于一帧

片上实现里，分帧状态机只有三个字段：`rx_hdr_got`（已收头字节数）、`rx_want`（头里声明的载荷长度）、`rx_got`（已收载荷字节数）。攒够 8 字节头就校验版本取反，然后按 `rx_want` 收载荷，收满就分发：

```c
/* components/app_doip/src/app_doip.c，conn_feed() 的骨架 */
while (i < len) {
    if (c->rx_hdr_got < APP_DOIP_HDR_LEN) {
        n = APP_DOIP_HDR_LEN - c->rx_hdr_got;  if (n > len - i) n = len - i;
        memcpy(&c->rx_hdr[c->rx_hdr_got], &data[i], n);
        c->rx_hdr_got += n;  i += n;
        if (c->rx_hdr_got == APP_DOIP_HDR_LEN) {
            if (!conn_begin_payload(d, c)) return;   /* 校验失败：回 NACK + 待关闭 */
        }
        continue;
    }
    n = c->rx_want - c->rx_got;  if (n > len - i) n = len - i;
    memcpy(&c->rx_buf[c->rx_got], &data[i], n);
    c->rx_got += n;  i += n;
    if (c->rx_got == c->rx_want) conn_frame_done(d, c);   /* 分发 + 复位状态 */
}
```

`on_tcp_recv()` 里有一个顺序细节：**先 `tcp_recved()` 再解析**。因为在解析里可能直接 `tcp_close()`（帧错、载荷超限），关掉之后再对已释放的 pcb 调 `tcp_recved()` 就是野指针。lwIP 的 `tcp_err` 回调更狠：进去时 pcb 已经被 lwIP 释放，**绝对不能再 `tcp_close()`**，只能清自己的槽位。

### 31.4.4 定时器：2 s 与 300 s

DoIP 用两个 inactivity 定时器管连接生命周期，再加一个 alive check 探活：

表 31-5 DoIP 定时器（本实现可运行期修改）

| 定时器 | ISO 符号 | 标准典型值 | 本实现默认 | 作用 | 超时行为 | 实测 |
|---|---|---|---|---|---|---|
| 初始不活动 | A_DoIP_Initial_Inactivity | 2 s | 2000 ms | TCP 建连后等 0x0005 | 直接断链 | 2000.28 ms |
| 通用不活动 | A_DoIP_General_Inactivity | 300 s（5 min） | 300000 ms | 已激活连接的保活上限 | 断链，槽位释放 | 2998.49 ms（测试改 3000 ms） |
| alive check | A_DoIP_AliveCheck_Timeout | 500 ms | 500 ms | 发 0x0007 后等 0x0008 | 断链（踢掉老 socket） | 同 SA 抢占用例 PASS |
| 声明间隔 | A_DoIP_Announce_Interval | 500 ms | 500 ms | 上电 3 次声明的间隔 | — | 1000.09 ms（测试改 1000 ms） |
| 声明等待 | A_DoIP_Announce_Wait | 0~500 ms 随机 | 500 ms | 上电到第一次声明的随机抖动 | — | 由 xorshift 生成 |

"2 s 内必须发路由激活"这条容易被忽略：测试仪连上 TCP 后如果先做别的事（读 VIN、等用户操作），2 s 一到连接就被 ECU 关掉，主机侧表现为"连上了但立刻断"。§31.13 坑 6 有复现方法。

## 31.5 帧格式逐字节

![图 31-2 通用头与四类载荷的逐字节布局](figures/31/fig-31-2-frames.png)

表 31-6 通用头 8 字节

| 偏移 | 长度 | 字段 | 取值 / 说明 | 本实现代码 |
|---|---|---|---|---|
| 0 | 1 B | 协议版本 | 0x02 = ISO 13400-2:2012；0x03 = 2019；0xFF = 请求里"任意版本" | `APP_DOIP_VER_2012` |
| 1 | 1 B | 版本取反 | 必须等于 `版本 ^ 0xFF`（0x02 → 0xFD） | `APP_DOIP_VER_INV()` |
| 2~3 | 2 B | 载荷类型 | 大端，见表 31-3 | `app_doip_put_u16()` |
| 4~7 | 4 B | 载荷长度 | 大端，**不含** 8 B 头 | `app_doip_put_u32()` |

表 31-7 四类常用载荷的字段布局

| 载荷 | 偏移 | 字段 | 长度 | 说明 |
|---|---|---|---|---|
| 0x0004 车辆声明 | 0 | VIN | 17 B | ASCII，不满 17 位补 0x00 |
| | 17 | 实体逻辑地址 | 2 B | 本板 0x1000 |
| | 19 | EID | 6 B | 本板取 MAC |
| | 25 | GID | 6 B | 组标识 |
| | 31 | further action | 1 B | 0x00 = 无需额外动作；0x10 = 需要路由激活 |
| | 32 | VIN/GID 同步状态 | 1 B | 仅 2019 版（本实现默认不发） |
| 0x0005 路由激活请求 | 0 | 源地址 SA | 2 B | 测试仪逻辑地址，如 0x0E00 |
| | 2 | 激活类型 | 1 B | 0x00 默认、0x01 WWH-OBD、0xE0 中央安全 |
| | 3 | ISO 保留 | 4 B | 全 0 |
| | 7 | OEM 特定 | 4 B | **可选**：标准 11 B，实测很多工具只发 7 B |
| 0x0006 路由激活响应 | 0 | 测试仪地址 | 2 B | 原样回请求里的 SA |
| | 2 | 实体地址 | 2 B | 0x1000 |
| | 4 | 响应码 | 1 B | 表 31-8 |
| | 5 / 9 | ISO 保留 / OEM | 4 B / 4 B | 本实现全 0 |
| 0x8001 诊断报文 | 0 | 源地址 SA | 2 B | 发送方逻辑地址 |
| | 2 | 目标地址 TA | 2 B | 接收方逻辑地址 |
| | 4 | UDS 数据 | n B | 就是第 30 章的 UDS PDU，原样搬运 |
| 0x8002/0x8003 | 0 | 实体地址 | 2 B | 见 §31.13 坑 4：方向与直觉相反 |
| | 2 | 测试仪地址 | 2 B | |
| | 4 | 应答码 | 1 B | 0x00 成功 / 0x02~0x08 见表 31-14 |

## 31.6 路由激活状态机

路由激活是 DoIP 的"准入控制"：TCP 连上不代表能诊断，必须先发 0x0005，ECU 回 0x0006 且响应码 0x10 之后，这条连接才能收发 0x8001。为什么要这么设计？因为以太网口在车上可能连到多个测试仪/多个网段，ECU 需要知道"谁在用哪个逻辑地址诊断"，才能把诊断响应发回去、才能做并发连接限制。

![图 31-3 单条 TCP 连接的 DoIP 状态机](figures/31/fig-31-3-fsm.png)

表 31-8 路由激活响应码（0x0006 第 5 字节）

| 响应码 | 含义 | 本实现触发条件 |
|---|---|---|
| 0x00 | 拒绝：未知源地址 | SA 不在 0x0001~0x0EFF，或与配置的 tester 地址不符 |
| 0x01 | 拒绝：没有可用 socket | 连接槽满（MAX_CONN=3，新连接直接 abort） |
| 0x02 | 拒绝：与已激活的 SA 不同 | 同一条连接已激活、又换了个 SA |
| 0x03 | 拒绝：该 SA 已在别的 socket 激活 | 同 SA 第二条连接（先对老连接发 0x0007） |
| 0x04 | 拒绝：缺少鉴权 | `cfg.require_oem_auth` 打开但 OEM 字段不匹配 |
| 0x05 | 拒绝：确认被拒 | 保留（本实现不产生） |
| 0x06 | 拒绝：不支持的激活类型 | 激活类型不在 `cfg.activation_mask` 里 |
| 0x10 | 成功 | 正常路径 |
| 0x11 | 成功但需确认 | 保留（本实现不产生） |

表 31-9 连接状态迁移

| 当前状态 | 事件 | 下一状态 | 动作 |
|---|---|---|---|
| CLOSED | TCP 三次握手完成 | CONNECTED | 分配槽位、记录对端 IP:port、启 2 s 定时器 |
| CONNECTED | 收到 0x0005 且校验通过 | ACTIVATED | 记 tester SA、回 0x0006/0x10、启 300 s 定时器 |
| CONNECTED | 收到 0x0005 但校验失败 | CONNECTED | 回 0x0006 + 拒绝码，连接保留（允许重试） |
| CONNECTED | 2 s 无 0x0005 | CLOSED | `tcp_close()`，`st_initial_timeout++` |
| ACTIVATED | 收到合法 0x8001 | ACTIVATED | 刷新 general 定时器、回 0x8002、UDS 入队 |
| ACTIVATED | 收到 0x8001 但 SA/TA 非法 | ACTIVATED | 回 0x8003 + 0x02/0x03，不断链 |
| ACTIVATED | 同 SA 的新连接请求激活 | ALIVE_CHECK（老连接） | 发 0x0007，500 ms 等 0x0008 |
| ALIVE_CHECK | 收到 0x0008 | ACTIVATED | 老 socket 继续用，新连接收 0x0006/0x03 |
| ALIVE_CHECK | 500 ms 无 0x0008 | CLOSED | 踢掉老 socket，新连接下次可激活 |
| ACTIVATED | 300 s 无任何报文 | CLOSED | `tcp_close()`，`st_general_timeout++` |

路由激活的判定顺序在代码里是"先地址、再类型、再鉴权、最后抢占用"，这个顺序决定了测试仪看到的第一个拒绝码。比如 SA 非法时先回 0x00 而不是 0x06，否则排查会跑偏：

```c
/* components/app_doip/src/app_doip.c，handle_route_activation() 的判定顺序 */
if (!sa_is_valid(ra.sa))                                    code = APP_DOIP_RA_UNKNOWN_SA;       /* 0x00 */
else if (cfg.tester_addr && ra.sa != cfg.tester_addr)       code = APP_DOIP_RA_UNKNOWN_SA;
else if (!activation_type_allowed(cfg, ra.activation_type)) code = APP_DOIP_RA_UNSUPPORTED_AT;  /* 0x06 */
if (code == OK && cfg.require_oem_auth &&
    (!ra.has_oem || memcmp(ra.oem, cfg.oem_key, 4) != 0))   code = APP_DOIP_RA_MISSING_AUTH;    /* 0x04 */
for (已激活的其它连接)
    if (o->tester_addr == ra.sa) { app_doip_alive_check(o); code = APP_DOIP_RA_SA_USED; break; } /* 0x03 */
```

## 31.7 车辆发现与声明

发现流程有三种问法，对应表 31-3 的 0x0001/0x0002/0x0003：不带参数（谁在都能答）、带 EID（只答 EID 匹配的，全 FF 表示广播）、带 VIN（只答 VIN 匹配的，全 0 表示广播）。本实现三种都答，且"全 FF / 全 0"按广播处理——这一点和 Vector 工具、EcuBus-Pro 的行为一致。

声明（0x0004）我们实现了两套触发：**上电 3 次**（标准要求，间隔 500 ms、首次前随机等 0~500 ms）和**周期声明**（OEM 扩展，`doip timer period <ms>` 打开，便于测试仪随时发现）。周期声明标准里没有，但很多车厂的"永远在线"OTA 场景需要，所以做成可配置、默认关。

表 31-10 车辆声明参数

| 参数 | 配置项 | 默认 | 运行期命令 | 实测 |
|---|---|---|---|---|
| 声明次数 | `cfg.announce_num` | 3 | `doip announce [n]` | 上电 3 次 |
| 声明间隔 | `cfg.announce_interval_ms` | 500 ms | `doip timer announce <ms>` | 1000.09 ms（改 1000 后） |
| 首次等待 | `cfg.announce_wait_ms` | 500 ms | 编译期 | 0~500 ms 随机 |
| 周期声明 | `cfg.announce_period_ms` | 0（关） | `doip timer period <ms>` | 4 s 收到 5 条，间隔偏差 0.23 ms |
| EID | `cfg.eid` | 板 MAC | `doip eid 12位HEX` | `00:00:00:00:D0:0E` |
| VIN | `cfg.vin` | `HPM5E31TESTVIN001` | `doip vin <17字符>` | 写 VIN 后 0x0004 立刻跟着变 |

一个容易忽略的联动：UDS 的 0x2E F190 可以改 VIN，改完之后 **DoIP 的 0x0004 也必须报新 VIN**，否则测试仪"发现到的 VIN"和"UDS 读到的 VIN"打架。本实现在每次 UDS 处理完调 `sync_vin_from_uds()`，把 F190 的内容同步回 `s_doip.cfg.vin`。

## 31.8 寄存器与驱动 API 对照

DoIP 是应用层协议，不直接碰寄存器；但"帧怎么出去、什么时候出去"完全由 ENET 外设和 lwIP 决定。下表把 DoIP 的每个动作映射到"ENET 寄存器 / lwIP API / 本仓库代码位置"，方便出问题时从协议层往下追。

表 31-11 协议动作 ↔ ENET 寄存器 ↔ 驱动 API ↔ 代码位置

| DoIP 动作 | ENET 侧 | lwIP API | 本仓库代码 |
|---|---|---|---|
| 建 UDP 13400 收发现请求 | DMA 描述符环 `ENET_DMA_CH0_RXDESC_START` | `udp_new/bind/recv` | `app_doip_start()` |
| 回 0x0004 车辆声明（广播） | TX 描述符 + `ENET_DMA_CH0_TXDESC_START` | `pbuf_alloc/pbuf_take/udp_sendto` | `doip_udp_send()` |
| 监听 TCP 13400 | 同上；TCP 段由 MAC 硬件校验和 | `tcp_new/bind/listen_with_backlog/accept` | `app_doip_start()` |
| 收 TCP 数据、切帧 | RX 描述符状态位 `OWN/FS/LS` | `tcp_recv` 回调 + `tcp_recved` | `on_tcp_recv()` / `conn_feed()` |
| 回 0x0006/0x8002/0x8001 | TX 描述符 `TBS1/TDES2` | `tcp_write(TCP_WRITE_FLAG_COPY)` + `tcp_output` | `conn_send_frame()` |
| 发送环满时重发 | `ENET_DMA_STATUS_TI`（发送完成） | `tcp_output` 重试（否则等 RTO 200 ms） | `app_doip_poll()` 里每轮踢一次 |
| 连接超时断链 | — | `tcp_close` | `conn_close()` |
| 链路 up/down | PHY 寄存器 `PHYSR/GBSR/BMSR`（见第 12 章） | `netif_is_link_up` | `app_lwip_poll()` |

## 31.9 例程：doip_server

例程只有一个固件，但把 DoIP 服务端、UDS 服务端、shell 全装进去了。构建、烧写、上位机三步：

```bash
cd hpm5e31_1
# 1) 编译
bash scripts/build.sh samples/app/doip_server
# 2) 烧写 + 串口抓取 + 主机侧跑全部用例（一条命令，自带整机锁）
bash scripts/hwrun.sh app/doip_server --secs 45 \
     --send "doip timer period 1000;doip timer general 3000" \
     --post "timeout 150 bash host/doip/run_client.sh all" --lock-wait 1800
# 3) 只跑某个用例 / 单独跑上位机（板子在跑时）
python3 host/doip/doip_client.py discover --count 5
python3 host/doip/doip_client.py uds
python3 host/doip/doip_client.py bulk --size 4000 --count 20
```

板端 shell 命令（letter-shell，`doip` 一个主命令带子命令）：

表 31-12 doip_server 的 shell 命令

| 命令 | 作用 | 典型用法 |
|---|---|---|
| `doip` | 用法 + 全局统计 | `doip` |
| `doip start` / `doip stop` | 起停 TCP+UDP 13400 监听 | `doip stop;doip start` |
| `doip stat` | 收发包、路由激活、超时、alive check、大报文计数 | `doip stat` |
| `doip conn` | 每条 TCP 连接的状态/对端/收发计数 | `doip conn` |
| `doip announce [n]` | 立即发 n 次车辆声明 | `doip announce 5` |
| `doip vin [17字符]` | 读/写 VIN（写会同步 UDS DID F190） | `doip vin HPM5E31DOIPVIN001` |
| `doip eid [12位HEX]` | 读/写 EID | `doip eid 02005E100001` |
| `doip route` | 路由激活策略与连接表 | `doip route` |
| `doip timer <名> <ms>` | initial/general/alive/announce/period | `doip timer general 3000` |
| `doip alive [idx]` | 主动对某连接发 0x0007 | `doip alive 0` |
| `doip selftest` | 编解码固定向量自检（49 项，不跑网络） | `doip selftest` |
| `doip bench [n] [cnt]` | DoIP/TCP 大报文吞吐注入 | `doip bench 4000 20` |
| `doip uds` | UDS 服务端统计（会话/安全/DID/例程） | `doip uds` |

上位机的用法（`host/doip/doip_client.py`，纯标准库）：

```text
$ python3 host/doip/doip_client.py discover --count 5
== 车辆发现（UDP 0x0001/0x0002/0x0003 -> 0x0004）==
  [plain] VIN=HPM5E31TESTVIN001 逻辑地址=0x1000 EID=00:00:00:00:D0:0E GID=00:00:00:00:00:01 ...
       RTT min/avg/max = 119.3 / 163.7 / 318.4 us（5 次）
```

## 31.10 实测

全套用例 16 项，命令就是 §31.9 的第 2 条。板端计数与主机侧时延同时记录，下表的数字都能在日志里逐条对上。

表 31-13 时延与功能实测（2026-11-02，主机侧日志 `logs/20261102-163907-host-doip-runtests.log`）

| 项目 | 实测 | 说明 | 日志 |
|---|---|---|---|
| UDP 0x0001 发现 RTT 均值 / 最大 | 129.6 µs / 289.9 µs | n=5，含 Python 侧开销 | host-doip-runtests |
| UDP 0x0002 带 EID 发现 | 通过（RTT 与 0x0001 同量级） | 全 FF 广播 EID | host-doip-runtests |
| UDP 0x0003 带 VIN 发现 | 通过（RTT 与 0x0001 同量级） | 全 0 广播 VIN | host-doip-runtests |
| UDP 0x4001 实体状态 RTT | 99.8 µs | 回 max_sockets=3、max_data=516 | host-doip-runtests |
| UDP 0x4003 电源模式 RTT | 81.5 µs | 回 0x01（就绪） | host-doip-runtests |
| 周期声明 | 4 s 收到 5 条，间隔 1000.05 ms、偏差 0.89 ms | 板端 doip timer period 1000 | host-doip-runtests |
| 路由激活 RTT | 1219.7 µs | 含 TCP 建连后的第一来一回 | host-doip-runtests |
| 0x8002 肯定应答时延 | 167.0 µs | 回调只入队、不阻塞 | host-doip-runtests |
| 3E 00 单次往返 | 229.0 µs | n=20 均值，TCP 已建连 | host-doip-runtests |
| 慢服务（0x78 + P2* 续期） | 150.6 ms，pending=1 | 例程 0x31 01 0301 故意阻塞 150 ms | host-doip-runtests |
| initial inactivity 断链 | 2000.40 ms | 连上不发 0x0005 | host-doip-runtests |
| general inactivity 断链 | 2998.70 ms | 板端 doip timer general 3000 | host-doip-runtests |
| UDS 服务序列 | 0x10/0x22/0x2E/0x27/0x31/0x19/0x3E 全 PASS | 走 0x8001 转发到第 30 章的 UDS | host-doip-runtests |
| DoIP 编解码自检 | 49 项检查 0 失败 | 板端 doip selftest | `logs/20261102-163909-doip_server.log` |
| 回归套件 | 16 个用例 15 PASS / 1 已知限制 | 见 §31.14（多帧突发卡住） | host-doip-runtests |

> 说明：表里的时延是**主机 Python 侧**测量的往返时间，包含 socket 收发与解释器开销，比板端纯处理时间偏大（0x8002 的 167 µs 里大部分是收包唤醒与 Python 开销）。串口日志里只有板端计数与打印，不可能有 RTT——RTT 只认 host 日志。

![图 31-4 一次完整 DoIP 诊断的时序](figures/31/fig-31-4-sequence.png)

表 31-14 异常用例（全部为"故意做错"的输入）

| 用例 | 输入 | 期望 | 实测 |
|---|---|---|---|
| 未知 SA | 0x0005 SA=0x1234 | 0x0006 响应码 0x00 | PASS |
| 未配置的 SA | 0x0005 SA=0x0E80（板端配 0x0E00） | 0x0006 响应码 0x00 | PASS |
| 非法激活类型 | 0x0005 激活类型 0x02 | 0x0006 响应码 0x06 | PASS |
| 未激活就发诊断 | 0x8001 在 CONNECTED 状态 | 0x8003 否定应答 0x02 | PASS |
| 错误目标地址 | 0x8001 TA=0x2000 | 0x8003 否定应答 0x03 | PASS |
| 非法源地址 | 0x8001 SA=0x0000 | 0x8003 否定应答 0x02 | PASS |
| 通用头版本取反错 | 版本 0x02 取反 0x00 | 0x0000 NACK 0x00 后断链 | PASS |
| 未知载荷类型 | 0x1234 | 0x0000 NACK 0x01 | PASS |
| 同 SA 双连接 | 两条 TCP 同 SA 激活 | 新连接 0x0006/0x03，老连接收 0x0007 | PASS |
| alive check 超时 | 老连接不应答 0x0007 | 500 ms 后老连接被踢、新连接可激活 | PASS |
| 超时重连 | 连上不发路由激活 | 2 s 后断链，重连可正常激活 | PASS |
| UDS 超过 512 B | 0x8001 里带 4000 B UDS | 0x8003/0x04（diagnostic message too large），连接保留 | PASS |

## 31.11 性能

DoIP 的性能瓶颈不在协议解析（一帧几百纳秒），而在"TCP 发送环 + lwIP 内存"这一层。表 31-15 是不同报文尺寸下的实测与资源占用。

表 31-15 大报文与资源（同一份 host 日志）

| 项目 | 数值 | 备注 |
|---|---|---|
| 入向 4008 B 帧（4000 B UDS）的否定应答时延 | 266.0 µs | 按 ISO 回 0x8003/0x04；帧被完整收下并分帧（3 个 TCP 段） |
| 入向 516 B 帧（512 B UDS）ACK+响应时延 | 236.3 µs | 正常交给 UDS（回 NRC 0x33，0x2E 未解锁） |
| 出向 4008 B 帧单帧总耗时 | 375.3 µs | 含 0x8002 ACK，单连接 |
| 出向 4008 B 帧吞吐 | 85.4 Mbit/s | 单帧（4008 B / 375.3 µs） |
| `components/app_doip` Flash 增量 | text 11 324 B（11.1 KB）：app_doip.c 8510 + app_doip_wire.c 2814 | -O3，两个 .obj 的 text 之和（不含 UDS 复用与 lwIP） |
| `components/app_doip` 自身静态 RAM | bss 4104 B（4.0 KB，发送拼包缓冲） | `app_doip.c.obj` 的 bss |
| 例程静态 RAM | 26 096 B（25.5 KB）：3×4096 连接接收缓冲 + 4104 拼包 + 8 KB UDS 请求队列 + 杂项 | `samples/app/doip_server/src/main.c` 的 bss 口径 |
| 整片镜像 | text 140 576 B / data 496 B / bss 153 856 B → Flash 137.8 KB、RAM 150.7 KB | `riscv32-unknown-elf-size demo.elf` |
| lwIP 参数 | TCP_SND_BUF = 8×MSS（11.68 KB）、TCP_WND = 8×MSS、MEM_SIZE = 24 KB | `components/app_lwip/inc/lwipopts_app.h` |

![图 31-5 DoIP 上板实测：时延与大报文](figures/31/fig-31-5-perf.png)

## 31.12 与 CAN ISO-TP 的对比

表 31-16 把两条传输路线放在一起比。数据来自本板实测（第 29 章的 ISO-TP、本章的 DoIP）和公开资料，**不是"以太网一定更好"的结论**：线束、成本、实时性各有取舍。

| 维度 | CAN + ISO-TP（第 29 章） | DoIP（本章） | 说明 |
|---|---|---|---|
| 物理层 | CAN 2.0 / CAN FD，双绞线 | 100/1000BASE-T1 单对双绞 | T1 就是为了车载单对线束 |
| 单帧上限 | 经典 7 B、CAN FD 64 B | 4096 B（本实现，可配） | DoIP 一帧顶 ISO-TP 几百帧 |
| 有效带宽 | 经典 500 kbit/s；FD 2 Mbit/s 数据段 | 100 Mbit/s / 1 Gbit/s | 差 2~3 个数量级 |
| 4 KB 刷写块搬运 | 约 60~80 ms（500k，含 FC 往返） | 单连接 4 KB 数百 µs 级（见表 31-15） | 大文件刷写差距最明显 |
| 诊断单请求 RTT | 478 µs（第 29 章实测，含板内 tester） | 279 µs~1.3 ms（含 Python 开销） | 小报文上 DoIP 优势不明显 |
| 连接/寻址 | CAN ID + 29 位 J1939 式寻址 | IP + 逻辑地址 SA/TA | DoIP 需要额外"发现"步骤 |
| 拓扑 | 总线，天然广播 | 交换式，点对点 | 以太网需要交换机/网关 |
| 成本 | 收发器便宜，线束成熟 | PHY + 变压器 + 交换机，贵 | 域控里网口本来就有，边际成本低 |
| 实时性 | 仲裁决定，确定性好 | 交换式，有排队抖动 | 安全相关报文仍走 CAN/TSN |
| 标准配套 | ISO 15765-2 / 14229-1 | ISO 13400-2 / 14229-1（同一套 UDS） | 上层诊断数据库可复用 |

一句话选型：**刷写、大文件、日志回捞走 DoIP；故障灯、10 ms 级控制、安全相关走 CAN/TSN**。两者不是替代关系，是同一辆车上并存的两条诊断路径。

## 31.13 坑

1. **载荷类型编号：0x0009/0x000A 是错译**。任务书最初写"诊断电源模式 0x0009/0x000A"，实际 ISO 13400-2 里是 **0x4003/0x4004**（0x0009~0x000F 目前未定义）。EcuBus-Pro 的 `src/main/doip/index.ts` 也是 0x4003/0x4004，写错的话主机侧只会收到 0x0000 NACK 0x01（未知载荷类型）。
2. **路由激活请求可能是 7 B，不是 11 B**。ISO 规定 11 B（带 4 B OEM 字段），但实测 EcuBus-Pro 默认发 **7 B**（`buildRouteActivePayload()` 就返回 7 B，带 OEM 才是 11 B）。第一版按 11 B 严格校验，结果主机怎么都激活不了。现在 7 B/11 B 都收。
3. **lwIP 回调里别阻塞，否则 0x8002 ACK 会迟到**。第一版在 `tcp_recv` 回调里直接调 `app_uds_handle()`，慢服务阻塞 150 ms，tcpdump 显示 ACK、0x78、最终响应被拼进同一个 TCP 段、一起在 150 ms 后才发出（`logs/20261102-155150-doip_server.log` 对应的抓包）。改成"回调只入队、主循环处理"之后 ACK 时延 **171 µs**。根因是 ENET 发送环在阻塞期间不再被轮询，`tcp_output` 出去的数据只能等下一轮。
4. **0x8002/0x8003 的 SA/TA 方向与直觉相反**。应答里的第一个地址是**实体**（发送方），第二个是测试仪。EcuBus-Pro 解析 0x8003 时校验 `sa == entity.logicalAddr && ta == testerAddr`；如果按"原报文 SA/TA 原样回"就会失败。
5. **TCP_SND_BUF 只够 2 条 4 KB 报文**。`TCP_SND_BUF = 8×MSS = 11.68 KB`，连续写 4 KB 帧时第 3 条起 `tcp_write` 返回 `ERR_MEM`。批量发送必须在帧之间跑一次 `app_lwip_poll()` 回收发送环；单帧 4008 B 实测 375.3 µs / 85.4 Mbit/s（表 31-15）。
6. **上电 2 s 内必须发路由激活**。测试仪连上 TCP 后先干别的（读 VIN、等用户点按钮），`A_DoIP_Initial_Inactivity` 一到连接就被关。复现：`python3 -c "import socket,time; s=socket.create_connection(('192.168.2.100',13400)); time.sleep(4); print(s.recv(16))"`，4 s 后 `recv` 返回空。
7. **UDP 广播要 bind 到 0.0.0.0 才收得到**。板端车辆声明发到 255.255.255.255:13400；主机 socket 如果 bind 到 `192.168.2.1:13400`，就收不到"受限广播"。上位机的 `listen_announce()` 因此固定 bind `0.0.0.0`。
8. **Nagle 会把诊断小报文攒 40 ms**。TCP 的 Nagle 算法 + 延迟 ACK 会让"一请求一应答"的小报文多等几十毫秒。本实现在 accept 时 `tcp_nagle_disable()`，上位机侧设 `TCP_NODELAY`。
9. **0x78 pending 只有在 UDS 层才自然**。DoIP 本身没有"分片/流控"概念，一条 0x8001 就是一整条 UDS PDU；慢服务必须靠 UDS 的 `NRC 0x78` + P2* 续期（本实现 150 ms 慢例程实测 pending=1、最终响应 150.7 ms）。想靠"多发几条 0x8001"凑大数据是错的。
10. **把超过 512 B 的 UDS 直接交给 app_uds 会"有 ACK 没响应"**。`components/app_uds` 的 `app_uds_handle()` 第一行就判 `len > APP_UDS_MAX_REQ（512）` 并**直接 return -1**，不产生任何响应。第一版 DoIP 把 4000 B 请求原样转过去，主机侧看到"0x8002 ACK 来了、3 s 后连接被 general inactivity 断掉"，很容易误判成 TCP 或 ENET 的问题。正确做法是在 DoIP 层先判：超过实体能力就按 ISO 回 `0x8003/0x04`，并在 0x4002 里如实报 `max data size = 4 + 512 = 516`（本实现即如此）。要真跑 4 KB 的 UDS，必须先调大 `APP_UDS_MAX_REQ`（不在本任务写作用域，见 §31.14）。

## 31.14 待确认项与风险

| 项 | 状态 | 复跑方法 |
|---|---|---|
| 单条 UDS 报文 >512 B | 已知限制：DoIP 层能收 4096 B 帧，但 UDS 侧 `APP_UDS_MAX_REQ=512`，超出回 0x8003/0x04。要跑 4 KB 的 UDS 需改 `components/app_uds` 的宏（不在本任务写作用域） | `python3 host/doip/doip_client.py bulk --size 4000` |
| 一条请求内连发多帧（20 × 512 B） | 已定位：本板 SDK lwIP 零拷贝发送路径在短时间多帧突发下会卡住，主机侧表现为连接被 general inactivity 断掉；单帧 4008 B 稳定（85.4 Mbit/s） | `python3 host/doip/run_tests.py -k bulk` |
| 并发多连接 | 已实测（2 条并发，同 SA 抢占）/ 已知限制（3 条不同 SA 并发未测） | 起 3 个 client 分别用 0x0E00/0x0E01/0x0E02 |
| TLS（2019 版 DoIP over TLS，端口 3496） | 未实现（已知限制），本实现只做明文 13400 | — |
| 跨网段诊断转发（网关模式） | **已知限制（未实现：本板是 DoIP 节点不是网关，`node_type=0x00`）** | — |
| 功能寻址（TA=0xE400） | **已知限制（未实现：只接受 `cfg.entity_addr`）** | 发 0x8001 TA=0xE400 会回 0x8003/0x03 |
| EID 与 MAC 一致性 | 已实测（初始化读 netif MAC）/ 已知限制（MAC 为本机管理位 00:00:00:00:D0:0E，量产需真实 OUI） | `doip eid` |
| 板卡共享带来的计时噪声 | 已定位：同一份用例在有其它成员编译/测试时，发现 RTT 会从 ~130 µs 涨到 28 ms、4008 B 帧从 375 µs 涨到 1.5 s。写书稿只用"独占且 banner 正确"的那次（163907） | 重跑 §31.9 第 2 条命令 |
| 标准符合性认证 | **已知限制（未做：只按 ISO 13400-2 公开条款实现，没跑过一致性测试套件）** | — |

## 31.15 参考来源

1. ISO 13400-2:2012 / 2019《Road vehicles — Diagnostic communication over Internet Protocol (DoIP) — Part 2: Transport protocol and network layer services》：通用头、载荷类型、路由激活、定时器、NACK 码的原始定义。
2. ISO 14229-1:2013《Road vehicles — Unified diagnostic services (UDS)》：诊断服务本身，本实现复用第 30 章的 `components/app_uds`。
3. ISO 15765-2《Road vehicles — Diagnostic communication over Controller Area Network (DoCAN)》：对比用的 ISO-TP，见第 29 章。
4. AUTOSAR Classic Platform，Dcm / DoIP 相关 SWS（R25-11 的 DoIP 文档本轮未下到，按 ISO 13400-2 公开条款实现；Dcm 与本组件通过 `diag_rx` 回调解耦，结构上对应 AUTOSAR 的 DoIP→Dcm 接口）。
5. EcuBus-Pro（开源诊断工具）DoIP 实现：<https://github.com/ecubus/EcuBus-Pro>，本仓库镜像 `ref2/EcuBus-Pro/src/main/doip/index.ts`（载荷类型枚举、路由激活 7 B 构造、ACK 方向校验都参考了它）。
6. lwIP 2.1.2 官方文档：<https://www.nongnu.org/lwip/2_1_x/index.html>（raw TCP/UDP API、`tcp_write` 的 `ERR_MEM` 语义）。
7. 本仓库第 12 章（YT8011AN 1000BASE-T1 链路）、第 13 章（lwIP 与 iperf 以太网基线）、第 29 章（ISO-TP）、第 30 章（UDS 服务端）。
8. 源码：`components/app_doip/src/app_doip.c`（1175 行）、`app_doip_wire.c`（342 行）、`include/app_doip.h`（194 行）、`include/app_doip_wire.h`（165 行）、`samples/app/doip_server/src/main.c`（653 行）、`host/doip/doip_client.py`（828 行）、`host/doip/run_tests.py`（431 行）。