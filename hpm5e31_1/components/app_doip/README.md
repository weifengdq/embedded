# app_doip —— DoIP(ISO 13400-2) 服务端组件

> 独立组件，AUTOSAR 风格分层但不属于 MCAL：只做 DoIP 运输层，UDS 服务由调用者提供回调。
> 章节：第 31 章；例程：`samples/app/doip_server`；上位机：`host/doip`。

## 1. 范围

| 能力 | 状态 | 说明 |
|---|---|---|
| 通用头（版本/取反/载荷类型/长度） | ✔ | 版本 0x02（2012）与 0x03（2019），0xFF「任意版本」 |
| 车辆识别 0x0001/0x0002/0x0003 -> 0x0004 | ✔ | 支持 EID/VIN 过滤，全 FF/全 0 视为广播 |
| 路由激活 0x0005/0x0006 | ✔ | 7 B/11 B 两种请求；0x00~0x11 响应码；OEM 鉴权可配 |
| alive check 0x0007/0x0008 | ✔ | 同 SA 抢占用例自动触发；可 shell 手动触发 |
| 诊断报文 0x8001 + ACK/NACK 0x8002/0x8003 | ✔ | SA/TA 校验；UDS 超过 cfg.max_diag_uds（默认 512 B）回 0x8003/0x04 |
| 实体状态 0x4001/0x4002、电源模式 0x4003/0x4004 | ✔ | UDP |
| 通用头 NACK 0x0000 | ✔ | 5 种 NACK 码 |
| TCP 分帧 + 两个 inactivity 定时器 | ✔ | 8 B 头 + 长度字段；2 s / 300 s 可配 |
| UDP 车辆声明（上电 3 次 + 可配周期） | ✔ | A_DoIP_Announce_Num/Interval/Wait |
| TLS（2019，端口 3496）、网关转发、功能寻址 | ✘ | 已知限制，见第 31 章 §31.14 |

## 2. 文件

| 文件 | 行数 | 内容 |
|---|---|---|
| `include/app_doip.h` | 194 | 公共 API、连接状态、配置项、统计字段 |
| `include/app_doip_wire.h` | 165 | 协议常量（载荷类型/响应码/NACK 码）+ 纯函数编解码原型 |
| `src/app_doip_wire.c` | 342 | 编解码实现 + 49 项固定向量自检（`app_doip_wire_selftest`） |
| `src/app_doip.c` | 1175 | TCP/UDP 引擎、分帧、路由激活、诊断转发、定时器、声明 |
| `app_doip.cmake` | — | 接入 hpm-sdk 例程的 CMake 片段 |

## 3. 用法

```c
#include "app_lwip.h"          /* 必须先起 lwIP */
#include "app_doip.h"
#include "app_uds.h"

static app_doip_t g_doip;
static app_uds_t  g_uds;
static uint16_t   g_cur_ta;

/* UDS 响应 -> 0x8001 */
static int uds_tx(void *ctx, const uint8_t *pdu, uint32_t len)
{
    (void) ctx;
    return app_doip_send_diag(&g_doip, g_cur_ta, pdu, len);
}

/* 0x8001 -> UDS（这里只入队，主循环再处理，见第 31 章坑 3） */
static int on_diag(void *ctx, uint16_t sa, uint16_t ta, const uint8_t *uds, uint32_t len)
{
    g_cur_ta = sa;
    return app_uds_handle(&g_uds, uds, len);   /* 生产代码建议入队 */
}

int main(void)
{
    app_doip_cfg_t cfg = {0};
    board_init(); bsp_init();
    app_lwip_init(NULL);

    memcpy(cfg.vin, "HPM5E31TESTVIN001", 17);
    cfg.eid[5] = 0x01;
    cfg.gid[5] = 0x01;
    cfg.entity_addr = 0x1000;        /* 本实体逻辑地址 */
    cfg.tester_addr = 0x0E00;        /* 允许的 tester SA，0 = 不校验 */
    cfg.diag_rx = on_diag;
    cfg.version = APP_DOIP_VER_2012;
    cfg.verbose = true;

    app_doip_init(&g_doip, &cfg);
    app_doip_start(&g_doip);         /* TCP+UDP 13400 */

    for (;;) {
        app_lwip_poll();             /* 收包 + 发送环 */
        app_doip_poll(&g_doip);      /* 定时器 + 声明 + 踢 TCP 输出 */
        app_uds_poll(&g_uds, 0);     /* S3/P2 计时 */
        bsp_shell_poll();
    }
}
```

## 4. 关键 API

| API | 作用 |
|---|---|
| `app_doip_init(d, cfg)` | 填配置（含 max_payload / max_diag_uds）、清连接槽（不碰网络） |
| `app_doip_start(d)` / `app_doip_stop(d)` | 起停 UDP/TCP 13400 监听 |
| `app_doip_poll(d)` | 主循环调用：inactivity/alive 超时、车辆声明、TCP 输出踢一脚 |
| `app_doip_send_diag(d, ta, uds, len)` | 把 UDS 响应打成 0x8001 发回 tester |
| `app_doip_send_announce(d, n)` | 立即发 n 次车辆声明 |
| `app_doip_alive_check(d, idx)` | 对某连接发 0x0007（idx 为负自动挑第一条） |
| `app_doip_find_conn(d, ta)` | 找该 tester 的已激活连接 |
| `app_doip_print(d)` / `app_doip_print_conns(d)` | 状态与统计打印 |
| `app_doip_set_vin(d, vin)` / `app_doip_set_eid(d, eid)` | 运行期改 VIN/EID |
| `app_doip_wire_selftest(&checks, &fail)` | 编解码固定向量自检，不跑网络 |

## 5. 资源与约束

- 静态 RAM：`APP_DOIP_MAX_CONN(3) × APP_DOIP_MAX_PAYLOAD(4096)` = 12 KB 接收缓冲 + 4 KB 发送拼包；
  调用者的 UDS 队列另算（例程里 2 × 4096 = 8 KB）。组件自身 bss 实测 4.1 KB，text 11.1 KB（-O3）。
- `cfg.max_payload`（帧上限，4096）与 `cfg.max_diag_uds`（交上层的 UDS 上限，默认 512）是两个概念：
  后者必须 ≤ 上层 UDS 的请求缓冲（本仓库 `components/app_uds` 是 `APP_UDS_MAX_REQ`），
  否则上层会静默丢掉请求（第 31 章坑 10）。
- 无动态分配、无 OS 依赖；所有 API 只能在主循环（lwIP 回调同上下文）里调用。
- lwIP 回调里**不要阻塞**：ACK 会被发送环拖住（第 31 章坑 3，tcpdump 实测 150 ms）。
- 需要的 lwIP 选项：`LWIP_TCP=1`、`LWIP_UDP=1`、`MEMP_NUM_TCP_PCB>=3`、`TCP_SND_BUF>=4*MSS`。

## 6. 自检

```bash
# 板端：编解码 49 项检查（不跑网络）
doip selftest
# 主机侧：全部用例（含异常/超时/大报文）
python3 host/doip/run_tests.py
```
