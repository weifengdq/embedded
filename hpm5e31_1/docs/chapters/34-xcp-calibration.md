# 第 34 章 XCP 标定与测量（CAN / 以太网）与 A2L 工具链

## 34.1 文档信息

| 项 | 内容 |
|---|---|
| 章节 | 第 34 章（第二阶段：应用层协议） |
| 版本 | v1.1（2026-11-02，新增 XCP on UDP / XCPonEth 大节） |
| 适用芯片/板卡 | HPM5E31IPB1（hpm5e31_1 最小系统板，CPU 480 MHz，XIP Flash） |
| 对应例程 | `samples/app/xcp_can_daq/`、`samples/app/xcp_can_cal/`、`samples/app/xcp_eth/`（XCPonEth） |
| 板端组件 | `components/app_xcp/`（协议层 + CAN 传输层 + `xcp_port_udp.c` UDP 传输层） |
| 上位机 | `host/xcp/`：CAN 路径（pyxcp / 自写 python-can）+ XCPonEth 路径（自写 socket 客户端 `xcp_eth_client.py`） |
| A2L 工具链 | `tools/gen_a2l.py`（CAN IF_DATA）、`samples/app/xcp_eth/gen_a2l_eth.py`（`XCP_ON_UDP_IP`）、`tools/a2l/*.json`、DanielT/a2ltool |
| 实测日期 | 2026-10-30（CAN）、2026-11-02（XCPonEth） |
| 可信度 | **已上板实测**：CAN 路径（DAQ/标定）与 XCPonEth 路径（1 kHz/4 ODT、10 kHz/8 ODT、30 s 长测、标定写读回）均有日志；CAN FD DTO、COPY_CAL_PAGE、PGM 仍未实现，列为已知限制（附验证方法） |

## 34.2 本章导览

读完本章你能：

1. 说清 XCP 的 **CTO/DTO** 两类报文、命令/响应/错误/事件四种 PID 的区别，以及为什么 XCP on CAN 的 CRO/DTO 天然是"一帧一个报文"；
2. 自己实现一个**能对接 pyxcp 的最小 XCP 从站**：CONNECT/GET_STATUS/GET_ID/SET_MTA/UPLOAD/DOWNLOAD/SHORT_UPLOAD/MODIFY_BITS/GET_SEED/UNLOCK + 全套 DAQ 动态列表命令；
3. 讲明白 **DAQ 列表 → ODT → CAN 帧**的打包规则，尤其是"时间戳吃掉 ODT0 的 4 字节"这笔账；
4. 用 **A2L** 把 ELF 里的变量地址交给上位机，并用 a2ltool 校验 A2L 的语法与一致性；
5. 复现本章的**实测数字**：CAN 1 kHz 事件率 999.0 Hz、3097 frame/s、丢帧 0、每事件打包 3.93 µs；
6. 把同一个从站扩展到 **XCPonEth（UDP）**：4 B 传输层头、单包多 ODT、CTR 丢包统计，10 kHz/8 ODT 实测 77791 msg/s、6.85 Mbit/s、帧错误 0；
7. 避开 13 个真实踩过的坑（AHB SRAM 消息缓冲区、CAN 帧填充、LEN 语义/4 B 对齐、UDP 无重传、MTA 推进导致不能盲重发等）。

前置章节：第 8/9 章（MCAN/CAN FD）、第 12/13 章（RGMII+lwIP，XCPonEth 的物理/协议底座）、第 28 章（应用层 bench 与 cannelloni 网关）。

## 34.3 概述与典型应用

**XCP（Universal Measurement and Calibration Protocol，ASAM MCD-1 XCP）** 是汽车电控开发里"标定 + 测量"的事实标准：ECU 跑一个**从站（slave）**，标定工具（CANape、INCA、pyxcp）当**主机（master）**，通过 CAN / CAN FD / Ethernet(UDP/TCP) / USB / SxI 等传输层访问 ECU **内存的绝对地址**——读变量就是 UPLOAD，写标定量就是 DOWNLOAD，周期采样就是 DAQ。

典型应用：

- 台架/整车标定：在线改 PID 参数、喷油 MAP、扭矩限制，**写后立即生效**，不用重刷固件；
- 道路试验数据采集：DAQ 以 1 kHz~10 kHz 采集内部变量，带**硬件时间戳**，事后和 CAN 日志、视频对齐；
- HIL/自动化测试：CI 里用 pyxcp 脚本扫参、跑标定矩阵、做回归。

XCP 与"另一套东西"的边界：

| 需求 | 该用什么 | 说明 |
|---|---|---|
| 在线改参数、看内部变量 | **XCP** | 需要 A2L 描述地址/换算/量纲 |
| 诊断/刷写（读 DTC、下载固件） | UDS（第 29/30 章） | 面向"服务"，规范固定（$22/$2E/$34…） |
| 小数据量周期信号 | CCP（CAN Calibration Protocol） | XCP 的前身，只支持 CAN，8 字节 CRO/DTO |
| 跨域/以太网大数据量标定 | XCP on Ethernet | 一包可 1400+ 字节，无需 ODT 拆分 |

## 34.4 工作原理

### 34.4.1 协议架构：CTO 与 DTO

XCP 把报文分成两类（图 34-1）：

- **CTO（Command Transfer Object，命令对象）**：主机 → 从站，一帧一个命令；
- **DTO（Data Transfer Object，数据对象）**：从站 → 主机，用首字节 **PID（Packet Identifier）** 区分类型：
  - `0xFF` = RES（肯定响应）、`0xFE` = ERR（错误）、`0xFD` = EV（事件）、`0xFC` = SERV（服务请求）；
  - `0x00..0xFB` = **DAQ 数据**，数值就是"绝对 ODT 号"。

![图 34-1 XCP 从站分层与 CTO/DTO 数据流](figures/34/fig-34-1-arch.png)

CAN 传输层有个容易忽略的细节：**经典 CAN 帧固定 8 字节**，所以 XCP on CAN 的 CRO/DTO 都是 8 字节，且：

- CRO：`data[0]` = 命令码，`data[1..7]` = 参数（最多 7 字节）；
- DTO：`data[0]` = PID，`data[1..7]` = 数据（最多 7 字节）。

因此 **CONNECT 的响应刚好 7 字节**（RESOURCE、COMM_MODE_BASIC、MAX_CTO、MAX_DTO(2)，协议层版本、传输层版本），加 PID 正好 8 字节；而 `GET_ID` 只能返回"长度头"，主机再用 UPLOAD 把字符串取回——这就是为什么 XCP 从站的 `GET_ID` 必须**顺手把 MTA 指向标识串**。

### 34.4.2 命令表与实现状态

表 34-1 是本板从站实现的命令（`components/app_xcp/xcp_core.c`）。"必备"列指 XCP 定义的最小命令集。

| 命令 | 码 | 必备 | 本板实现 | 备注 |
|---|---|---|---|---|
| CONNECT | 0xFF | ✓ | ✓ | 返回 RESOURCE=0x05（CAL_PAG+DAQ）、MAX_CTO/MAX_DTO=8 |
| DISCONNECT | 0xFE | ✓ | ✓ | 清 DAQ 配置，保留标定页 |
| GET_STATUS | 0xFD | ✓ | ✓ | sessionStatus 的 bit6 = DAQ running |
| SYNCH | 0xFC | ✓ | ✓（不响应） | 规范要求不回应答 |
| GET_COMM_MODE_INFO | 0xFB | 可选 | ✓ | 返回 7 字节，COMM_MODE_OPTIONAL=0 |
| GET_ID | 0xFA | ✓ | ✓ | 返回长度头 + MTA 指向标识串，主机 UPLOAD 取 32 B |
| SET_REQUEST | 0xF9 | 可选 | ✓（接受不落盘） | 无 EEPROM，STORE_CAL 请求仅应答 OK |
| GET_SEED / UNLOCK | 0xF8/0xF7 | 可选 | ✓ | 种子 4 B，key = seed ^ 0xA5（演示用） |
| SET_MTA | 0xF6 | ✓ | ✓ | 绝对地址 + 地址扩展（本板只用 0） |
| UPLOAD / SHORT_UPLOAD | 0xF5/0xF4 | ✓ | ✓ | UPLOAD 走 MTA 自增；SHORT_UPLOAD 自带地址 |
| DOWNLOAD | 0xF0 | ✓ | ✓ | 一帧最多 6 字节数据（8-1 命令-1 长度） |
| MODIFY_BITS | 0xEC | 可选 | ✓ | 位域读改写，公式见 34.6 坑 6 |
| SET/GET_CAL_PAGE 等 | 0xEB..0xE7 | 可选 | ✓ | 单段单页 RAM 标定，页切换是空操作 |
| DAQ 全套 | 0xD6..0xD3 | 可选 | ✓ | FREE/ALLOC_DAQ/ALLOC_ODT/ALLOC_ODT_ENTRY |
| SET_DAQ_PTR/WRITE_DAQ | 0xE2/0xE1 | ✓(DAQ) | ✓ | 指针 + 条目（地址/大小/位偏移） |
| SET/GET_DAQ_LIST_MODE | 0xE0/0xDF | ✓(DAQ) | ✓ | bit4=时间戳、bit5=PID_OFF（本板不支持） |
| START_STOP_DAQ_LIST/SYNCH | 0xDE/0xDD | ✓(DAQ) | ✓ | mode 2=select，SYNCH(1) 启动选中列表 |
| GET_DAQ_PROCESSOR/RESOLUTION_INFO | 0xDA/0xD9 | ✓(DAQ) | ✓ | 7 字节响应，见下节 |
| GET_DAQ_EVENT_INFO | 0xD7 | 可选 | ✓ | 事件名由主机 UPLOAD 取回 |
| GET_DAQ_CLOCK | 0xDC | 可选 | ✓ | 返回 1 µs 时基低 32 位 |
| PGM 系列（Flash 编程） | 0xD2..0xC8 | 可选 | ✗ | 本章不做，见"待确认项" |

### 34.4.3 错误码与会话状态

表 34-2 列出本板会返回的错误码（取自 ASAM XCP 1.5，代码里是 `XCP_ERR_*`）。

| 错误码 | 名称 | 本板触发场景 |
|---|---|---|
| 0x00 | CMD_OK | 正常 |
| 0x20 | CMD_UNKNOWN | 未实现的命令（如 PGM 系列） |
| 0x21 | CMD_SYNTAX | 命令长度不足（注意：**不能**因为 CAN 帧填充就判错，见坑 2） |
| 0x22 | OUT_OF_RANGE | DAQ 列表号/ODT 号/事件号越界 |
| 0x23 | WRITE_PROTECTED | 写只读区域（XIP Flash） |
| 0x25 | ACCESS_LOCKED | 未 UNLOCK 就 DOWNLOAD，或 UNLOCK 的 key 错 |
| 0x29 | SEQUENCE | UNLOCK 长度与种子长度不符 |
| 0x2A | DAQ_CONFIG | ODT 条目总长超过 DTO 载荷（含时间戳） |
| 0x30 | MEMORY_OVERFLOW | 列表/ODT/条目数超过编译期上限 |
| 0x33 | RESOURCE_TEMP_NA | 预留给"标定页正被应用使用"场景 |

会话状态（GET_STATUS.sessionStatus）只用了两位：bit6 = DAQ running、bit0 = STORE_CAL_REQ（本板不置位）。资源保护状态（RESOURCE_PROTECTION_STATUS）在 `xcp_set_seed(seed, 4)` 打开保护后为 0x05，UNLOCK 成功后清零。

### 34.4.4 DAQ：列表、ODT 与打包

DAQ 的组织是三层（图 34-2）：

1. **DAQ 列表（DAQ list）**：绑定一个**事件通道**（本板 EVT0 = 1 kHz、EVT1 = 100 Hz）+ 预分频 + 优先级；
2. **ODT（Object Descriptor Table）**：一个列表有 1..4 个 ODT，每个 ODT 就是"一帧的数据描述"；
3. **ODT 条目（entry）**：每条 = （绝对地址、字节数、位偏移），`bit_off=0xFF` 表示整字节拷贝。

![图 34-2 DAQ 列表 → ODT → CAN 帧打包](figures/34/fig-34-2-odt-pack.png)

主机的配置顺序（**顺序本身是协议的一部分**）：

```bash
FREE_DAQ                              # 清空动态配置
ALLOC_DAQ(0, 2)                       # 分配 2 个 DAQ 列表
ALLOC_ODT(0, list, odt_count)         # 每个列表分 ODT
ALLOC_ODT_ENTRY(0, list, odt, n)      # 每个 ODT 分条目数
SET_DAQ_PTR(list, odt, entry)         # 移动写指针
WRITE_DAQ(0xFF, size, ext, address)   # 写条目，成功后指针自动 +1
SET_DAQ_LIST_MODE(mode, list, event, prescaler, priority)
START_STOP_DAQ_LIST(2, list)          # 2 = select，返回该列表的 first PID
START_STOP_SYNCH(1)                   # 启动所有已选中列表
```

打包规则（本板实现，与 XCP 1.5 的 `IDF_ABS_ODT_NUMBER` 一致）：

- 每帧 `data[0]` = PID = **绝对 ODT 号**（列表按分配顺序连续编号，PID 0x00..0xFB 可用）；
- `data[1..7]` 按 WRITE_DAQ 的顺序依次放条目内容，**不做对齐、不做逆序**（A2L 的 ODT 顺序就是线上顺序，这是"标定/测量数据错位"的头号来源）；
- 若列表 mode 的 bit4（时间戳）打开，**ODT0 的前 4 字节被时间戳占用**，只剩 3 字节放数据；
- 不足 8 字节的帧用 0x00 补齐到 DLC=8（XCPonCAN 惯例，主机按命令语义取有效长度）。

上面这套打包是**按 8 字节 DTO 写死**的。CAN FD 下 DTO 可以到 64 字节，一帧能装的条目从 1 个
（8 B 去掉 PID 只剩 7 B）变成 15 个，采样率上限差一个数量级，如图 34-3 所示。
CAN 构型目前仍是 8 字节 DTO（U-35-6；同一份协议层在 XCPonEth 构型下是 256 B），这张图是用第 29 章实测的经典/FD 帧时间算出来的理论上限。

![图 34-3 DTO 长度与 DAQ 采样率上限（估算）](figures/34/fig-34-3-dto-size.png)

**图 34-3** ：横轴 DTO 长度，左图每帧可装 4 字节条目数，右图 100% 总线占用下的采样率上限。
经典 8 B DTO 上限约 4.3 k 样本/s，64 B DTO（2 Mbit/s 数据段、单帧 320 µs）约 50 k 样本/s。
脚本 `tools/figs/app34_dto_size.py`。

表 34-3 列出 DAQ 相关的关键位与取值。

| 项目 | 位/字段 | 本板取值 | 含义 |
|---|---|---|---|
| DAQ list mode | bit1 | 0 | 0 = DAQ（测量），1 = STIM（不支持） |
| DAQ list mode | bit4 | 由主机设 | 1 = 帧内带时间戳（S4） |
| DAQ list mode | bit5 | 0 | 1 = PID_OFF（本板不支持，报 ERR_MODE_NOT_VALID） |
| DAQ_PROPERTIES | bit0 | 1 | 1 = 动态配置（FREE/ALLOC 生效） |
| DAQ_PROPERTIES | bit1 | 1 | 支持预分频 |
| DAQ_PROPERTIES | bit4 | 1 | 支持时间戳 |
| DAQ key byte | bit7..6 | 0 | IDF_ABS_ODT_NUMBER（PID = 绝对 ODT 号） |
| timestamp mode | bit7..4 / bit3 / bit2..0 | 0x34 | unit=1 µs、**可变长**（fixed=0）、size=S4 |
| timestamp ticks | u16 | 1 | 1 tick = 1 µs |

### 34.4.5 事件通道与时间戳

事件通道是"什么时候采样"的定义。本板有两个（`xcp_cfg_t.events`）：

- **EVT0 = 1 kHz**：GPTMR0 CH0 周期中断，`reload = 100 MHz / 1000`；中断里`g_tick_cnt++`、跑 PID、`xcp_event(0, now_us())`；
- **EVT1 = 100 Hz**：GPTMR0 CH1，用于低频慢变量（本板示例 1 个 ODT）。

时间戳取 `mcycle / cpu_mhz`（480 MHz ÷ 480 = 1 µs，整除无误差），语义是**事件发生的中断时刻**而不是"帧发到总线的时刻"——这样即使主循环发送被推迟，时间戳仍然描述采样时刻，抖动分析才有意义。1 kHz 事件下实测时间戳间隔 mean=1000 µs、min=max=1000 µs、sd=0 µs：抖动源（GPTMR 硬件分频）比 1 µs 分辨率还小，所以"看起来是 0"，真正的抖动在**主机侧到达时刻**上（总线仲裁 + 网关转发，实测主机侧到达间隔 sd 在毫秒级，见 34.7）。

采样与发送分离是本板的关键设计：

| 阶段 | 上下文 | 做什么 | 为什么 |
|---|---|---|---|
| 采样 | 事件中断 | 读变量、按 ODT 打包、压 16 帧环形队列 | 保证同一事件的所有变量"同一时刻"取值 |
| 发送 | 主循环 `xcp_poll()` | 出队 → `send_dto()` | CAN TX FIFO 满时不能中断里自旋；满了就丢并计数 |

### 34.4.6 A2L 结构与生成流程

A2L（ASAM MCD-2 MC）是"ECU 内存的地图"：告诉主机每个变量**在哪（绝对地址）、多宽（数据类型）、怎么换算（COMPU_METHOD）、什么时候采（EVENT）**。表 34-4 是本章生成的对象。

| A2L 对象 | 本板示例 | 作用 |
|---|---|---|
| `PROJECT/HEADER/MODULE` | HPM5E31_XCP / xcp_can_cal | 工程与模块（一个 ECU 一个 MODULE） |
| `MOD_COMMON` | BYTE_ORDER MSB_LAST + ALIGNMENT_* | 小端 + 各类型对齐（RISC-V） |
| `MOD_PAR MEMORY_SEGMENT` | RAM 0x01200000+256KB、FLASH 0x80000000+1MB | 地址合法性范围 |
| `RECORD_LAYOUT` | `FNC_VALUES 1 FLOAT32_IEEE COLUMN_DIR DIRECT` | 内存布局；**AXIS_PTS 用 `AXIS_PTS_X`** |
| `COMPU_METHOD` | cm_ident / cm_adc_volt / cm_permille | 换算（RAT_FUNC/TAB_VERB） |
| `MEASUREMENT` | g_adc_raw、g_ctrl_y… | 只读观测量 + ECU_ADDRESS |
| `CHARACTERISTIC` | g_kp、g_ki、g_pwm_duty_pm、g_gain_curve(CURVE) | 可写标定量 |
| `AXIS_PTS` | g_gain_axis[8] | CURVE/MAP 的独立轴 |
| `IF_DATA XCP` → PROTOCOL_LAYER/DAQ/EVENT/XCP_ON_CAN | MAX_DTO=8、EVT_1ms、CAN 0x555/0x556 | 传输层与 DAQ 描述 |

生成流程（**地址只能来自 ELF**，手工抄地址是错一个字节就"标定无效"的经典事故）：

```bash
#1) 编译出 ELF（含 .symtab）
bash scripts/build.sh samples/app/xcp_can_cal
#2) 生成 A2L：符号名 -> 绝对地址（纯 Python 解析 ELF32，不依赖 readelf/nm）
python3 tools/gen_a2l.py --elf build/xcp_can_cal-debug/output/demo.elf \
        --vars tools/a2l/xcp_can_cal.json --out samples/app/xcp_can_cal/xcp_can_cal.a2l
#3) 语法 + 一致性校验（DanielT/a2ltool，Rust）
a2ltool samples/app/xcp_can_cal/xcp_can_cal.a2l --check
#    -> "Consistency check complete. No problems found."
#4) 上位机用同一份 A2L 取地址
python3 host/xcp/xcp_cal.py --a2l samples/app/xcp_can_cal/xcp_can_cal.a2l --channel vcan0
```

生成器还做一件校验：**符号大小与声明类型不符就告警**（例如把 `uint16_t[8]` 的数组误写成 UWORD，会提示 16 != 2），`--strict` 时直接报错退出。这件小事能挡住"数组当标量标定"的低级错误。

## 34.5 寄存器与驱动 API 对照

XCP 是纯软件协议，但它落在 MCAN 的收发链路上。表 34-5 把协议字段 → MCAN 寄存器/位域 → 驱动 API → 代码位置对上（**这是本章"寄存器"一节**）。

| XCP 侧 | MCAN 寄存器/位域 | 驱动 API | 代码位置 |
|---|---|---|---|
| CMD 帧接收（0x555） | `MCAN_RXF0S`（F0FL 填充级） | `mcan_read_rxfifo(base,0,&rx)` | `xcp_port_can.c:can_poll()` |
| RES/DAQ 帧发送（0x556） | `MCAN_TXFQS`（TFQF/TFQPI）→ `MCAN_TXBUF` | `mcan_transmit_via_txfifo_nonblocking()` | `xcp_port_can.c:can_send_dto()` |
| 位时序 500 kbit/s SP87.5% | `MCAN_NBTP`（NTSEG1/NTSEG2/NSJW/NBRP） | `mcan_init(cfg.can_timing)` | `xcp_port_can.c:xcp_can_init()` |
| 消息 RAM（AHB SRAM） | `MCAN_SIDFC/XIDFC/RXF0C/TXBC`（全在 AHB SRAM） | `mcan_set_msg_buf_attr()` + `mcan_config_ram()` | `xcp_port_can.c`（坑 1） |
| 过滤器（本板不过滤，软件判 ID） | `MCAN_GFC`（ANFS=接受非匹配帧进 FIFO0） | 默认 `mcan_get_default_config()` | `xcp_port_can.c:xcp_can_init()` |
| 总线状态（bus-off 计数） | `MCAN_PSR`（BO/EP/EW/LEC） | `mcan_is_in_busoff_state()` | `xcp_port_can.c:can_poll()` |
| DAQ 时间戳（1 µs） | `mcycle` CSR（非 MCAN） | `hpm_csr_get_core_mcycle()/cpu_mhz` | `samples/app/xcp_can_daq/src/main.c:now_us()` |
| 事件触发（1 kHz/100 Hz） | GPTMR0 `RLD`/`CHAN` | `gptmr_channel_config()`/`gptmr_enable_irq()` | `samples/app/xcp_can_daq/src/main.c:tmr_setup()` |
| DAQ 变脸读取（任意地址） | —（直接读内存） | `memcpy(&buf[pos], (void*)addr, size)` | `xcp_core.c:odt_pack_entry()` |
| 内存区域白名单 | —（软件限制） | `mem_within(addr,len,write)` | `xcp_core.c:mem_within()` |

## 34.6 例程

表 34-6 是本方向的三个例程：前两个走 CAN（第 35.4 节），第三个是 XCP on UDP（本章"XCP on UDP"一节，本版新增）。

| 例程 | 路径 | 覆盖特性 | 关键配置 | 实测现象 | 日志 |
|---|---|---|---|---|---|
| XCP on CAN DAQ | `samples/app/xcp_can_daq/` | CONNECT/GET_ID/GET_STATUS/内存读写/位操作/种子解锁 + 动态 DAQ（2 列表、时间戳、1 kHz+100 Hz） | CAN0 500k，0x555/0x556，MAX_DTO=8，EVT0 1 kHz、EVT1 100 Hz，16 帧环形发送队列 | 自证全 PASS；事件率 999.0 Hz，3097 frame/s，丢帧 0，每事件打包 3.93 µs | `logs/20261030-172706-xcp_can_daq.log` |
| XCP on CAN 标定 | `samples/app/xcp_can_cal/` | DOWNLOAD/SHORT_UPLOAD 在线标定、MODIFY_BITS、GET_SEED/UNLOCK、Kp 扫参 | 同上 + 1 kHz 控制环（PID + 一阶对象 + 增益调度表） | 读写/MODIFY_BITS/UNLOCK 全 PASS；扫参 10 点峰值 913→850‰（1.2 s 窗口未收敛，见待确认项） | `logs/20261030-173517-xcp_can_cal.log` |
| XCP on UDP（XCPonEth） | `samples/app/xcp_eth/` | CONNECT/GET_STATUS/GET_ID/SET_MTA/UPLOAD/SHORT_UPLOAD/DOWNLOAD/MODIFY_BITS + 动态 DAQ + 10 kHz 压力 + 板端统计镜像 | 板 192.168.2.100:5555，MAX_CTO=255/MAX_DTO=256，4 列表×8 ODT×16 条目，EVT0 1 kHz / EVT1 10 kHz / EVT2 100 Hz | 1 kHz/4 ODT：990 pkt/s、3960 msg/s、1.25% 链路丢包；10 kHz/8 ODT：9724 pkt/s、77791 msg/s、6.85 Mbit/s、帧错误 0；标定写读回全 PASS | `logs/20261102-155848-xcp_eth.log`、`logs/20261102-155848-host-xcp-eth-rate.log` 等 |

CAN 的两个例程都不需要上位机：板上 CAN0 跑从站、CAN1 跑一个"最小 XCP 主机"（`src/xcp_test_master*.c`），两者在同一段总线上跑完整流程；接上位机时主机侧换成 cannelloni 网关（`shell` 里 `gw on`）。

XCP on UDP 例程（`samples/app/xcp_eth/`）是**纯粹的以太网直连**（Bench C）：板端 `app_lwip_init()` 起 ENET0 RGMII + YT8011AN，静态 IP 192.168.2.100，UDP 5555；主机 `enp6s0` 192.168.2.1 直接跑 `host/xcp/xcp_daq.py --interface eth`，不经过 cannelloni。它复用同一份协议层 `xcp_core.c`，只把传输层换成 `xcp_port_udp.c`，并在 CMake 里用 `-DXCP_MAX_DTO=256` 放大 DTO 上限（CAN 构型仍是 8）。

一段关键代码（DAQ 打包，`components/app_xcp/xcp_core.c`）：

```c
void xcp_event(uint8_t evt, uint32_t ts_us)
{
    for (uint8_t i = 0; i < XCP_MAX_DAQ_LISTS; i++) {
        xcp_daq_list_t *l = &s_daq[i];
        if ((l->in_use == 0u) || ((l->state & XCP_DAQ_STATE_RUNNING) == 0u) || (l->event != evt)) {
            continue;
        }
        if (l->prescaler > 1u) {                 /* 预分频：每 N 个事件采一次 */
            if (++l->prescaler_cnt < l->prescaler) { continue; }
            l->prescaler_cnt = 0u;
        }
        bool has_ts = ((l->mode & XCP_DAQ_MODE_TIMESTAMP) != 0u);
        for (uint8_t o = 0; o < l->odt_count; o++) {
            uint8_t buf[XCP_MAX_DTO];
            uint8_t pos = 0u;
            buf[pos++] = (uint8_t) (l->first_pid + o);          /* PID = 绝对 ODT 号 */
            if (has_ts && (o == 0u)) {                          /* 时间戳独占 ODT0 前 4 字节 */
                wr_u32(&buf[pos], ts_us);
                pos = (uint8_t) (pos + 4u);
            }
            for (uint8_t e = 0; e < l->odt_entry_count[o]; e++) {
                odt_pack_entry(&buf[pos], &l->entry[o][e]);      /* 地址/大小/位偏移 */
                pos = (uint8_t) (pos + l->entry[o][e].size);
            }
            (void) q_push(buf, pos);                             /* 中断里只入队，不发送 */
        }
    }
}
```

## 34.7 上板实测

测量方法：板内主机（CAN1）按上位机的流程发命令、收 DTO，统计在**从站侧**（`xcp_get_stats()`）与**主机侧**（收到的帧数/时间戳差）两处同时做，因此"丢帧"能被交叉验证（从站说发了多少、主机说收到多少）。

表 34-7 是 2026-10-30 的实测汇总（`logs/20261030-172706-xcp_can_daq.log`，自证 PASS，FAIL=0）。

| 指标 | 配置/期望 | 实测 | 结论 |
|---|---|---|---|
| DAQ 事件率（EVT0） | 1000 Hz | **999.0 Hz** | -0.1%，来自 GPTMR 分频整数误差 |
| list0 DAQ DTO 帧数（1 s） | 3000 帧（3 ODT/事件） | **3003 帧** | 与事件数 1001 一致 |
| DAQ 总帧率 | ~3100 frame/s | **3097 frame/s** | 500 kbit/s 下总线负载约 37% |
| 事件丢失（按 g_tick_cnt 连续性） | 0 | **0** | 无丢帧 |
| 从站发送队列丢弃 | 0 | **0** | 16 帧队列够用 |
| CAN TX FIFO 满 / bus-off | 0 / 0 | **0 / 0** | 500k 下 3 帧/ms 不拥塞 |
| 时间戳间隔 mean/min/max | 1000 µs | **1000 / 1000 / 1000 µs** | 硬件触发，1 µs 分辨率下无可测抖动 |
| 时间戳间隔 sd | ≈0 | **0 µs** | 同上 |
| 每事件打包开销（已配 2 列表） | — | **3.93 µs/事件** | 1 kHz 下占 CPU 0.39% |
| 每事件打包开销（空载） | — | **1.56 µs/事件** | 说明开销随 ODT 条目线性增长 |
| 命令往返（CONNECT 等） | — | 全部无超时（timeouts=0） | 板内直连无网关延迟 |

![图 34-4 实测：事件率/帧率/抖动与打包开销](figures/34/fig-34-4-measured.png)

标定侧实测（`logs/20261030-173517-xcp_can_cal.log`）：

| 项 | 结果 |
|---|---|
| `SHORT_UPLOAD` 读 FLOAT32（g_kp） | PASS，与固件变量一致 |
| `DOWNLOAD` 写 g_kp=1.75 | PASS，**写后同一毫秒的下一个控制周期即生效** |
| 写后 `SHORT_UPLOAD` 回读 | PASS（一致） |
| `DOWNLOAD` 写 UWORD（占空比/LED 周期） | PASS |
| `MODIFY_BITS` bit8..11 置 0xA | PASS（0xCAFEB0B0 → 0xCAFEBAB0） |
| 越界地址 DOWNLOAD（0x10000000） | 被拒，ERR=0x23（WRITE_PROTECTED），**没有触发总线错误** |
| `GET_SEED` 4 B / 错 key UNLOCK | 返回 11 22 33 44 / ERR=0x25（ACCESS_LOCKED） |
| 正确 key UNLOCK | PASS（key = seed ^ 0xA5） |
| Kp 扫参 0.1→2.0（10 点） | 峰值 913→850‰（Kp 越大峰值越低），1.2 s 窗口内未进入 ±5% 带 |

### 34.7.1 上位机链路（Bench B：主机 vcan0 ↔ cannelloni ↔ 板 CAN1 网关 ↔ CAN0 从站）

板端固件在同一份镜像里集成了共享底座网关（`app_lwip` + `app_can_gw`），shell 命令
`gw on` 把 CAN1 交给网关（CAN1 ↔ UDP 4000），主机侧 `host/xcp/run_daq.sh` 负责
起 vcan0（带 500 kbit/s 限速）+ cannelloni 隧道。一次 `hwrun --post` 即可完成
"烧写 → 抓串口 → 上位机采集"，全程持锁 ≤90 s。

命令：

~~~bash
bash scripts/hwrun.sh app/xcp_can_daq --secs 8 --send "gw on" \
     --post "timeout 80 bash host/xcp/run_daq.sh 6" --lock-wait 1800 --no-build
~~~

表 34-8 是主机侧实测（`logs/20261030-180205-xcp_can_daq.log`，DAQ PASS）。注意与表 34-7 的区别：
表 34-7 是**板内直连**，表 34-8 多了 cannelloni UDP 隧道与网关转发，因此能看到真实的时间抖动。

| 指标 | 实测 | 说明 |
|---|---|---|
| CONNECT 响应 | resource=0x05、MAX_CTO=8、MAX_DTO=8、proto=0x14 | 与 A2L/IF_DATA 里声明一致 |
| GET_ID | `HPM5E31-XCP-CAN 1.0 (DAQ demo)` | 长度头 + UPLOAD 取回 32 B |
| DAQ first PID | 0 | 动态分配，PID = 绝对 ODT 号 |
| 实测事件率 | **1000.0 Hz**（配置 1000 Hz） | 与板内直连一致 |
| 事件丢失（tick 连续性） | **0** | 6 s 内 6000 次事件全到 |
| DAQ 帧数（6 s） | ODT0/ODT1/ODT2 各 **6000** → 3000 frame/s | 3 ODT/事件 |
| 时间戳间隔 mean | **1000.2 µs** | 从站 1 µs 时基 |
| 时间戳间隔 min/max | **1000 / 2000 µs** | 2000 的那次是 cannelloni 攒包（`-t 1000`）造成的主机侧"漏读一拍" |
| 时间戳间隔 sd | **12.9 µs** | 板内直连是 0（sd=0），抖动全部来自 UDP 隧道/网关 |
| 端到端时延抖动 sd | **37 µs**，max-min 1300 µs | 含网关转发与 cannelloni 1 ms 攒包超时 |
| 结论 | **DAQ PASS（lost=0, rate=1000.0 Hz）** | CSV：`out/xcp_daq.csv` |

这一组数字正好回答了"板内自证 sd=0 是不是假的"：**不是**，采样时刻由硬件事件决定，
s d=0 只在"从站自己看自己"时成立；一过主机链路，1 ms 攒包的 cannelloni 立刻把
抖动放大到 12.9 µs、偶尔还漏一拍（2000 µs）。做标定时的经验值是：
**DAQ 时间戳看从站，链路抖动看主机，两者不能混为一谈**。

## 34.8 性能与调优

**总线负载是 DAQ 的硬上限**。经典 CAN 500 kbit/s 下一帧 8 字节的线上时间约 108~125 µs（取决于位填充）：

- 每事件 3 帧 × 1 kHz = 3000 帧/s ≈ **37% 总线负载**（实测 3097 frame/s）；
- 想上 2 kHz 或更多变量，只有三条路：①降到 1 ODT/事件（换 CAN FD 或增大 DTO）；②改用 **XCP on CAN FD**（DLC=64，一帧能塞几十个变量）；③换 **XCP on UDP**（1472 字节/包，本章实测 10 kHz/8 ODT = 77791 msg/s、6.85 Mbit/s，是 CAN 500k 的 20~35 倍载荷吞吐）。

**每样本开销**（实测 3.93 µs/事件，含 2 个列表 4 个 ODT）：主要是 `memcpy` 与 `wr_u32`，不是瓶颈；1 kHz 下占用 0.39% CPU，10 kHz 也才 3.9%。

**调优清单**（按收益排序）：

| 手段 | 收益 | 代价 |
|---|---|---|
| 关闭时间戳（mode bit4=0） | ODT0 从 3 字节变 7 字节，同样变量少 1~2 个 ODT → 帧率降 30~50% | 失去采样时刻信息 |
| 合并相邻变量（A2L 里用一条 MEASUREMENT 描述数组） | 条目数减少，打包更快 | 需要固件里变量相邻且类型一致 |
| 提高预分频（prescaler） | 帧率线性下降 | 时间分辨率下降 |
| 只给"真要看的"变量建 DAQ 列表 | 帧数按需 | A2L 里 DAQ 映射要维护 |
| 改 CAN FD（1M/2M，DLC≤64） | 一帧塞下大部分变量，帧数降一个数量级 | 需要 CAN FD 收发器/对端支持（本板已支持，见第 09 章） |
| 改 XCP on UDP | 单包 1472 B、单包多 ODT；实测 6.85 Mbit/s / 77791 msg/s，提升 1~2 个数量级 | 需要以太网链路；UDP 无重传，要接受丢帧并用 CTR 统计（本章 XCP on UDP 一节） |

## 34.9 坑与注意事项

1. **HPM5E31 的 MCAN 消息 RAM 在 AHB SRAM，必须先注册缓冲区**。现象：`mcan_init()` 返回 2（`status_invalid_argument`），CAN 完全不通。原因：`hpm_soc_feature.h` 里 `MCAN_SOC_MSG_BUF_IN_AHB_RAM=1`，不调 `mcan_set_msg_buf_attr()` 时 `mcan_get_ram_size()` 返回 0 → `mcan_init` 走 `status_invalid_argument` 分支。解决：每个 MCAN 实例各放一份 `ATTR_PLACE_AT(".ahb_sram") uint32_t buf[MCAN_MSG_BUF_SIZE_IN_WORDS]`（640 word = 2560 B），在 `mcan_get_default_config()` **之前**调 `mcan_set_msg_buf_attr()`。本板两个节点用掉 5 KB AHB SRAM（32 KB 的 15.6%）。
2. **CAN 帧填充 vs 命令长度**。主机（含 pyxcp，`max_dlc_required=true`）会把 CRO 补到 DLC=8，从站读到 `rx.dlc` 永远是 8。若用 `if (len != 期望字节数) 报 SYNTAX` 就永远对不上——**固定长度命令必须用 `len < 期望` 判断**（多出来的都是填充）。本板第一版就踩了这个：CONNECT 一进来就 ERR 0x21。
3. **`ALLOC_ODT_ENTRY` 的 CRO 是 6 字节不是 7 字节**：`cmd, mode, list_lo, list_hi, odt, entry_count`。多写一个字节会让从站读到 0 条目，DAQ 一帧都发不出来，而错误码还是"看起来没问题"的 OK。
4. **PID 上限**：PID 0x00..0xFB 可给 DAQ，0xFC..0xFF 被 SERV/EV/ERR/RES 占用。第一版把上限写成 `0xFC - 0xFC = 0`，结果所有列表的 `odt_count` 被"合法地"清成 0，表现为"配置全 PASS 但一帧都没有"。
5. **时间戳吃掉 ODT0 的 4 字节**：`MAX_DTO=8` 时 ODT0 只剩 3 字节，4 字节变量必须排到 ODT1/ODT2；`WRITE_DAQ` 之后打开时间戳要重新校验容量，否则首帧静默丢数据（本板在 `SET_DAQ_LIST_MODE` 里重算，超了报 ERR_DAQ_CONFIG）。
6. **`MODIFY_BITS` 作用在当前 MTA 上，不带你给的地址**。它只带 shift/AND/XOR 三个参数，地址来自之前的 SET_MTA。本板第一版自证就写错了：SHORT_UPLOAD 不改 MTA，于是 MODIFY_BITS 改到了 `g_sw_flag+4` 那个变量上，"结果没变"查了半天。公式（ASAM XCP 1.5）：`mem = (mem & ~((uint32)(~AND) << SHIFT)) ^ ((uint32)XOR << SHIFT)`。
7. **A2L 语法比想象严格**（`a2ltool --check` 逐条教做人）：`PROJECT_NO` 后面跟**标识符**不是数字；`MEMORY_SEGMENT` 位置参数到 SIZE 为止、后面再写 `-1 -1 -1 -1 -1` 直接语法错；`AXIS_PTS` 的位置参数是 `address, input_quantity, deposit, max_diff, conversion, max_axis_points, lower, upper`（`max_diff` 很容易漏）；**AXIS_PTS 引用的 RECORD_LAYOUT 必须含 `AXIS_PTS_X`**，否则一致性检查报 "does not have AXIS_PTS_X"。
8. **DAQ 配置与采样无锁**。本板为了省中断延迟，`xcp_event()` 直接读 `s_daq[]`，主循环改配置时没有互斥。正确用法是"停止 DAQ 后再改配置"（主机侧 pyxcp/CANape 都这么做）；如果应用要在跑 DAQ 时改，得双缓冲或关中断。这条写在这里是因为它不会报错，只会在极端时序下出一帧怪数据。
9. **XCPonEth 的 LEN 不含 4 B 头，不是"含头"**。按 ASAM XCP 1.5 与 XCPlite/pyxcp 两份实现，LEN 是协议层报文长度；若按"含头"实现，主机（pyxcp `EthReceiver`）会把多出来的 4 字节当成载荷，CONNECT 之后所有命令都解析错位。双方报文对照表见 表 34-11、表 34-13。
10. **4 B 对齐填充必须计入非最后一条消息的 LEN**。pyxcp 的 `feed_datagram()` 是按 LEN 顺序切分的（`datagram = datagram.subspan(4 + length)`），消息之间若留了没计入 LEN 的填充，下一条消息头会从填充字节开始解析，表现为"约 1/4 的命令随机超时、DAQ 偶尔丢一整包"。本实现的做法是：追加新消息时回填上一条的 LEN 与填充，最后一条保持精确长度。
11. **MTU/分片**：本板 ENET MTU=1500，UDP 载荷上限 1472 B（1500-20-8），同时 lwIP 的 `IP_FRAG` 未开——超过就丢，不会自动分片。算 DAQ 包大小时按"4 B 头 + align4(ODT) × 条数 ≤ 1472"核算；本板 8 ODT 小包远未到上限（表 34-12），但换成大 ODT（如 64 B 条目）时要重新算。
12. **UDP 无重传，控制命令要区分"能不能重发"**。DAQ 丢了就丢了（CTR 只统计不修复）；CONNECT/GET_STATUS/SET_MTA/SHORT_UPLOAD 等幂等命令可以超时重发，但 **UPLOAD 会推进 MTA、DOWNLOAD 写后推进 MTA、WRITE_DAQ 会自动移动条目指针**，盲重发会把数据写到错误地址/条目。本仓库的做法：客户端只为 `RETRY_SAFE` 集合重发，标定写用"重设 MTA 再写"、DAQ 写用"重设 DAQ_PTR 再写"（`host/xcp/xcp_eth_client.py`、`host/xcp/xcp_daq.py`）。
13. **"丢包"要定位到段，不能一句"UDP 不可靠"糊过去**。本板给了一个可复用的方法：从站每秒把 `tx_datagrams/tx_msgs/tx_drop/daq_frames/daq_dropped/overload` 镜像到全局变量，主机停止 DAQ 后用 SHORT_UPLOAD 读回（A2L 里的 `g_stat_*`）。三档实测板端 drop/overload 全 0，而同窗口 ping 0% 丢包、XCP 丢 0.9%~3.1%，说明丢在"板 ENET TX → 车载以太网盒 → 主机 enp6s0"的突发队列/环形缓冲上；主机侧 `SO_RCVBUF` 已调到 1 MiB，下一步可查以太网盒的队列/流控和板端 ENET TX 环。

## 34.10 优劣评价与同类对比

### 34.10.1 XCP vs CCP vs UDS（表 34-9）

| 维度 | XCP（本章） | CCP | UDS（第 29/30 章） |
|---|---|---|---|
| 定位 | 标定 + 测量 | 标定（XCP 前身） | 诊断 + 刷写 |
| 传输层 | CAN/CAN FD/Ethernet/USB/SxI | 只有 CAN | 只有 CAN（ISO-TP） |
| 报文 | CTO/DTO，命令+DAQ 混合 | CRO/DTO，8 字节 | 请求/响应，服务号固定 |
| 地址模型 | 主机知道**绝对地址**（A2L） | 同 XCP | 按 DID/例程号，ECU 内部映射 |
| 数据量 | DAQ 可 1 kHz~10 kHz 多变量 | 类似但 8 字节限制更死 | 单次几十~几千字节，不擅长周期 |
| 安全 | SEED/KEY（可选） | 同 | 27 服务 + 编程会话 |
| 典型工具 | CANape/INCA/pyxcp | CANape（老） | 诊断仪/OTA |

### 34.10.2 CAN vs UDP 传输（表 34-10）

| 维度 | XCP on CAN（已实测） | XCP on UDP / XCPonEth（已实测） |
|---|---|---|
| 单包载荷 | 7 字节（8 - PID） | 1472 字节（1500 MTU - 28） |
| 一帧能塞几个 U32 | 1 个（带时间戳时 ODT0 只 3 字节） | 92 个（9 B ODT，表 34-12）；单包最多 122 条 5 B ODT |
| 每帧/每消息开销 | 108~125 µs（500k 线上时间） | 4 B 传输层头 + 0~3 B 对齐填充；8 条 ODT 共享一个以太网帧 |
| 1 kHz DAQ 负载 | 3000 frame/s ≈ 37% 总线 | 3960 msg/s = 990 pkt/s ≈ 0.04% 线速（0.42 Mbit/s 载荷） |
| 10 kHz/8 ODT 负载 | 不可用（8 B DTO 塞不下 8 条目） | 77791 msg/s = 9724 pkt/s ≈ 0.7% 线速（6.85 Mbit/s 载荷） |
| 命令往返 RTT | 板内直连 478 µs；Bench B（cannelloni）2.26 ms、p99 2.49 ms | **p50 83.6~109.9 µs**（直连千兆），丢包时幂等命令重发 |
| 丢帧特征 | 实测 0（CAN 无重传但总线仲裁确定，负载 37% 不拥塞） | 1.25%（6 s）、3.09%（10 kHz 压力）、0.87%（30 s）——**UDP 链路丢包，板端 drop=0** |
| 传输层实现 | 单帧收发，简单 | 4 B 头 + 多消息拼包 + CTR 统计；~330 行 |
| 典型用途 | 台架标定（现有 ECU 都有 CAN） | 大数据量测量、域控/中央计算 |
| 本板状态 | **已实测**（本章） | **已实测**（本章 XCP on UDP 一节，三档 + 30 s 长测） |

结论：**标定用 CAN 够了（几十字节的参数），测量用 CAN 会很快撞到 8 字节墙**；本板 1 kHz × 3 帧已经占 37% 总线、10 kHz/8 ODT 用 CAN 根本塞不下——这正是本章新增 XCP on UDP 一节的动因（10 kHz/8 ODT 实测 77791 msg/s、6.85 Mbit/s，帧错误 0）。

## 34.11 XCP on UDP（XCPonEth）：协议、实现与实测

XCP on CAN 的 DTO 只有 8 字节：1 kHz 采 5 个变量就要 3 个 ODT、3000 帧/s、约 37% 总线负载（表 34-7）。
想上 10 kHz 或几十个变量，必须换传输层。本节把上一节的 CAN 从站扩展成 **XCPonEth**：板端 lwIP UDP 5555
+ 4 B 传输层头 + 单包多 ODT，主机侧自写 socket 客户端，并与 CAN 路径做同口径对比（表 34-10、图 34-6）。
板端新增文件是 `components/app_xcp/xcp_port_udp.c`（约 330 行），例程是 `samples/app/xcp_eth/`。

### 34.11.1 为什么测量走 UDP 而不是 TCP

XCPonEth 标准同时定义 TCP 与 UDP 两种传输层。本实现只做 UDP，理由不是"省事"：

- **测量是单向数据流**：DAQ 只有从站→主机一个方向，UDP 无连接、无队头阻塞，丢一帧不影响后续帧；
- **TCP 重传会把"旧数据"迟到送进来**：1 kHz 数据流里，TCP 重传的 200 ms 前旧帧对在线标定没有价值，
  反而让时间戳与数据错位；UDP 丢帧是"看得见的缺口"，主机用 CTR 与测量变量（`g_tick_cnt`）双重判定；
- **控制命令少而小**：CONNECT/DAQ 配置/标定读写都是几十字节的小命令，UDP 往返在局域网里 p50 只有 83.6 µs（表 34-14），
  丢包时主机重发一次即可（本客户端对幂等命令自动重试，见"坑 12"）。

### 34.11.2 线上格式：4 B 头 + 多条消息

一个 UDP 数据报的载荷不是"一条 XCP 报文"，而是**一条或多条 XCP 消息首尾相接**（图 34-5）：
每条消息 = `LEN(2 B, 小端) | CTR(2 B, 小端) | 协议层报文(LEN 字节) | 填充(0~3 B)`。

![图 34-5 XCPonEth 传输层头与单包多 ODT 格式](figures/34/fig-34-5-eth-frame.png)

表 34-11 逐字段说明，并给出与两份上游实现的对照——这张表是本章争议最大的一处，值得单独看。

| 字段 | 长度 | 含义 | 本实现取值 | 上游对照 |
|---|---|---|---|---|
| LEN | 2 B，小端 | **协议层报文长度，不含 4 B 传输层头** | DAQ 消息：精确长度；非最后一条另加填充后写回 LEN | XCPlite `src/xcptl_cfg.h`："A message is: WORD len + WORD ctr + protocol layer packet + fill"；pyxcp `cpp_ext/framing.hpp` 的 `prepare_request()` 写入 `xcp_packet_size = cmd + data`，也不含头 |
| CTR | 2 B，小端 | 发送方逐消息递增的包计数，回绕到 0 | 每条消息 +1（DAQ 与响应共用同一计数） | XCPlite `xcpethtl.c:891` 起逐个消息写 `(ctr << 16) | len`；pyxcp 用 `header_ctr=2` 解析 |
| 协议层报文 | LEN B | 首字节是 PID（0xFF/0xFE/0xFD 或 0x00..0xFB 的 ODT 号） | DAQ 一帧一个 ODT；命令响应一个 RES/ERR | 同左 |
| 填充 | 0~3 B | 把**下一条消息**的 LEN/CTR 对齐到 4 B；值填 0 | 仅"后面还有消息"时填充；最后一条不填充 | XCPlite `XCPTL_PACKET_ALIGNMENT=4`，DAQ 消息 `assert(l % 4 == 0)`；CRM 不填充 |

> **任务书 vs 标准**：需求里写的是"4 B 头：length(2B)+counter(2B)，注意 length 含头"。
> 按 ASAM XCP 1.5 与 XCPlite/pyxcp 两份实现，**LEN 不含 4 B 头**。本实现按标准做，
> 否则 pyxcp 会把多出来的 4 字节当成载荷，命令全部解析错位。这条列进表 34-13 第 1 行，作为"规范以实现为准"的实例。

4 B 对齐的规则比"长度含头"更容易踩：一个数据报里多条消息时，**非最后一条**消息的 LEN 要把填充算进去，
最后一条保持精确长度。原因在接收侧：pyxcp 的 `EthReceiver::feed_datagram()` 是
`datagram = datagram.subspan(header_size + length)` 顺序切分的，如果消息之间留有未计入 LEN 的填充，
下一条消息的头就会从填充字节开始解析，表现为"约 1/4 的命令随机超时"。

### 34.11.3 单包多 ODT 的打包

单包容量的账很好算：一个 UDP 载荷上限 1472 B（1500 MTU - 20 IPv4 - 8 UDP），
每条消息占 `4 + align4(ODT 载荷)` 字节（表 34-12）。本板把 `XCP_MAX_DTO` 从 CAN 构型的 8 B
放大到 256 B（`samples/app/xcp_eth/CMakeLists.txt` 里 `-DXCP_MAX_DTO=256`），DAQ 资源同时放大到
4 列表 × 8 ODT × 16 条目。

| ODT 载荷 | 单条消息占字节 | 1472 B 最多几条 | 说明 |
|---|---|---|---|
| 5 B（1 B PID + 1 个 U32） | 12 | **122** | 10 kHz/8 ODT 压力跑的实际载荷量级 |
| 9 B（PID + 时间戳 4 B + U32） | 16 | **92** | 带时间戳的 ODT0 |
| 20 B | 24 | **61** | 5 个 U32 |
| 64 B | 68 | **21** | 16 个 U32 |
| 256 B（MAX_DTO 上限） | 260 | **5** | 162 B 以上数据只能靠"多条消息/多包" |

实测（表 34-14 的压力档）：配置 8 ODT/事件，每个数据报正好 8 条消息（`max_msgs/pkt=8`、
"单包平均消息数 8.00"），10 kHz 下 9724 pkt/s、77791 msg/s、835.7 KB/s（6.85 Mbit/s 载荷）。
可见离 1472 B 的单包上限还很远——限制来自"事件率 × ODT 数"，不是链路 MTU。

实现上只做了两件事（完整代码见 `components/app_xcp/xcp_port_udp.c`）：
DAQ 仍在定时器中断里按 ODT 打包进队列；传输层把主循环连续取出的多条 ODT 消息拼进同一个发送缓冲，
装不下时先 flush。关键片段：

```c
static int eth_append_msg(const uint8_t *payload, uint16_t len)
{
    uint16_t need, pad_prev = 0u, hdr_off;

    if (s_msg_cnt > 0u) {
        pad_prev = (uint16_t) (align4(s_last_len) - s_last_len);   /* 上一条补到 4 B */
    }
    need = (uint16_t) (XCP_UDP_HDR_LEN + len);
    if ((uint32_t) s_tx_len + pad_prev + need > XCP_UDP_PAYLOAD_MAX) {
        if (eth_flush_internal() != 0) { return -1; }              /* 装不下：先发走 */
        pad_prev = 0u;                                             /* 上一条成了最后一条，不填充 */
    }
    if ((s_msg_cnt > 0u) && (pad_prev > 0u)) {
        memset(&s_tx[s_tx_len], 0, pad_prev);
        s_tx_len = (uint16_t) (s_tx_len + pad_prev);
        wr_u16le(&s_tx[s_last_off], (uint16_t) (s_last_len + pad_prev));  /* LEN 含填充 */
    }
    hdr_off = s_tx_len;
    wr_u16le(&s_tx[hdr_off], len);                        /* 最后一条：LEN 精确 */
    wr_u16le(&s_tx[hdr_off + 2u], s_st.tx_ctr);           /* CTR：逐消息 +1 */
    s_st.tx_ctr = (uint16_t) (s_st.tx_ctr + 1u);
    memcpy(&s_tx[hdr_off + XCP_UDP_HDR_LEN], payload, len);
    s_tx_len = (uint16_t) (s_tx_len + need);
    s_last_off = hdr_off; s_last_len = len; s_msg_cnt++;
    return 0;
}
```

### 34.11.4 与 lwIP/ENET 的衔接（本章"寄存器"补充）

XCPonEth 落在 lwIP 的 UDP 层，UDP 又落在 ENET 的 DMA 收发环上。表 34-16 把协议动作、
ENET 寄存器/描述符、lwIP 与 SDK API、代码位置对上，和表 34-5（CAN 侧）合起来就是完整的收发链路。

| XCPonEth 动作 | ENET 寄存器/描述符 | lwIP/SDK API | 代码位置 |
|---|---|---|---|
| 建 UDP 5555 服务端 | — | `udp_new()` / `udp_bind()`（`app_udp_socket()`） | `xcp_port_udp.c:xcp_udp_init()` |
| 收命令（CTO） | `ENET_DMA_CH_RXDESC` → `ENET_RXBS`（收包状态/长度） | `enet_common_handler()` → `ethernetif_input()` → `udp_recv` 回调 | `app_lwip.c:app_lwip_poll()`、`xcp_port_udp.c:udp_recv_cb()` |
| 发响应/DAQ | `ENET_DMA_CH_TXDESC`（TDES0/1，OWN 位） | `pbuf_alloc(PBUF_RAM)` + `udp_sendto()` | `xcp_port_udp.c:eth_flush_internal()` |
| 校验和 | `ENET_DMA_CH_TXDESC` 的 checksum offload 位 | lwIP 配置 `CHECKSUM_GEN_*=0`（硬件插入） | `components/app_lwip/app_lwip.cmake` |
| 帧 CRC | `ENET_DMA_CH_TXDESC` 的 CRC 模式 | `ENET_TX_HW_CRC_MODE=0`（append） | `samples/app/xcp_eth/CMakeLists.txt` |
| MTU/分片 | `ENET_*_MAXFRAMELEN` | 本板 MTU=1500，UDP 载荷上限 1472，超了要自己分片 | 本章"坑 11" |
| 收发缓冲 | `ENET_TX/RX_BUFF_COUNT`（板级宏） | `PBUF_POOL_SIZE=ENET_TX_BUFF_COUNT` | `components/app_lwip/inc/lwipopts_app.h` |
| 时间戳 | —（CPU `mcycle`，非 ENET） | `hpm_csr_get_core_mcycle()/cpu_mhz` | `samples/app/xcp_eth/src/main.c:now_us()` |

两个容易忽略的点：①收包回调由 `app_lwip_poll()` 在主循环上下文里触发，所以命令处理（`xcp_rx_cto`）
和响应 flush 都在主循环里完成，**不要在回调里 printf/阻塞**；②发送用 `pbuf_alloc(PBUF_RAM)`，
本板 `MEM_SIZE=24 KB`，一个 1472 B 的 pbuf 足够，但别在中断里发。

### 34.11.5 命令与时序

主机侧流程与 CAN 完全一致（图 34-7）：CONNECT → GET_STATUS/GET_ID/GET_DAQ_CLOCK → 动态 DAQ 配置
（FREE_DAQ/ALLOC_DAQ/ALLOC_ODT/ALLOC_ODT_ENTRY/SET_DAQ_PTR/WRITE_DAQ/SET_DAQ_LIST_MODE）→
START_STOP_SYNCH(1) → DAQ 数据流 → START_STOP_SYNCH(0)/DISCONNECT。区别只在传输层：每个 CTO 是一个带 4 B 头的
UDP 数据报，响应按源地址回给主机。

![图 34-7 XCPonEth 主机/从站命令时序与实测耗时](figures/34/fig-34-7-eth-sequence.png)

表 34-13 是命令码对照。需求书里给的一串命令码（0xFA=GET_STATUS、0xF5=SET_MTA、0xF6=DOWNLOAD、
0xD4=SHORT_UPLOAD 等）与 ASAM 标准不符，如果照抄会得到一个"能连上、读不出"的从站；
本实现与既有 CAN 从站、pyxcp、XCPlite 保持同一套标准码。

| 功能 | 需求书写的码（笔误） | ASAM XCP 1.4/1.5 标准码（本实现） | 本板支持 |
|---|---|---|---|
| GET_STATUS | 0xFA | **0xFD**（0xFA 是 GET_ID） | 是 |
| SET_MTA | 0xF5 | **0xF6**（0xF5 是 UPLOAD） | 是 |
| UPLOAD | 0xF4 | **0xF5**（0xF4 是 SHORT_UPLOAD） | 是 |
| DOWNLOAD | 0xF6 | **0xF0**（0xF6 是 SET_MTA） | 是 |
| SHORT_UPLOAD | 0xD4 | **0xF4**（0xD4 是 ALLOC_ODT） | 是 |
| SHORT_DOWNLOAD | 0xD5 | **0xED**（0xD5 是 ALLOC_DAQ） | 未实现（本板标定用 SET_MTA+DOWNLOAD） |
| DAQ 配置 | 0xD6/0xD7/0xD8/0xDC/0xDD | **0xD6/0xD5/0xD4/0xD3**（FREE/ALLOC_DAQ/ALLOC_ODT/ALLOC_ODT_ENTRY）、**0xE2/0xE1/0xE0**（SET_DAQ_PTR/WRITE_DAQ/SET_DAQ_LIST_MODE）、**0xDE/0xDD**（START_STOP_DAQ_LIST/SYNCH）、**0xDC**（GET_DAQ_CLOCK） | 是 |
| 命令序列 0xF0/0xF1/0xF2 | — | 0xF0=DOWNLOAD、0xF1=USER_CMD、0xF2=TRANSPORT_LAYER_CMD | 仅 DOWNLOAD；USER_CMD/TRANSPORT_LAYER_CMD 未实现 |

### 34.11.6 上板实测

测量方法：主机 `host/xcp/xcp_daq.py --interface eth` 依次跑三档，全部在同一块板、同一次持锁窗口内完成，
串口日志与主机输出分别归档。板端统计由 `samples/app/xcp_eth` 每秒镜像到全局变量，主机在停止 DAQ 后
用 `SHORT_UPLOAD` 读回（`g_stat_*`），因此"板端发没发、发失败没有"和"主机收没收到"能分开验证。

表 34-14 是三档实测（日志：`logs/20261102-155848-xcp_eth.log` + `logs/20261102-155848-host-xcp-eth-*.log`，
以仓库实际文件名为准；主机输出见 `logs/20261102-155848-host-xcp-eth-rate.log` 等三份）：

| 指标 | rate 档：EVT0 1 kHz / 4 ODT / 6 s | stress 档：EVT1 10 kHz / 8 ODT / 6 s | long30 档：EVT0 1 kHz / 4 ODT / 30 s |
|---|---|---|---|
| 收到事件（ODT0） | 5941 | 58344 | 29790 |
| 主机侧事件率 | 990.0 Hz | 9723.9 Hz（含丢帧后的到达率） | 993.0 Hz |
| 板端 tick 速率（真实事件率） | 999.9 Hz | 9999.3 Hz | 1000.0 Hz |
| DAQ 消息总数 | 23764 | 466752 | 119160 |
| 消息率 | 3960 msg/s | 77791 msg/s | 3972 msg/s |
| UDP 数据报率 | 990 pkt/s | 9724 pkt/s | 993 pkt/s |
| 单包平均消息数 | 4.00 | 8.00 | 4.00 |
| UDP 载荷吞吐 | 51.2 KB/s = 0.42 Mbit/s | 835.7 KB/s = 6.85 Mbit/s | 51.4 KB/s = 0.42 Mbit/s |
| CTR 丢包（传输层） | 240 | 13224 | 840 |
| tick 丢帧（测量侧） | 60 | 1653 | 210 |
| 实测丢包率 | **1.25%** | **3.09%** | **0.87%** |
| 帧错误（LEN 越界/截断） | 0 | 0 | 0 |
| 时间戳间隔 mean/sd | 1010.1 µs / 100.1 µs | 102.8 µs / 16.7 µs | 1007.0 µs / 83.8 µs |
| 主机到达间隔 sd | 0.438 ms | 0.032 ms | 0.436 ms |
| 命令 RTT min/p50/p99 | 86.0 / 109.9 / 501.0 ms（重发 2 次） | 见日志（重发 28 次） | 见日志（重发 2 次） |
| 板端 tx_drop / daq_dropped / overload | **0 / 0 / 0** | **0 / 0 / 0** | **0 / 0 / 0** |
| 结论 | PASS | PASS | PASS |

![图 34-6 CAN 500k 与 XCPonEth 实测吞吐对比（载荷口径）](figures/34/fig-34-6-eth-throughput.png)

表 34-15 是同一窗口的"双侧对账"，它回答了"丢包到底丢在哪"这个关键问题：

| 计数 | rate 档（6 s） | stress 档（6 s） | 解读 |
|---|---|---|---|
| 板端 `tx_datagrams` | 6253 | 66622 | 板端认为发出去的数据报数 |
| 板端 `tx_msgs` | 24256 | 505101 | 含命令响应与 DAQ 消息 |
| 板端 `daq_frames` | 24004 | 504548 | 协议层交给传输层的 DAQ 帧数 |
| 主机 `datagrams_rx` | 6203 | 58685 | 主机实际收到的数据报数（含控制响应） |
| 主机收到的 DAQ 消息 | 23764 | 466752 | 按 ODT 计数的有效样本帧 |
| 差值来源 | 数据报 6253-6203≈50 个带 DAQ 的包丢失 | 66622-58685≈7937 个包丢失 | UDP 链路无重传，丢在线上/缓冲区 |
| 同窗口 ping 丢包 | 0%（10/10） | 0%（10/10） | **ping 是 84 B 低速率包，不触发小包突发队列压力** |
| 板端发送失败 | `tx_drop=0`、`tx_err=0` | 同左 | 板端 lwIP 层没有"发失败"，丢点在链路/主机收包侧 |

结论要说清楚：**DAQ 丢包不是从站代码丢的**（板端 drop/overload 全 0、daq_frames 与 tick×ODT 对得上），
而是 1 kHz/10 kHz 的小包突发在"板 ENET TX → 千兆车载以太网盒 → 主机 enp6s0"这段链路上丢了 0.9%~3.1%；
同一时刻 ping 0% 丢包，说明不是链路误码，而是突发队列/环形缓冲的容量问题。主机侧已把 `SO_RCVBUF`
调到 1 MiB，仍会丢，说明瓶颈更可能在以太网盒或板端 ENET TX 环（下一节"坑 10"给排查方法）。
XCP 标准本身不提供重传，所以正确做法是：接受丢帧、用 CTR/tick 精确统计，而不是假装 DAQ 无丢包。

标定与命令面在同一轮里也全部复核：CONNECT 返回 `resource=0x05、MAX_CTO=255、MAX_DTO=256、proto=0x14`，
GET_ID 返回 `HPM5E31-XCP-ETH 1.0 (UDP 5555, DAQ)`；SHORT_UPLOAD/DOWNLOAD 读写 g_kp（1.200→1.300→1.200）、
MODIFY_BITS（0x12345678→0x12345A78）、g_pwm_duty_pm 写读回（500→300→500）全部 PASS（日志里的 `[2]` 段）。

### 34.11.7 带宽换算与 1000M 链路单包上限

把账算清楚，才知道"要不要换以太网"：

- 单个 UDP 数据报载荷上限 1472 B；加上 8 B UDP + 20 B IPv4 + 42 B 以太网头/前导/FCS，线上约 1538 B；
- 1000BASE-T1 线速下，1500 B 帧的包速率上限 `1e9 / ((1500+42)×8) ≈ 81.3k pkt/s`，
  对应 UDP 载荷吞吐 `1472×81.3k×8 ≈ 957 Mbit/s`（理论值，实测受 PHY/对端影响）；
- 单包 8 条 ODT 消息时，消息率上限 ≈ `81.3k × 8 = 650k msg/s`；本板实测 77.8k msg/s（约 12%），
  瓶颈是 10 kHz 事件 + 中断打包 + 主机收包，不是链路；
- 本板实测 6.85~6.96 Mbit/s 只占线速 **0.7%**，说明 XCPonEth 的余量非常大：要更多变量/更高采样率，
  优先加 ODT 条目和事件率，而不是担心带宽。

真要把链路跑满，需要把 ODT 载荷做大（接近 256 B 的 MAX_DTO）、减少每包消息数（降低 pps），
这正是表 34-12 的"单包上限"要表达的：**同样的字节数，大 ODT + 少消息比小 ODT + 多消息更省包速率**。

### 34.11.8 复跑命令

~~~bash
#1) 编译（改过 xcp_core.c / xcp_port_udp.c / main.c 后）
cd hpm5e31_1 && bash scripts/build.sh samples/app/xcp_eth

#2) 生成 A2L（地址来自 ELF，必须与烧写镜像一致）
python3 samples/app/xcp_eth/gen_a2l_eth.py --elf build/xcp_eth-debug/output/demo.elf         --vars samples/app/xcp_eth/xcp_eth_vars.json --out samples/app/xcp_eth/xcp_eth.a2l

#3) 上板：一次持锁内完成 烧写 + 串口 + 主机三档测试（rate/stress/long30）
bash scripts/hwrun.sh app/xcp_eth --secs 8 --send "xcp info;xcp stat"      --post "timeout 200 bash host/xcp/xcp_eth_final.sh" --lock-wait 1800

#4) 单跑某一档（板端已在跑 xcp_eth 固件时）
$HOME/pyenv/bin/python host/xcp/xcp_daq.py      --a2l samples/app/xcp_eth/xcp_eth.a2l --interface eth --plan stress --secs 6      --csv out/xcp_eth_stress.csv

#5) CAN 路径回归（确认 XCPonEth 的改动没有影响 CAN）
bash scripts/hwrun.sh app/xcp_can_daq --secs 12 --send "gw on"      --post "timeout 150 bash host/xcp/run_can_regress.sh 5" --lock-wait 1800
~~~

### 34.11.9 本节小结

- XCPonEth 的协议层与 CAN **完全复用**（同一份 `xcp_core.c`），差异只在传输层 330 行：4 B 头、
  多消息拼包、CTR 统计；`MAX_DTO` 用编译期宏从 8 放大到 256，CAN 构型不受影响；
- 单包多 ODT 的收益不在"省头"（每条消息仍要 4 B 头），而在**包速率**：8 条 ODT 共享一个以太网帧的
  42 B 开销，10 kHz 下把包速率从 78k pkt/s 降到 9.7k pkt/s；
- 实测 10 kHz/8 ODT 持续 6 s：77791 msg/s、6.85 Mbit/s 载荷、帧错误 0；30 s 长测稳定在 990 Hz 事件率、
  0.87% 链路丢包，板端 drop 全 0；
- 丢包是 UDP 链路的真实属性，不是实现 bug；主机侧必须用 CTR + 测量变量交叉判定，并对幂等命令重发，
  对 UPLOAD/DOWNLOAD/WRITE_DAQ 这类"会推进 MTA/指针"的命令**不能盲重发**。


## 34.12 参考来源

上游与官方链接（核对日期 2026-11-02）：

- ASAM XCP（MCD-1 XCP）<https://www.asam.net/standards/detail/mcd-1-xcp/>
- ASAM MCD-2 MC / A2L 标准入口 <https://www.asam.net/standards/>（A2L 语法与 ASAP2 兼容性说明）
- Vector XCPlite（XCP on Ethernet 参考实现）<https://github.com/vectorgrp/XCPlite>
- a2lfile / a2ltool（A2L 解析与编辑，Rust）<https://github.com/DanielT/a2lfile>、<https://github.com/DanielT/a2ltool>
- pyxcp 文档 <https://pyxcp.readthedocs.io/>、源码 <https://github.com/christoph2/pyxcp>
- python-can 文档 <https://python-can.readthedocs.io/>
- XCPonEth 传输层细节（4 B 头/4 B 对齐/segment 上限）：Vector XCPlite 的 `src/xcptl_cfg.h`、`src/xcpethtl.c`、`src/xcpethtl.h` <https://github.com/vectorgrp/XCPlite>
- pyxcp 以太网分帧（LEN 语义、多消息切分）：`pyxcp/cpp_ext/framing.hpp`（`prepare_request`/`unpack_header`）、`pyxcp/cpp_ext/eth_framing.hpp`（`EthReceiver::feed_datagram`）、`pyxcp/transport/eth.py`（`header_len=2, header_ctr=2`）<https://github.com/christoph2/pyxcp>
- lwIP 2.1.2 UDP/RAW API <https://www.nongnu.org/lwip/2_1_x/index.html>
- HPM SDK lwIP 例程与 ENET 驱动（`samples/lwip`、`drivers/src/hpm_enet_drv.c`、`hpm_enet_dma_drv.c`）<https://github.com/hpmicro/hpm_sdk>


1. ASAM MCD-1 XCP V1.5（2013）—— 命令码、错误码、DAQ 列表模式位、时间戳模式；
2. ASAM MCD-2 MC（A2L）V1.7.1 —— MEASUREMENT/CHARACTERISTIC/AXIS_PTS/IF_DATA 语法；
3. `docs/chapters/08-canfd.md`、`09-canfd-perf.md` —— MCAN 位时序与吞吐（本章 500k/SP87.5% 配置出处）；
4. `docs/chapters/13-lwip-iperf.md` —— 以太网吞吐（XCP on UDP 的物理层依据）；
5. `components/app_xcp/`、`samples/app/xcp_can_daq/`、`samples/app/xcp_can_cal/`、`host/xcp/`；
6. `tools/gen_a2l.py`、`tools/a2l/*.json`、`samples/app/xcp_can_cal/xcp_can_cal.a2l`；
7. 参考实现：`vectorgrp/XCPlite`（XCP on Ethernet 从站 + A2L 运行时生成）、`DanielT/a2ltool`/`DanielT/a2lfile`（Rust A2L 工具/库）；
8. pyxcp 0.30.1（`$HOME/pyenv`）：`Master` + `create_application_from_config` 的 CAN 传输配置；
9. 实测日志：`logs/20261030-172706-xcp_can_daq.log`（CAN DAQ 自证 PASS）、`logs/20261030-173517-xcp_can_cal.log`（CAN 标定自证，扫参未收敛 1 项）；
10. XCPonEth 实测日志：`logs/20261102-155848-xcp_eth.log`（板端 banner/统计）+ `logs/20261102-155848-host-xcp-eth-rate.log`、`logs/20261102-155848-host-xcp-eth-stress.log`、`logs/20261102-155848-host-xcp-eth-long30.log`（主机 rate/stress/30 s 三档）；
11. CAN 回归日志：`logs/20261102-155714-xcp_can_daq.log` + `logs/20261102-155714-host-xcp-can-regress.log`（`xcp_daq_raw.py` PASS、pyxcp CAN 路径复现 U-35-2）。

## 34.13 待确认项与风险

表中条目已逐条复核（2026-11-02）：能上板测的给了数据和日志路径；本轮确实测不了的降级为已知限制，写明原因和一条可执行的验证方法，不再保留待确认/未尝试的裸条目。


| # | 项 | 现状 | 验证方法 |
|---|---|---|---|
| U-35-1 | XCP on UDP（`samples/app/xcp_eth`） | **已实测**（1 kHz/4 ODT、10 kHz/8 ODT、30 s 长测；标定写读回全 PASS；板端 drop=0，链路丢包 0.87%~3.09%） | 复跑：`bash scripts/hwrun.sh app/xcp_eth --secs 8 --send "xcp info;xcp stat" --post "timeout 200 bash host/xcp/xcp_eth_final.sh" --lock-wait 1800`；单档：`$HOME/pyenv/bin/python host/xcp/xcp_daq.py --a2l samples/app/xcp_eth/xcp_eth.a2l --interface eth --plan stress --secs 6`。详见本章 XCP on UDP 一节、表 34-14/34-15 |
| U-35-2 | **pyxcp 0.30.1 的 CAN 传输层不可用** | **已实测（pyxcp 三个配置均抛 AttributeError）/ 已知限制（pyxcp CAN 传输层不可用，改用自写客户端）** | 上游复现：`pyxcp` ≥0.31 或换 `Eth` 传输再验；本仓库保留两套脚本（`xcp_daq.py` 用 pyxcp、`xcp_daq_raw.py` 用自写客户端） |
| U-35-3 | 扫参"收敛"判据 | **已知限制（1.2 s 窗口 + Ki=2.0 时 10 点均未进入 ±5% 带（峰值 913→850‰ 有单调性））** | 把窗口延到 3 s 或 Ki 降到 0.5 复测；或改判据为"峰值单调 + 稳态误差 < 20%" |
| U-35-4 | 标定页：Flash 页/多段 | 已知限制（未实现，组件里没有 CAL_PAGE 状态与 COPY_CAL_PAGE） | 原因：`components/app_xcp` 目前只做单段单页 RAM 标定，没有页状态机。验证方法：加两页 RAM 数组 + `SET_CAL_PAGE`(0xEB)/`COPY_CAL_PAGE`(0xE4) 后，用 `xcp_can_cal` 或 `xcp_eth` 写 page1、COPY 到 page0、再从 page0 读回比对 |
| U-35-5 | PGM（Flash 编程）命令 | **已知限制（未实现）** | 结合第 21 章 XPI Flash 驱动实现 PROGRAM_START/PROGRAM/VERIFY |
| U-35-6 | CAN FD 下的 DTO 长度 | 已知限制（CAN 构型 DTO 仍 8 B；协议层已支持编译期放大） | 原因：`XCP_MAX_DTO` 已改为 `#ifndef` 可配置（XCPonEth 构型用 256 B），但 CAN 例程仍按 8 B 编译，且未配 MCAN FD 位时序。验证方法：给 `xcp_can_daq` 加 `-DXCP_MAX_DTO=64` + `mcan_config.enable_canfd`，主机用 `xcp_daq_raw.py --fd` 观察 DLC 与样本率提升（理论上限见 图 34-3） |
| U-35-7 | DAQ 过载事件（EV 0x06） | 已知限制（未制造过载） | 原因：EVT0 由 GPTMR 固定 1 kHz，DAQ 只有 2 个列表，1 kHz 下 TX FIFO 不满（历史日志 `overload=0 dropped=0`）。验证方法：把 `xcp_event(0u)` 的节拍改成 4 kHz（GPTMR 周期 250 µs）或加 4 列表×4 ODT，用 `xcp_can_daq` 的 `stat` 看 `overload` 与 `dropped` 是否非 0，主机侧确认收到 EV 0x06 包 |
| U-35-8 | 时间戳抖动"真实值" | 已知限制（板端 1 µs 分辨率下 sd=0，无法测更细） | 原因：时间戳由 GPTMR 1 µs 计数，触发抖动小于分辨率，统计出来恒为 0。验证方法：主机侧用 `candump -ta` 或 `xcp_daq_raw.py` 的到达时刻做分布（会叠加 cannelloni 队列，实测往返 2.26 ms/p99 2.49 ms），或用示波器看 GPTMR 触发脚与 CAN TX 的相位差 |
| U-35-9 | UDP 链路丢包定位（1.25%/3.09%/0.87%） | 已定位到链路/突发队列，非固件丢帧（板端 `tx_drop=0`、`daq_dropped=0`、`overload=0`；同窗口 ping 0% 丢包） | 下一步：①抓 `ethtool -S enp6s0` 的 rx_dropped/rx_missed；②在板端 ENET TX 环加"待发描述符数"统计；③换一块以太网盒/主机网卡复测。XCP 标准无重传，丢帧属 UDP 语义，不作为缺陷 |
| U-35-10 | 用 pyxcp 自带 Eth 传输直接对接本从站 | **已知限制（未验证：本轮用自写 socket 客户端，因 pyxcp CAN 传输层有 U-35-2 的 bug）** | 验证方法：`pyxcp` 的 `Eth` transport 配 `host=192.168.2.100, port=5555, protocol=UDP`，跑 GET_STATUS/DAQ；若与表 34-11 的 LEN/对齐语义不一致，优先用抓包（`tcpdump -i enp6s0 -w`）对照本实现 |
