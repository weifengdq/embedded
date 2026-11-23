# ptpd_v2 —— PTPd 2.0.1 移植版（PTP 协议栈本体）

本目录是 **HPM SDK v1.13.0 `middleware/ptpd/v2`** 的拷贝 + **1 处上游缺陷修补**，
供 `components/app_ptp` 与 `samples/app/ptp_{slave,master}`、`gptp_demo` 使用。

## 1. 来源与许可

| 项 | 值 |
|---|---|
| 上游 | PTPd 2.0.1（`````` 见 SDK 头注释，van Kempen Alexandre，IEEE 1588-2008 完整普通时钟实现） |
| 取用位置 | `$HPM_SDK_BASE/middleware/ptpd/v2`（SDK v1.13.0） |
| 许可 | BSD-3-Clause（与原文件头一致，未改动版权声明） |
| 规模 | 10 个 .c + 6 个 .h，约 3.4 k 行（上游的 Makefile / man page 未带入） |
| 为什么拷贝而不是直接用 SDK | 需要修 `msgPackHeader()` 的 transportSpecific（见下），而 SDK 目录禁止修改 |

## 2. 与上游的唯一差异（patch）

`src/dep/msg.c` → `msgPackHeader()`：

```c
/* 上游（PTPd 2.0.1 / SDK v1.13.0） */
Nibble transport = 0x80; /* (spec annex D) */

/* 本移植版 */
#ifndef APP_PTP_TRANSPORT_SPECIFIC
#define APP_PTP_TRANSPORT_SPECIFIC 0x00
#endif
Nibble transport = APP_PTP_TRANSPORT_SPECIFIC;
```

**为什么必须改**：`0x80` 使 PTP 报文头 byte0 的高 4 位（transportSpecific / PTPv2.1 的 majorSdoId）
等于 8。IEEE 1588-2008 Annex D（PTP over UDP/IPv4）规定该字段为 0；linuxptp 的 ptp4l 默认按
`transportSpecific` 过滤报文，于是 **ptp4l 会静默丢弃板端发出的 Delay_Req**：

- 板端现象：`updateClock: one-way delay averaged (E2E): 0s 0ns` 恒为 0，从钟永远测不到路径延迟；
- 主机现象：`ptp4l -m` 一切正常（主钟不需要 Delay_Req），日志无任何报错，极难定位；
- 抓包判据：板端 Delay_Req 的 byte0 = `0x81`（应为 `0x01`）；`ptp4l -l 7` 的配置项
  `transportSpecific is 0` 会出现在启动日志里。

临时规避（不改板端）：主机 `/etc/linuxptp/ptp4l.conf` 里加 `ignore_transport_specific 1`。

修复后实测：单向路径延迟 13830~13840 ns（本板 ↔ 主机 enp6s0，中间经 1000BASE-T1↔RJ45 转换盒），
从钟 offset 收敛到 ±0.5 µs 以内、典型值 ±60 ns。

若要与**未打补丁**的 PTPd 对端互通，编译时加 `-DAPP_PTP_TRANSPORT_SPECIFIC=0x80` 即可。

## 3. 构建方式

不在 SDK 的 `add_subdirectory_ifdef(CONFIG_PTPD_V2 v2)` 体系里编译，而由各例程的
`CMakeLists.txt` 显式列出源文件：

```cmake
set(CONFIG_PTPD_V2 0)
set(PTPD_DIR ${PTP_COMPONENT}/ptpd_v2/src)
sdk_inc(${PTPD_DIR})
sdk_inc(${PTPD_DIR}/dep)
sdk_app_src(${PTPD_DIR}/arith.c)  # bmc.c protocol.c ptpd.c
sdk_app_src(${PTPD_DIR}/dep/msg.c) # net.c servo.c startup.c time.c timer.c
```

其中 `dep/time.c` 与 `dep/net.c` 是 HPM 特有的适配层，用到的硬件接口：

| 函数 | 实现 | 对应硬件 |
|---|---|---|
| `getTime()` | `enet_get_ptp_timestamp(ENET, &ts)` | ENET `SYST_SEC`(0x708) / `SYST_NSEC`(0x70C) |
| `setTime()` | `enet_set_ptp_timestamp()` | `SYST_SEC_UPD`/`SYST_NSEC_UPD` + `TS_CTRL.TSINIT` |
| `updateTime()` | `enet_update_ptp_timeoffset()` | `SYST_*_UPD` + `TS_CTRL.TSUPDT`（带符号粗调） |
| `adjFreq()` | `enet_adjust_ptp_time_freq()` | `TS_ADDEND`(0x718) + `TS_CTRL.TSADDREG` |
| `netRecv*/netSend*` | lwIP `udp_recv/udp_sendto` + `p->time_sec/time_nsec` | 描述符 RDES6/7、TDES6/7（`LWIP_PTP=1`） |

## 4. 维护注意

- 上游缺陷修复只此一处；升级 SDK 时直接对 `middleware/ptpd/v2` 做 diff 再同步；
- `constants_dep.h` 里 `PTPD_DBG` 打开会让每秒打印 3 行 `updateClock`（观测/取证靠它，量产可关）；
- 默认 E2E + two-step + domain 0 + Announce 2 s + Sync 1 s，与 linuxptp 默认一致。
