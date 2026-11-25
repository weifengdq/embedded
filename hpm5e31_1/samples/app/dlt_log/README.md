# dlt_log —— COVESA DLT 日志报文发送端（UDP 3490）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

板端按 DLT 格式（Standard Header 4 B + Extended Header 14 B + verbose 载荷）以 **100 条/s**
向主机 `192.168.2.1:3490` 发日志；4 个 context（MAIN/CAN/ADC/NET）× 6 个级别轮转。

## 接线

只用**以太网**：板 192.168.2.100/24 ↔ 主机 enp6s0 192.168.2.1/24（无需 CAN）。

## 命令

```bash
# 板端 + 主机一条命令拿全数据（--post 持锁执行主机接收）
bash scripts/hwrun.sh app/dlt_log --secs 10 --post "timeout 40 bash host/dlt/run_dlt.sh" --lock-wait 1800
```

串口 shell：`dlt on|off|rate <ms>|stat`。上电自动 `dlt on`（10 ms）。

## 报文格式（与 `ref2/dlt-daemon/include/dlt/dlt_protocol.h` 对齐）

| 段 | 长度 | 字段 |
|---|---|---|
| Standard Header | 4 B | HTYP(1)=0x11（UEH+WTMS）、MCNT(1)、LEN(2 大端，含头总长) |
|（.dlt 文件）存储头 | 16 B | pattern `DLT\x01` + sec(u32 LE) + usec(i32 LE) + ECU ID(4 字符定长)，主机侧写入 |
| Standard-extra | 4 B | TMSP（WTMS=1，0.1 ms，小端；**在扩展头之前**） |
| Extended Header | 10 B | MSIN(1)=0x01、NOAR(1)=2、APID(4)、CTID(4) |
| verbose 参数 1 | 8 B | TypeInfo(4)=UINT\|TYLE_32BIT(0x43) + uint32 level |
| verbose 参数 2 | 6+len B | TypeInfo(4)=STRG(0x200) + u16 长度 + ASCII 文本 |

级别编码：0=fatal、1=error、2=warn、3=info、4=debug、5=verbose（放在第一个 uint32 参数里）。

## 实测（logs/20261030-180754-dlt_log.log + logs/20261030-181000-host-dlt.log）

```
包数           : 599（解析错误 0）
实测速率       : 99.8 包/s（标称 100）
MCNT 丢包      : 2 / 601（0.33 %）
APID 直方图    : NET:150, MAIN:150, CAN:150, ADC:149
level 直方图   : fatal:98, error:100, warn:100, info:100, debug:101, verbose:100
样例报文       : APID=ADC CTID=ADC msin=0x01 noar=2 tmsp=13491.0 ms args=[4, 'seq=1342 ctx=ADC  level=4']
=== RESULT: PASS ===
```

## 官方工具链验证

`host/dlt/dlt_recv.py` 落盘的 .dlt 可直接被 **dlt-viewer 2.30.0** 打开：xvfb 无头启动后截图
`logs/20261102-104100-dlt-viewer-host-validation.png` 显示 599 条消息（Ecuid HPM5、APID/CTID
MAIN/CAN/ADC/NET、Type log、Payload "2 seq=1628 ctx=MAIN level=2"），FileErr 0。

## 易错点

1. **UDP 无重传**：丢包只能靠 `MCNT` 检测（0.33% 属正常量级）；要零丢包需 TCP 或 dlt-daemon 的缓冲机制；
2. **TMSP 位置**：WTMS 的 4 B 时间戳属于标准头扩展、在扩展头**之前**；放进扩展头会让官方解析器报 Plausibility(-18)；
3. **type info 的 TYLE 是"标准长度编码"**（1=8bit、2=16bit、3=32bit、4=64bit），不是字节数——
   接收端按 3 当 3 字节解析会整体错位（本章调试遇到过，见 `host/dlt/dlt_recv.py` 的 `TYLE_BYTES`）；
4. **HTYP 必须同时置 UEH(0x01) 和 WTMS(0x10)**：漏 WTMS 时扩展头只有 10 B，接收端会把 TMSP 当参数解析；
5. **APID/CTID 是 4 字节定长非 C 字符串**：不要用 `strlen` 处理。
