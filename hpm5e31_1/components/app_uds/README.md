# components/app_uds —— UDS(ISO 14229-1:2013) 服务端

## 1. 文件

| 文件 | 说明 |
|---|---|
| `include/app_uds.h` | 服务端 API、会话/安全枚举、NRC、DID/例程/配置结构 |
| `src/app_uds.c` | SID 分发、0x10/11/14/19/22/27/28/2E/2F/31/34/36/37/3E/85、P2/P2*/S3 |
| `include/app_uds_dtc.h` / `src/app_uds_dtc.c` | DTC 内存（状态位、快照、扩展记录、老化） |
| `include/app_uds_flash.h` / `src/app_uds_flash.c` | 刷写后端函数表 + 片内 XPI NOR 实现 |

## 2. 设计要点

1. **后端全部可替换**：DID 是数组、DTC 是 RAM 表、例程是函数表、刷写是函数指针表。
   `app_uds.c` 不依赖任何 MCU 头文件，可以拿到主机上做单元测试。
2. **长操作先发 0x78**：`app_uds_send_pending()` 在擦除等长操作前主动发 NRC 0x78，主机 P2* 续期。
3. **刷写安全**：下载窗口固定为 1 MB Flash 的最后 64 KB（0x800F0000~0x80100000），
   绝不碰正在 XIP 执行的应用镜像；擦写期间关全局中断；写完 invalidate D-Cache。

## 3. 接入步骤

`@c
static app_uds_t s_uds;
static app_uds_did_t s_dids[8];
static uint32_t s_did_count;
static app_uds_dtc_mem_t s_dtc;

app_uds_default_dids(s_dids, &s_did_count);   /* 预置 F190/F195/F187/F18C/F191/F010 */
app_uds_dtc_init(&s_dtc);                      /* 再 app_uds_dtc_set(...) 装故障 */
app_uds_cfg_t cfg = {
    .tx = my_isotp_send,                       /* 把响应 PDU 交给 ISO-TP */
    .reset = my_reset,  .dids = s_dids, .did_count = s_did_count,
    .dtc = &s_dtc, .routines = s_routines, .routine_count = n,
    .flash = app_uds_flash_default(),          /* 支持 0x34/0x36/0x37 */
};
app_uds_init(&s_uds, &cfg);

/* ISO-TP 的 on_msg 回调里 */
app_uds_handle(&s_uds, data, len);
/* 主循环里 */
app_uds_poll(&s_uds, app_isotp_port_now_us());
`@

注意：**DTC 内存必须在 `app_uds_init()` 之前装填**（init 不清表，第一版清表导致 19 02 永远读到 0 条）。

## 4. 实测

见第 30 章与 `samples/app/uds_ecu/README.md`、`samples/app/uds_flash/README.md`：
13/13 服务序列 PASS；32 KB 镜像 34/36/37 刷写 10/10 PASS，总线侧 24.2 KB/s、Flash 裸写 269 KB/s。
