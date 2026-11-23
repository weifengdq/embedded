# components/app_can —— CAN FD 节点组件

> 第二阶段应用层共享底座（tp-infra）。上层 ISO-TP/UDS、J1939、CANopen、XCP 都通过本组件收发 CAN 帧，
> 不再各自摸 MCAN 寄存器。对应书稿：第 28 章 §28.4（寄存器与驱动 API 对照）、第 08 章（CAN FD 原理）。

## 1. 它解决什么问题

`mcan_two_node` 里一个能跑的 CAN 节点要 60 行初始化：`mcan_get_default_config` → 低位时序
（prescaler/seg1/seg2/sjw/TDC）→ `board_init_can()` → `board_init_can_clock()` → `mcan_deinit()` →
`mcan_init()`，再加 DLC↔字节数换算、RXFIFO0/1 轮询、错误计数读取。本组件把这套收敛成：

```c
static app_can_t can0;
app_can_cfg_t cfg = APP_CAN_CFG_DEFAULT;          /* 经典 500k / SP87.5% */
app_can_init(&can0, BOARD_CAN0_BASE, &cfg);       /* 0 = 成功 */
```

> **硬件前提（必读）**：HPM5E31 的 MCAN 消息 RAM 位于共享 AHB SRAM（`0xF0200000`，32 KB），
> `hpm_soc_feature.h` 中 `MCAN_SOC_MSG_BUF_IN_AHB_RAM=1`。使用 `mcan_init()` 之前必须把消息缓冲区放进
> `.ahb_sram` 段并调用 `mcan_set_msg_buf_attr()`，否则 `mcan_init()` 返回 2（`status_invalid_argument`）、
> CAN 完全起不来。本组件已内置该处理（见 `app_can.c` 顶部 `s_msg_buf` / `can_inst_index()`），
> 上层**不要**自己再调 `mcan_init()`。

## 2. API（与 `docs/应用层开发约定.md` §4.1 完全一致）

| 函数 | 说明 | 返回 |
|---|---|---|
| `int app_can_init(app_can_t *c, MCAN_Type *base, const app_can_cfg_t *cfg)` | 初始化实例：引脚/时钟/收发器/位时序/过滤器 | 0 成功，-1 参数，-2 无时钟，-3 `mcan_init` 失败 |
| `int app_can_send(app_can_t *c, const app_can_frame_t *f, uint32_t timeout_us)` | 发一帧；`timeout_us` 是等 TX FIFO 空位的上限（0=只试一次） | 0 已入队，1 超时/失败 |
| `int app_can_recv(app_can_t *c, app_can_frame_t *f, uint32_t timeout_us)` | 轮询收一帧（FIFO0 优先，其次 FIFO1） | 0 收到，1 超时 |
| `void app_can_set_filter(app_can_t *c, const app_can_filter_t *flt, int n)` | 运行期重装过滤器（n=0 恢复全收） | — |
| `uint64_t app_can_now_us(void)` | 单调微秒时基（`mcycle / CPU MHz`），时延统计用 | 微秒 |
| `void app_can_get_stats(app_can_t *c, app_can_stats_t *st)` | 软件计数 + PSR/ECR 现场值 | — |

便利 API（非契约，可选）：`app_can_loopback_test()`（单节点内部环回自测）、
`app_can_dump()`（打印一行状态）、`app_can_speed_str()`、`app_can_cpu_mhz()`、`app_can_strerror()`。

### 帧结构

```c
typedef struct {
    uint32_t id;      /* 11 位标准 ID 或 29 位扩展 ID（右对齐） */
    uint8_t  ext;     /* 1 = 29 位扩展帧 */
    uint8_t  rtr;     /* 1 = 远程帧（仅经典帧有效） */
    uint8_t  fd;      /* 1 = CAN FD 帧 */
    uint8_t  brs;     /* 1 = 数据段切换到高速率（仅 FD） */
    uint8_t  len;     /* 0..8（经典）/ 0..64（FD） */
    uint8_t  data[64];
    uint64_t ts_us;   /* 接收时刻；发送时忽略 */
} app_can_frame_t;
```

### 预置位时序

| 档位 | 仲裁段 | 数据段 | 采样点 | TDC | 用途 |
|---|---|---|---|---|---|
| `APP_CAN_SPEED_CLASSIC_500K` | 500 kbit/s | — | 87.5%（80 MHz/1/160TQ，seg1=139 seg2=20 sjw=4） | — | UDS/J1939/CANopen/XCP 默认 |
| `APP_CAN_SPEED_FD_1M_2M` | 1 Mbit/s | 2 Mbit/s | 75%（arb presc=2 seg1=29 seg2=10；data presc=2 seg1=14 seg2=5） | 开，SSP=15，滤波窗 15 | CAN FD 大数据块 |

两档都来自 `samples/can/mcan_two_node` / `mcan_fd_timing` 的上板实测配置（第 8/09 章）。

## 3. 接入 CMake（两种方式等价）

```cmake
# 方式 A：一行 include（必须在 find_package(hpm-sdk) 之后）
include(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_can/app_can.cmake)

# 方式 B：手写两行
sdk_inc(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_can/inc)
sdk_app_src(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_can/src/app_can.c)
```

无额外编译宏；依赖板级包 `boards/hpm5e31_1` 的 `board_init_can()` / `board_init_can_clock()` /
`board_can_transceivers_enable()`（`-DBOARD=hpm5e31_1` 时自动可用）。

## 4. 用法示例

### 4.1 经典 500k 收发

```c
#include "app_can.h"

static app_can_t s_can0;

int main(void)
{
    app_can_cfg_t cfg = APP_CAN_CFG_DEFAULT;
    bsp_init();

    if (app_can_init(&s_can0, BOARD_CAN0_BASE, &cfg) != 0) {   /* classic 500k */
        printf("[can] init FAILED\r\n");
    }
    app_can_frame_t tx = { .id = 0x123, .len = 8 };
    for (int i = 0; i < 8; i++) tx.data[i] = (uint8_t) i;
    app_can_send(&s_can0, &tx, 1000);

    app_can_frame_t rx;
    if (app_can_recv(&s_can0, &rx, 10000) == 0) {               /* 等 10 ms */
        printf("rx id=0x%lX len=%u ts=%llu us\r\n",
               (unsigned long) rx.id, rx.len, (unsigned long long) rx.ts_us);
    }
    while (1) { bsp_shell_poll(); }
}
```

### 4.2 CAN FD 64 字节

```c
app_can_cfg_t cfg = APP_CAN_CFG_DEFAULT;
cfg.speed = APP_CAN_SPEED_FD_1M_2M;
app_can_init(&s_can0, BOARD_CAN0_BASE, &cfg);

app_can_frame_t f = { .id = 0x123, .fd = 1, .brs = 1, .len = 64 };
memset(f.data, 0xA5, sizeof(f.data));
app_can_send(&s_can0, &f, 1000);
```

### 4.3 只收某个 ID（过滤器）

```c
static const app_can_filter_t flt[] = {
    { .id = 0x7E0, .mask = 0x7FF, .ext = 0 },            /* 精确匹配 UDS 物理请求 */
    { .id = 0x18DA0000, .mask = 0x1FFF0000, .ext = 1 },  /* J1939 按 PGN 掩码 */
};
app_can_cfg_t cfg = APP_CAN_CFG_DEFAULT;
cfg.filters = flt;
cfg.filter_count = 2;
app_can_init(&s_can0, BOARD_CAN0_BASE, &cfg);   /* 未命中帧被硬件拒收，不进 FIFO */
```

运行期换过滤器：`app_can_set_filter(&s_can0, new_flt, n);`（内部会短暂进入 MCAN 初始化模式，
重配窗口内可能丢 1~2 帧，适合在总线上无关键业务时调用）。

### 4.4 时延统计 / 错误诊断

```c
uint64_t t0 = app_can_now_us();
app_can_send(&c, &f, 1000);
app_can_recv(&c, &rx, 5000);
uint32_t dt = (uint32_t)(app_can_now_us() - t0);   /* us */

app_can_stats_t st;
app_can_get_stats(&c, &st);
printf("tx=%lu rx=%lu bo=%u lec=%lu tec=%u rec=%u\r\n",
       st.tx_frames, st.rx_frames, st.bus_off, st.last_error_code, st.tec, st.rec);
```

## 5. 坑（都是本板实测踩过的）

| # | 现象 | 原因 | 处理 |
|---|---|---|---|
| 0 | `mcan_init()` 返回 2（`status_invalid_argument`），CAN 完全起不来，PSR/ECR 全 0 | HPM5E31 的 MCAN 消息 RAM 是 **AHB SRAM(0xF0200000)**，不是 MCAN IP 内部；`hpm_soc_feature.h` 里 `MCAN_SOC_MSG_BUF_IN_AHB_RAM=1` | 缓冲区加 `ATTR_PLACE_AT(".ahb_sram")`，并在 `mcan_init()` **之前**调用 `mcan_set_msg_buf_attr()`（组件已做，4 实例各 640 words=10 KB；见 `app_can.c` 顶部） |
| 1 | 一访问 MCAN 寄存器就挂死 | 外设时钟门控未开（`board_init_can_clock` 没调用） | 组件内部已按 `board_init_can`→`board_init_can_clock`→`mcan_deinit`→`mcan_init` 顺序，**不要**在外部重复初始化 |
| 2 | 总线上一帧都发不出去、PSR 显示 ACK 错 | TCAN1044AV 的 STB(PE03) 还是高电平（standby） | `app_can_init` 首次调用会拉低 STB；若中途手动改过 PE03 需再调 `board_can_transceivers_enable()` |
| 3 | 单节点发几帧后 bus-off | 没有其它节点发 ACK，TEC 累加到 256 就 bus-off | 双节点自测必须 CAN0↔CAN1 都初始化；单节点用 `cfg.loopback = true`（内部环回） |
| 4 | `app_can_send` 返回 0 但对端没收到 | 返回 0 只表示已进 TX FIFO，不保证已被 ACK | 需要端到端确认时用对端回显或读 `PSR.LEC/BO`；`tx_fail` 只统计入队失败 |
| 5 | FD 帧 DLC 与实际长度不符 | DLC 是 4 bit 编码（9..15 → 12/16/20/24/32/48/64），不是字节数 | 组件内部 `len_to_dlc()/dlc_to_len()` 已换算；直接调 SDK 时注意 `mcan_tx_frame_t.dlc` |
| 6 | `mcan_transmit_blocking()` 偶尔卡 100 us+ | 内部按 TSU/TX event 自旋重试 | 热路径用 `app_can_send(c, f, 0)`（非阻塞，FIFO 满立刻返回 1），由上层决定重试 |
| 7 | 4 路 CAN 同时跑时机器变慢 | `app_can_recv(timeout_us)` 是忙等 | 主循环里用 `timeout_us = 0` 轮询，或在中断里取帧再置标志（第 28 章 bench 架构） |

## 6. 验证

- `samples/app/can_gateway` 启动时调用 `app_can_loopback_test(BOARD_CAN3_BASE, ...)` 做内部环回自证，
  串口打印 `[loop] CAN3 loopback PASS rc=0`；
- CAN0 监视节点 + CAN1 网关的双节点真实总线链路见 `samples/app/can_gateway` 与第 28 章实测表；
- 组件本身在 `build/can_gateway-debug` 中每次上板都会编译并运行。

## 7. 文件

```
components/app_can/
├── inc/app_can.h       # 公开 API / 帧结构 / 统计
├── src/app_can.c       # 实现（位时序、DLC 换算、FIFO 收发、过滤器、统计）
├── app_can.cmake       # CMake 接入片段（sdk_inc + sdk_app_src）
└── README.md
```