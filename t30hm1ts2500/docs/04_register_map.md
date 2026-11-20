# S2500 寄存器速查

完整位定义见 `common/s2500/s2500_regs.h`；原始文档见 `docs/datasheet/T30HM1TS2500-D.txt`。

> **用法**：本页是"查表"用的，不需要从头读。位定义以 `common/s2500/s2500_regs.h` 为准。
> 命令行读写：`s2500 reg <mms> <addr> [value]`（地址是**十进制**或 `0x` 前缀都可）。

## 本篇目录

| 章节 | 内容要点 |
| --- | --- |
| — | MMS 0 标准控制/状态 |
| — | MMS 1 MAC |
| — | MMS 2 PCS / MMS 3 PMA |
| — | MMS 4 PLCA / 诊断 / TC10 / TD |
| — | MMS 12 onsemi 厂商寄存器 |
| 1 | 怎么读这张表（MMS 与地址换算） |
| 2 | 常用"组合读法"（5 个典型场景） |
| 3 | 常见错误与排查 |

---

## MMS 0 — 标准控制/状态（32-bit）

| 地址 | 名称 | 关键位 |
| --- | --- | --- |
| 0x0000 | IDVER | MAJVER[7:4]=1, MINVER[3:0]=1 → 0x11 |
| 0x0001 | PHYID | OUI 60:C0:BF, Model 1, REV 1 |
| 0x0002 | STDCAP | CTC=1, FTC=1, AIDC=1, DPRAC=1, MINCPS=3(8B) |
| 0x0003 | RESET | bit0 软复位（CS 释放后生效） |
| 0x0004 | CONFIG0 | SYNC15, TXFCSVE14, CSARFE13, ZARFE12, TXCTHRESH11:10, TXCTE9, RXCTE8, FTSE7, FTSS6, PROTE5, CPS2:0 |
| 0x0008 | STATUS0 | CDPE12, TXFCSE11, TTSCA*10:8, PHYINT7, RESETC6, HDRE5, LOFE4, RXBOE3, TXBUE2, TXBOE1, TXPE0（写 1 清） |
| 0x0009 | STATUS1 | CMPD1, CAPD0 |
| 0x000B | BUFSTS | TXC15:8, RCA7:0 |
| 0x000C | IMSK0 | 各错误屏蔽位，默认 0x1FBF |
| 0x000D | IMSK1 | CMPDM1, CAPDM0 |
| 0x0010–15 | TTSCA/B/C H/L | 发送时间戳 |
| 0xFF00 | PHY CONTROL | RESET15, LOOP14, LCTL12, LRST9, CTEST7 |
| 0xFF01 | PHY STATUS | S10M11, LNOK5, RJAB4, LKST2, LJAB1 |

## MMS 1 — MAC（32-bit）

| 地址 | 名称 | 说明 |
| --- | --- | --- |
| 0x0000 | MCONTROL0 | IPGNF21, BKOD20, NFCSF19, MCSF18, BCSF17, ADRF16, FCSA8, TXEN1, RXEN0 |
| 0x0010–17 | MADDRFILT0..3 L/H | H[15:0]=MAC[47:32], H[31]=EN, L=MAC[31:0] |
| 0x0020–27 | MADDRMASK0..3 L/H | 1 = 参与比较 |
| 0x0030–0x0052 | 统计计数器 | TX/RX 字节、帧、广播、组播、长度分布、错误 |

## MMS 2 — PCS（16-bit）

| 0x08F3 | PCSCTL | PCSRST15, LOOP14 |
| 0x08F4 | PCSSTATUS | JAB7 (latching) |
| 0x08F5 | PCSDIAG1 | REMJAB 计数（读清） |
| 0x08F6 | PCSDIAG2 | 物理碰撞计数（读清） |

## MMS 3 — PMA（16-bit）

| 0x08F9 | PMACTL | PMARST15, TXDIS14, MULT10, LOOP0 |
| 0x08FA | PMASTATUS | LOOPA13, MULTA10, RJAB1 |
| 0x08FB | PMATEST | Test Mode[15:13] |

## MMS 4 — PLCA / 诊断 / TC10 / TD（16-bit）

| 0xCA00 | PLCAIDVER | MAPID=0x0A, MAPVER=0x12 |
| 0xCA01 | PLCACTL0 | EN15, RST14 |
| 0xCA02 | PLCACTL1 | NCNT15:8, ID7:0（ID=0 协调者，255 不参与） |
| 0xCA03 | PLCASTATUS | PST15 |
| 0xCA04 | PLCATOTMR | TOTMR7:0（默认 32 BT） |
| 0xCA05 | PLCABURST | MAXBC15:8, BTMR7:0 |
| 0xCA06 | PLCADIAG | RXINTO2, UNEXPB1, BCNBFTO0（写 1 清） |
| 0xCC00 | ADFCAP | HDD1W11, HDD[10:8]=4, SQI+[4:1]=5, SQI0=1 |
| 0xCC01 | HDD | CTRL15, READY14, START13, 1W_ST3, VALID2, SHORT_OPEN1:0 |
| 0xCC02 | DCQ_TOID | SQI 统计的 TO ID（0xFF=全部） |
| 0xCC03 | DCQ_SQI | UPD15, SQI2:0 |
| 0xCC04 | DCQ_SQI+ | UPD15, SQI+7:0 |
| 0xCD00–02 | HDD_Tune1..3 | 门限微调、单线检测 |
| 0xCE00 | TD_CTRL | EN15, REFN14, DLYM_START13, DM_DUR12:9, DM_START8, AUTO_START7 |
| 0xCE01 | TD_STAT | DLYM_DONE15, DLYM_ERR14, DM_DONE13, DM_ERR12, AUTO_ERR11 |
| 0xCE02–08 | TD 结果 | DIST_MR, DLY_MR, MNDLY_MR, MNDLY_DUR |
| 0xD000/0xD001 | WS_STATUS0 / WS_CTRL0 | LPCAP15, LP_FAIL14 / LPREQ15, LPEXIT14 |
| 0xD100/0xD101 | WS_STATUS1 / WS_CTRL1 | POR_VCC15, POR_VIO14, RST_IN12, TSD9, WUT8, WPIN7 / WFWD15, WDEB14, LWD13, RWD12, RWCD11, WOUT10, LWTH9, RWTH8, LPRQ1, GW0 |
| 0x8000 | ONIPVER | 0x30C0 |
| 0x8001 | TWEAKS1 | ENIE7, UNJT6, SCRD2, ECOLE1 |
| 0x8002 | PLCAEXT | PRECEDENCE15, CRPS4, CREN2, LDEN1, LDR0 |
| 0x8003/4 | PMATUNE0/1 | BDT, DCWS / PPDT, CDT |
| 0x8007 | WUPTUNE | TTTHR3:0 |

## MMS 12 — onsemi 厂商（32-bit）

| 0x0010 | PHYIRQM | CRDIM6, PCIM5, PRIM4, RJIM3, LJIM2, PSIM1, LSIM0 |
| 0x0011 | PHYIRQS | 对应状态（读清） |
| 0x0012 | DIOCFG0 | DIOx: SLW7, PEN6, PUD5, FN4:1, VAL0（DIO1 在 bit15:8） |
| 0x1000 | ONICVER | 0xA7A8 13 13 |
| 0x1001 | TWEAKS2 | TX_AMP15:14, EDTHCD13:10, EDTH9:6, TX_SLW5:4, TSD_DIS3, TX_CMC2:1, CLKO_ENA0 |
| 0x1002/3 | MACID0/1 | 出厂 MAC（低 32 / 高 16 位） |
| 0x1005 | NVM_HEALTH | RZW15, RZE14, YZW13, YZE12 |
| 0x1010/11 | PTPSEC/PTPNSEC | 当前时间 |
| 0x1012/13 | SETSEC/SETNSEC | 设置时间（SETNSEC[31]=SETT） |
| 0x1014 | PTPADJ | 时钟调整（attoseconds） |
| 0x1015 | CMPCTL | 比较控制 |
| 0x1016/17 | CMPSEC/CMPNSEC | 单次比较时间 |
| 0x1018 | CMPPER | 比较输出周期 (ns) |
| 0x1019–1B | CAPCTL/CAPSEC/CAPNSEC | 捕获 |
| 0x101C | BCNCNT | BEACON 计数 |

---

## 1. 怎么读这张表

S2500 用 **MMS（Memory Map Select）** 分区，控制头里的 bit27:24 就是 MMS：

| MMS | 分区 | 数据宽度 | 典型地址 |
| --- | --- | --- | --- |
| 0 | 标准控制/状态 | 32-bit | 0x0000（IDVER） |
| 1 | MAC | 32-bit | 0x0000（MCONTROL0） |
| 2 | PCS | 16-bit | 0x08F3（PCSCTL） |
| 3 | PMA | 16-bit | 0x08F9（PMACTL） |
| 4 | PLCA / 诊断 / TC10 / TD | 16-bit | 0xCA00（PLCAIDVER） |
| 12 | onsemi 厂商 | 32-bit | 0x1000（ONICVER） |

shell 的 `s2500 reg <mms> <addr>` 里，`mms` 就是上表第一列，`addr` 是该分区内的地址
（十进制或 0x 十六进制）。例：读 ONICVER = `s2500 reg 12 0x1000`。

## 2. 常用"组合读法"

### 2.1 确认 SPI 通路与芯片型号

```bash
s2500 reg 0 0        # IDVER  = 0x00000011
s2500 reg 0 1        # PHYID  = 0x180FF411
s2500 reg 12 0x1000  # ONICVER= 0xA7A813D2
s2500 reg 12 0x1002  # MACID0 = 出厂 MAC 低 32 位
s2500 reg 12 0x1003  # MACID1 = 出厂 MAC 高 16 位
```

### 2.2 确认 PHY 是否真的打开

```bash
s2500 reg 0 0xFF00   # PHY CONTROL：LCTL(bit12) 必须为 1
s2500 reg 0 0xFF01   # PHY STATUS ：S10M(bit11)=1 表示 10M，LKST(bit2)
```

> 这是本项目"两端 RX=0"的根因：**LCTL 默认 0，PHY 收发是关闭的**。

### 2.3 确认 MAC 收发开关

```bash
s2500 reg 1 0        # MCONTROL0：bit1 TXEN=1、bit0 RXEN=1、bit18 MCSF=0、bit17 BCSF=0
```

### 2.4 看 PLCA 是否真的在轮询

```bash
s2500 reg 4 0xCA01   # PLCACTL0 ：bit15 EN
s2500 reg 4 0xCA02   # PLCACTL1 ：NCNT[15:8] / ID[7:0]
s2500 reg 4 0xCA03   # PLCASTATUS：bit15 PST
s2500 reg 4 0xCA06   # PLCADIAG：bit2 RXINTO / bit1 UNEXPB / bit0 BCNBFTO（写 1 清）
```

### 2.5 看缓冲与流量

```bash
s2500 reg 0 0x000B   # BUFSTS：TXC[15:8] 剩余发送 credit，RCA[7:0] 可读块数
```

## 3. 常见错误与排查

| 现象 | 原因 | 处置 |
| --- | --- | --- |
| `s2500 reg 12 4096` 和 `s2500 reg 12 0x1000` 结果一样吗？ | 一样（4096 = 0x1000） | 都支持 |
| 写 `RESET`（MMS0 0x0003）后立刻读配置 | 软复位在 CS 释放后生效，需要时间 | 写后延时 ≥ 100 µs，再等 RESETC=1 |
| 写 `STATUS0` 想"置位"某位 | STATUS0 是**写 1 清** | 只能清，不能置 |
| 改 `CONFIG0.CPS` 后收发异常 | chunk 大小与驱动的缓冲管理必须匹配 | 本仓库固定 CPS=6（64B），不要单独改 |
| `PLCACTL1.ID=255` 时开 PLCA 没效果 | 255 表示"不参与 PLCA"（CSMA 模式） | 要先写合法 ID（0..NCNT-1） |
| 读厂商寄存器读到 0 | MMS 写成了 1 或 0 | 厂商区必须 `mms=12` |
| 16-bit 分区读到高 16 位是垃圾 | MMS2/3/4 是 16-bit 寄存器 | 只看低 16 位 |
| `s2500 reg` 参数错位 | letter-shell 的 CMD_FUNC 语义 | 本仓库已改用 `SHELL_TYPE_CMD_MAIN` |

## 4. 相关章节

| 想看什么 | 去哪里 |
| --- | --- |
| 协议事务格式 | [`docs/03_spi_protocol.md`](03_spi_protocol.md) |
| 位定义源码 | `common/s2500/s2500_regs.h` |
| 数据手册原文 | `docs/datasheet/T30HM1TS2500-D.txt` |
| 命令手册 | [`docs/08_shell_commands.md`](08_shell_commands.md) |
