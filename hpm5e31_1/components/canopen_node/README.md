# components/canopen_node —— CANopenNode v4.0 移植（CiA 301 从站）

hpm5e31_1 上的 CANopen 从站协议栈组件：把 [CANopenNode](https://github.com/CANopenNode/CANopenNode)
v4.0 的 CiA 301 协议栈移植到 HPM5E31 的 MCAN 上，提供 NMT、心跳、SDO server（加速 + 分段）、
PDO（RPDO/TPDO，可运行时重映射）、SYNC、EMCY、TIME 与对象字典。

## 1. 上游版本与许可证

| 项 | 值 |
|---|---|
| 上游仓库 | `https://github.com/CANopenNode/CANopenNode` |
| 版本 | **v4.0**（`CO_VERSION_MAJOR=4`、`CO_VERSION_MINOR=0`，commit `9b8beed`，ref2/CANopenNode） |
| 许可证 | **Apache-2.0**（见本目录 `LICENSE`，未修改上游文件头） |
| 引入的目录/文件 | `301/`（全部 .h + 用到的 .c）、`303/CO_LEDs.c/h`、`304/305/309/extra` 仅头文件、`CANopen.c/h` |
| 实际编译的 .c | NMT/心跳、EMCY、SDO server、PDO、SYNC、TIME、OD 接口、CANopen.c、CO_LEDs（见 `canopen.cmake`） |
| 未编译（配置关闭） | SDO client、LSS、SRDO/GFC、ASCII 网关、Trace、storage(0x1010/0x1011) |

> 升级上游时：替换 `301/ 303/ CANopen.c CANopen.h`，重新检查 `port/` 与 `od/` 的接口差异，
> 再跑 `samples/app/canopen_node` 自测即可。

## 2. 目录结构

```
components/canopen_node/
  CANopen.c/h          上游主对象（CO_new/CO_CANinit/CO_CANopenInit/CO_process）
  301/                 上游 CiA 301 实现（NMT/EMCY/SDO/PDO/SYNC/TIME/OD接口）
  303/CO_LEDs.c/h      状态灯（RUN/ERR）计算
  304/305/309/extra/   仅头文件（被 CANopen.h 无条件 include）
  port/
    CO_driver_target.h 目标相关类型/宏（本移植的核心配置文件）
    CO_driver.c        CO_driver 实现：MCAN 收发 + 错误状态（本文件只有 ~300 行是"新写的"）
  od/
    OD.h / OD.c        对象字典（由 tools/gen_od.py 生成，勿手改）
  tools/gen_od.py      对象字典 + EDS 的单一数据源生成器
  canopen_node.c/h     应用友好封装（co_node_init/poll/reset_comm/emcy/stats）
  canopen.cmake        例程共用的源文件清单
```

## 3. CO_driver 适配点（与其它 MCU 移植的差异）

本板的特点是"**单线程轮询**"：没有 RTOS、没有 CAN 中断参与协议处理。

| CANopenNode 期望 | 本移植做法 | 代码位置 |
|---|---|---|
| `CO_CANmodule_init()` 初始化 CAN 控制器 | `mcan_init()`：经典帧、仲裁段 500 kbit/s（16 TQ，采样点 87.5%）、RX FIFO0 | `port/CO_driver.c` `co_hpm_can_hw_init()` |
| 按 ID/MASK 的硬件过滤器 | **软件过滤**：一个 mask=0 的经典过滤器接收全部标准帧进 FIFO0，再按 `rxArray[]` 的 ident/mask 分发（PDO COB-ID 可运行时改写，硬件表放不下） | `CO_CANpoll_rx()` |
| 接收线程/中断 | 主循环调 `CO_CANpoll_rx()` 主动抽 FIFO，回调在调用者上下文执行（栈回调只做拷贝 + 置标志，安全） | `canopen_node.c` `co_node_poll()` |
| TX 中断重发未发出的帧 | `CO_CANsend()` 直接塞 MCAN TX FIFO；失败置 `bufferFull`+`CANtxCount`，`CO_CANpoll_tx()` 重试 | `port/CO_driver.c` |
| `CO_LOCK_*` 临界区 | 空宏（全在主循环顺序执行） | `port/CO_driver_target.h` |
| `CO_FLAG_SET/CLEAR` | GCC 内存屏障 `__sync_synchronize()` | 同上 |
| 错误状态 `CANerrorStatus` | 读 MCAN PSR 的 BO/EP/EW 映射到 `CO_CAN_ERRTX_*` | `CO_CANmodule_process()` |
| 消息 RAM | MCAN 消息缓冲必须放在 AHB SRAM：`ATTR_PLACE_AT(".ahb_sram")` + `mcan_set_msg_buf_attr()` | `port/CO_driver.c` 顶部 |

### 3.1 与 `components/app_can` 的关系

当前 `port/CO_driver.c` **直接调用** `hpm_mcan_drv`（`mcan_init/mcan_read_rxfifo/
mcan_transmit_via_txfifo_nonblocking`）+ 板级 `board_init_can()/board_init_can_clock()/
board_can_transceivers_enable()`，原因：`components/app_can`（tp-infra 提供，见
`docs/应用层开发约定.md` §4.1）在本轮开发时尚未发布。切换成本很低：

```c
/* 目前 */ co_hpm_can_hw_init()  -> mcan_init()
/* 接 app_can 后 */ app_can_init(&c, BOARD_CAN0_BASE, &cfg);
/* 目前 */ mcan_transmit_via_txfifo_nonblocking() -> can_tx()
/* 接 app_can 后 */ app_can_send(&c, &frame, 0);
/* 目前 */ CO_CANpoll_rx() 里 mcan_read_rxfifo()
/* 接 app_can 后 */ app_can_recv(&c, &frame, 0)
```

只需改 `port/CO_driver.c` 一个文件，CANopenNode 上层与对象字典不动。

## 4. 对象字典（0x1000-0x1FFF 标准 + 0x2000 厂商区）

对象字典由 `tools/gen_od.py` 生成（同时生成 `host/canopen/node1.eds`），改对象**只改生成器**：

```bash
python3 components/canopen_node/tools/gen_od.py          # 重新生成 OD.h/OD.c/EDS
python3 components/canopen_node/tools/gen_od.py --check  # CI 校验是否一致
```

### 4.1 通信区

| 索引 | 名称 | 类型 | 属性 | 说明 |
|---|---|---|---|---|
| 0x1000 | Device type | UNSIGNED32 | RO | `0x00000191`（DS-301 通信规范） |
| 0x1001 | Error register | UNSIGNED8 | RO/TPDO | EMCY 错误寄存器 |
| 0x1003 | Pre-defined error field | ARRAY(16×U32) | RW/RO | 错误历史 |
| 0x1005 | COB-ID SYNC | UNSIGNED32 | RW | 默认 0x080 |
| 0x1006 | Communication cycle period | UNSIGNED32 | RW | SYNC 周期（µs，0=仅消费） |
| 0x1007 | Synchronous window length | UNSIGNED32 | RW | 同步窗口（µs） |
| 0x1008 | Manufacturer device name | VISIBLE_STRING(32) | RO | 分段 SDO 上传示例对象 |
| 0x1009/0x100A | HW/SW version | VISIBLE_STRING | RO | — |
| 0x1012 | COB-ID TIME | UNSIGNED32 | RW | 默认 0x100 |
| 0x1014 | COB-ID EMCY | UNSIGNED32 | RW | 存 0x80，栈按 +nodeID 计算 |
| 0x1015 | Inhibit time EMCY | UNSIGNED16 | RW | 100 µs 单位 |
| 0x1016 | Consumer heartbeat time | ARRAY(8×U32) | RW | 心跳消费者（主机可写监测节点） |
| 0x1017 | Producer heartbeat time | UNSIGNED16 | RW | **默认 1000 ms** |
| 0x1018 | Identity | RECORD(5) | RO | Vendor 0x0000A531 / Product 0x0000E531 / Rev 0x00010000 |
| 0x1019 | SYNC counter overflow | UNSIGNED8 | RW | 0=不用计数器 |
| 0x1200 | SDO server parameter | RECORD(3) | RO | 0x600/0x580 + nodeID |
| 0x1400/0x1401 | RPDO1/2 comm | RECORD(4) | RW | 0x201 / 0x301，transmission type 0xFF |
| 0x1600/0x1601 | RPDO1/2 mapping | RECORD(9) | RW | 见下 |
| 0x1800/0x1801 | TPDO1/2 comm | RECORD(6) | RW | 0x181 同步型（type=1）/ 0x281 事件型 1000 ms |
| 0x1A00/0x1A01 | TPDO1/2 mapping | RECORD(9) | RW | 见下 |

### 4.2 厂商区（0x2000-0x2FFF）

| 索引 | 名称 | 类型 | 长度 | 用途 |
|---|---|---|---|---|
| 0x2000 | vendorArray1k | ARRAY(128×U64) | 1024 B | 数组对象对照（逐 subindex 访问） |
| 0x2001 | vendorDomain1k | DOMAIN | 1024 B | 分段 SDO upload/download 吞吐测试 |
| 0x2002 | vendorDomain4k | DOMAIN | 4096 B | 1 KB vs 4 KB 对比 |
| 0x2100 | ledState | UNSIGNED16 | 2 B | **RPDO1 映射**：bit0/1 控制 CAN0 红/白灯 |
| 0x2101 | adcSample | UNSIGNED16 | 2 B | **TPDO1 映射**：ADC16 IN01(PF26) 采样 |
| 0x2102 | pdoCounter | UNSIGNED32 | 4 B | **TPDO1/TPDO2 映射**：主循环计数 |
| 0x2103 | loopCounter | UNSIGNED32 | 4 B | **RPDO2 映射** |
| 0x2104 | syncCounter | UNSIGNED16 | 2 B | **TPDO2 映射**：SYNC 计数器 |
| 0x2105 | lastSdoIndex | UNSIGNED16 | 2 B | 调试用 |
| 0x2106 | emcyTrigger | UNSIGNED32 | 4 B | **测试钩子**：写非 0 触发一条 EMCY |
| 0x2107 | emcyCount | UNSIGNED32 | 4 B | EMCY 注入计数 |

### 4.3 默认 PDO 链接

| PDO | COB-ID | 传输类型 | 映射（索引:子索引/位长） |
|---|---|---|---|
| RPDO1 | 0x201 | 0xFF（事件） | 0x2100:0 / 16 |
| RPDO2 | 0x301 | 0xFF（事件） | 0x2103:0 / 32 |
| TPDO1 | 0x181 | 0x01（每 SYNC） | 0x2101:0 / 16 + 0x2102:0 / 32（6 B） |
| TPDO2 | 0x281 | 0xFE（事件，1000 ms） | 0x2102:0 / 32 + 0x2104:0 / 16（6 B） |

## 5. 资源占用（实测）

- 对象字典 RAM：0x2000(1 KB) + 0x2001(1 KB) + 0x2002(4 KB) + 其它 ≈ **6.2 KB**；
- CANopenNode 堆分配（`CO_new`）：**2876 B**（`samples/app/canopen_node` 串口实测）；
- Flash（.text/.rodata）增量：≈ **26 KB**（对比同例程不含协议栈，见第 32 章表 33-x）。

## 6. 使用示例

```c
#include "canopen_node.h"
#include "OD.h"

co_node_cfg_t cfg = {.can_base = BOARD_CAN0_BASE, .node_id = 1,
                     .bitrate_kbps = 500, .heartbeat_ms = 1000,
                     .nmt_start_operational = 1};
co_node_init(&cfg);                 /* CO_new + CAN + NMT/SDO/PDO/SYNC/EMCY/HB */
while (1) {
    switch (co_node_poll()) {       /* 收帧分发 + SYNC/RPDO/TPDO + 主线程处理 */
    case CO_NODE_RESET_COMM: co_node_reset_comm(); break;
    case CO_NODE_RESET_APP:  bsp_reboot();         break;
    default: break;
    }
    OD_RAM.x2102_pdoCounter++;      /* 应用可直接读写对象字典变量 */
}
```

## 7. 已知限制

1. 未实现非易失存储（0x1010/0x1011），参数掉电不保存；
2. 未实现 SDO client / LSS / SRDO / GFC / ASCII 网关（可通过 `CO_CONFIG_*` 打开并补源文件）；
3. CANopen 只用经典帧；CAN FD 的 PDO 长度（>8 B）需要改 `CO_CANtx_t.data[8]` 与驱动，当前不支持；
4. 轮询方式下 `co_node_poll()` 的调用间隔决定最小 SYNC 抖动（实测 < 100 µs，见第 32 章）。
