# components/app_xrce —— Micro-XRCE-DDS 客户端组件（HPM5E31 + lwIP）

把 **Micro-XRCE-DDS Client v3.0.2** 移植到 hpm5e31_1（裸机 lwIP 2.1.2 + YT8011AN 千兆），
让板端可以直接**发布/订阅 DDS 话题**，从而与 ROS 2（经 Micro-XRCE-DDS Agent）互通。

- 上游源码：`ref2/Micro-XRCE-DDS-Client` v3.0.2（commit `6884d7bdd5682f34e0084f48dfe41a2192495465`，Apache-2.0）
  + Micro-CDR v2.0.2（commit 见上游 v2.0.2 tag，Apache-2.0）
- 授权：两份上游 LICENSE 已随源码放在 `third_party/microxrcedds_client/LICENSE`、`third_party/microcdr/LICENSE`
- 本组件只做"接入"，不改上游源码（唯一例外：`include/uxr/client/config.h` 与 `include/ucdr/config.h`
  是上游 CMake 模板的**手工生成结果**，因为本仓库用 SDK 的 `sdk_app_src` 而不是上游 CMake）

## 1. 目录

```
components/app_xrce/
├── inc/app_xrce.h                组件 API（会话/实体/发布/订阅/统计）
├── src/app_xrce.c                会话与实体管理、统计、断线重连（无堆）
├── src/app_xrce_transport.c      lwIP UDP custom transport（4 槽环形接收）
├── src/app_xrce_platform.c       uxr_millis()/uxr_nanos() 时基（mcycle@480 MHz）
├── app_xrce.cmake                构建片段：app_xrce_configure()
├── third_party/microxrcedds_client/{include,src,LICENSE}   Client 3.0.2
│      · src/c 只保留 core + custom transport 的 .c（其余 Posix/FreeRTOS 传输不编译）；
│      · 但保留上游全部 *_internal.h（core 会无条件包含 shared_memory_internal.h 等，删了编译不过）
└── third_party/microcdr/{include,src,LICENSE}              Micro-CDR 2.0.2
```

## 2. 在例程的 CMakeLists.txt 里接入

```cmake
set(APP_XRCE_COMPONENT ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_xrce)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)   # 必须先
include(${APP_XRCE_COMPONENT}/app_xrce.cmake)
app_xrce_configure()      # 加 include 路径 + 编译 30 个源文件（Client 核心 + microcdr + 适配层）
```

## 3. API（inc/app_xrce.h）

```c
int  app_xrce_init(const app_xrce_cfg_t *cfg);      /* 建会话 + Participant/Publisher/Subscriber */
int  app_xrce_add_pub(const app_xrce_pub_cfg_t *cfg); /* 返回发布索引 */
int  app_xrce_add_sub(const app_xrce_sub_cfg_t *cfg);
int  app_xrce_publish(int pub_idx, const void *cdr, uint32_t len);
void app_xrce_poll(void);                           /* 主循环：驱动 lwIP + 跑 1 ms 会话 */
void app_xrce_set_net_poll(void (*fn)(void));       /* 注入 enet_common_handler(&gnetif) */
void app_xrce_get_stats(app_xrce_stats_t *out);
bool app_xrce_connected(void);
uint32_t app_xrce_now_ms(void);                     /* 精确单调毫秒（调度用） */
```

要点：

1. **无堆**：会话/实体状态都是静态的；收发缓冲由调用者通过 `app_xrce_cfg_t` 传入。
2. **字节级发布**：`app_xrce_publish()` 的载荷是**纯 CDR 体**（不含 4 字节封装头，Agent 会自己加/去）。
3. **CDR 自己写**：组件不依赖 IDL 生成代码，因此可以对接任意 ROS 2 标准消息（见例程里的 `cdr_encode_*`）。
4. **二进制实体创建**：topic 名/类型名就是 DDS 名（ROS 2 侧为 `rt/<topic>` + `pkg::msg::dds_::Type_`）。

## 4. 平台适配（移植时改这三处）

| 位置 | 需要实现 | 本工程做法 |
|---|---|---|
| `app_xrce_platform.c` | `uxr_millis()` / `uxr_nanos()` | 读 RISC-V `mcycle` CSR，除以 `clock_get_frequency(clock_cpu0)` |
| `app_xrce_transport.c` | `open/close/write/read` 四个回调 | lwIP `udp_new/udp_bind/udp_connect/udp_send/udp_recv` |
| `third_party/.../uxr/client/config.h` | MTU、流数量、连接重试 | MTU 512、每类流各 1 条、5×1000 ms |

`read()` 的语义是"最多等 timeout ms"，但**等待期间必须继续驱动 lwIP**，否则永远收不到包；
所以通过 `app_xrce_set_net_poll()` 把 `enet_common_handler(&gnetif)` 注入传输层，在等待循环里反复调用。

## 5. 资源占用（实测，2026-10-30）

| 项 | 数值 |
|---|---|
| Flash（组件净增） | 约 **3.5 KB**（`enet_lwip_iperf` 124296 B → `xrce_pubsub` 128792 B，含两个例程的差异） |
| 静态 RAM（组件自用） | 可靠流出 4 KB + 入 4 KB + BE 出 1 KB + 接收环 2 KB ≈ **11 KB** |
| 堆分配 | **0 次**（Client/microcdr/适配层无 `malloc`） |

## 6. 坑（移植时必需知道的）

1. `src/c` 下还有 Posix/FreeRTOS/Windows 的传输实现，**不能 `file(GLOB)`**，必须显式列出文件
   （见 `app_xrce.cmake`）；`.c` 同目录的 `*_internal.h` 也要一起拷贝，否则 `xrce_header_internal.h` 找不到。
2. 上游 `src/c/util/time.c` 用 `clock_gettime`，裸机不可用 → **不编译它**，改由 `app_xrce_platform.c` 提供同名函数。
3. 订阅（`READ_DATA`）在 Agent 侧是**一次性**的，收到数据后必须在回调里重新
   `uxr_buffer_request_data()`，否则只能收到第一条。
4. DataWriter 的 QoS 要按对端需求设置：ROS 2 默认订阅者是 `RELIABLE`，板端若写 `BEST_EFFORT` 会**不匹配**。
5. 不要用 `bsp_uptime_ms()` 做周期调度（该实现每次调用截断，实测会慢一半），用 `app_xrce_now_ms()`。
