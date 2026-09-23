# tc397_hss — TC397XX + 双路 WSTD6020AN 高边开关 (P40 监测 + IS 电流检测)

> **关键结论（2026-09-16 实测）：P40.0~P40.7 在 TC397 LFBGA292 上是
> INPUT-ONLY 引脚（无数字输出驱动），因此 HSS 的 IN/DEN/DSEL 无法由 P40
> 驱动，开关动作暂不可测（详见 §1.2）。本工程保留 `adc` 基线 +
> `hss` 监测命令（板级电平 + IS 关断态零点表征）与完整诊断工具
> （`regs/dbg`），待硬件改线后直接复用。**

本工程由 `tc397_adc` 拷贝而来（基线 commit `b63bea7`，仅改名无功能变化），
在 **EVADC AN0~AN47 监测 + `adc`/`adcdbg` 命令** 之外新增 **双片稳先微
WSTD6020AN 高边开关 `hss` Shell 命令**。
其余（UART0 921600、P13.0 LED、心跳、`mcu/temp/sysinfo` 等命令）与 adc 基线一致。

* 工具链/下载：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ TAS/DAS 8.3.0 + `aurix_flasher`
  （复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`，TC3xx 通用）
* 串口：`/dev/ttyACM0`（1a86:55d3，921600-8N1，ASCLIN0 P14.0 TX / P14.1 RX），
  DAP MiniWiggler `058b:0043` 仅用于 TAS 下载
* 电源域：**VDDM / VREF 均接 TLF35584 输出的 VREF（5V），ANx 电源域 5V**，
  故 ADC 满量程 5V，`Vpin = raw × 5.0 / 4096`（12bit）。P40 为 5V 域输入脚。

---

## 1 原理说明

### 1.1 WSTD6020AN 主要特性（数据手册 `ref/WSTD6020ANDatasheet_A1_EN-*.pdf`）

> 数据手册 17 页，已用 `pdftotext -layout` 抽取为同目录 `.txt`
>（`ref/` 不进 git，见根 `.gitignore`；本地查阅即可）。
> 下列为与本板相关的要点摘录，页码指 PDF 页：

* 双通道智能高边开关，DFN9×6-14L，AEC-Q100，`RON=18mΩ/ch (Tj=25℃)`，
  单通道标称 9A、双通道同时 7A/ch，限流约 40A，工作电压 8~36V（本板 VBAT=12V）。
* 逻辑输入 3.3V/5V CMOS 兼容：`VLOW≤0.9V`、`VHIGH≥2.1V`、迟滞约 0.2V；
  上限 `VIN/VDEN/VDSEL ≤6.0V`。
* 引脚（Top view，详见手册 p3）：
  `GND(1)`、`IN0/IN1(2/6)`、`DEN(3)`、`IS(4)`、`DSEL(5)`、
  `OUT1(8/9/10)`、`OUT0(12/13/14)`、`VS(EPAD)`。
* `IN0/1`：电压控制 + 迟滞，控制对应 OUT 通断（高=开）。
* `DEN`：高有效，使能 IS 诊断脚；低=IS 高阻（Hi-Z）。
* `DSEL`：高有效，IS 多路选择；低=ch0、高=ch1（`DEN=H` 时有效）。
* `IS`：多路模拟电流输出，与选中通道的负载电流成比例 `IIS = IOUT/K`；
  板上 1K 下拉到地做 I-V 变换：`Vis = IIS × 1K`。
  手册建议 RIS=100kΩ+CEXT=10nF，本板用 1K（小电流时 Vis 较小，见 §1.4）。
* 真值表（手册 p15 Table 2/3 摘录）：
  `IN=L,DEN=L → OUT=L,IS=0`（standby）；
  `IN=L,DEN=H → OUT=L,IS=0`；
  `IN=H,DEN=H,正常 → OUT=H,IIS=IOUT/K`；
  `过载/过温锁存 → IIS=IISH(10~30mA)`；
  `DEN=L → IS=Hi-Z`（与 IN 无关）。
* 电流检测比（手册 p9，`VDEN=5V`）：

  | Iout | K typ | 精度 |
  | --- | --- | --- |
  | 50mA | K0=1560 | ±15% |
  | 0.5A | K1=2500 | ±5% |
  | 2A/4A/7A | K2/K3/K4=2630 | ±3% |

  温度漂移约 ±2%。`K` 非线性，小电流用 K0、大电流用 2630；
  固件同时打印三种 K 的换算结果以避免选错（见 §1.4）。
* 诊断时序（手册 p10）：`DEN↑→有效 ≤100us (tDSENSE1H)`；
  `IN↑→有效 80~250us (tDSENSE2H)`；固件 `diag` 统一等待 3ms 裕量。
* 保护：欠压关断、过压钳位（~64V）、限流、快热瞬态自限、丢地/丢 VS 保护、
  过温锁存关断（~175℃，迟滞 20℃）；开态开负载/对 VS 短路/过载对地短路可由 IS 区分。
* 未用脚处理（手册 p4 Table 1）：IS 不允许悬空（本板经 1K 到地）；
  OUT 不允许对地；IN/DEN/DSEL 不用时经 15K 到地。
* Bypass 模式：8 个特定 DEN 脉冲 + IN 脉冲进入（每通道 100mA），`DEN>5ms` 退出；
  本工程不使用。

### 1.2 板级连接、P40 引脚映射与 input-only 根因

| HSS 信号 | TC397 引脚 | 复用 AN | 板级期望 | 硅片实际 |
| --- | --- | --- | --- | --- |
| HSS0_IN0 | P40.0 | AN24 | OUT（高=开） | **仅输入，无输出驱动** |
| HSS0_DEN | P40.1 | AN25 | OUT | **仅输入** |
| HSS0_DSEL | P40.2 | AN26 | OUT | **仅输入** |
| HSS0_IN1 | P40.3 | AN27 | OUT | **仅输入** |
| HSS1_IN0 | P40.4 | AN32 | OUT | **仅输入** |
| HSS1_DEN | P40.5 | AN33 | OUT | **仅输入** |
| HSS1_DSEL | P40.6 | AN36 | OUT | **仅输入** |
| HSS1_IN1 | P40.7 | AN37 | OUT | **仅输入** |
| HSS0_IS | — | AN14 | ADC | 正常（1K→GND，`Vis=IIS×1K`） |
| HSS1_IS | — | AN15 | ADC | 正常 |

三重证据（2026-09-16）：

1. **Datasheet 引脚表**（`Infineon-TC39x-DataSheet-v01_02-EN.txt` §2，
   如 `AD7 AN24/P40.0 I S/HighZ`）：8 个 P40 脚类型均为 `I`（输入），
   缓冲 `S/HighZ`，复用功能全是输入（SENT 接收/EVADC/CCU 捕获/EDSADC），
   **无 `O` 输出行**；对照 P13.0/P14.0 均有 `I` + `O0` 两行（双向 GPIO）。
2. **User Manual Part-1 §14**（Port 章 Emergency Stop 例外）：
   `Not available for P40.x and P41.x (analog input ANx overlayed with GPI)`——
   明确 P40 为模拟输入 + 通用**输入**。
3. **板上实测**（`hss regs/dbg`，日志 `temp/hss_dbg_settled.log`）：
   IOCR 写输出模式、OUT 锁存置位（`OUT=0x01`）均成功，PDISC 模拟已关，
   但引脚纹丝不动（`IN=0`，AN24=G3CH0 读 `0.012V raw=10`，高低锁存同值）；
   而内部上拉/下拉/无拉均正常（settled：nopull 0 / pullup 1 / pulldown 0），
   证明输入通路完好、外部下拉弱、无硬短路——**强上拉驱动不存在**。

结论：任务书把 P40 当 GPIO 输出是与硅片矛盾的（LFBGA292 下 P40/P41 整组
皆为 analog+GPI）。HSS 开关动作**必须改硬件**（如从真 GPO 飞线到
IN/DEN/DSEL，或改版）；改完后本工程 `App/hss.c` 的引脚表换 port/pin 即可复用。

### 1.3 负载与预期电流（1K 对地，VBAT=12V，改线后用）

每路 `HS_OUTx` 与 GND 之间接 1K 电阻（x=0..3，共 4 路）：

```
Iout ≈ VBAT / 1K = 12V / 1K ≈ 12mA（RON 压降约 0.2mV 可忽略）
Iis(K0) = 12mA/1560 ≈ 7.7uA → Vis ≈ 7.7mV；LSB = 5V/4096 ≈ 1.22mV
```

### 1.4 IS 读数换算（两种布线假设同时打印，改线后用）

`adc` 对 G1 通道按 47K+3K 打印 `pin` 与 `ext=pin×50/3`。
IS 链路到底是“经分压进 ADC”还是“直进 ADC”，固件 `hss` 对每片 HSS
同时打印两种假设（`Hss_PrintSense`）：

* A) `ext = Vis`（IS 节点经 47K+3K 到 ADC）：`Iis = Vext/1K`；
* B) `pin = Vis`（IS 节点直连 ADC）：`Iis = Vpin/1K`；

再分别以 K0/K1/K2 求 `Iout = K × Iis`。改线点亮后，看哪列回到约 12mA
即定布线（注意 K0 ±15% + 电阻容差 + 1LSB 量化，10~14mA 均算通过）。

### 1.5 固件实现（`App/hss.{h,c}` + `Shell/shell_hss.c` + `App/adc.c` 扩展）

* `Hss_Init()`（`Cpu0_Main.c` 在 `Adc_Init()` 之后调用）：
  P40.0~7 配 **`inputNoPullDevice`**（板级下拉决定电平，实测 0V），
  不配输出（无驱动器）；启动日志 `HSS init (P40 inputs...)` /
  `HSS ready (P40 input-only, ...)`。
* `Hss_SetIn/GetIn/SetDen/GetDsel/SelectDiag/AllOff`：iLLD 薄封装；
  Set 类写 OMR 锁存（对 P40 无 ball 效果）+ 调用方如实打印
  `input-only` 与回读；Get 类返回真实板级电平（改线后可直接看到外部驱动）。
* `Hss_DelayMs()` 轮询 `g_TickCount_1ms`（STM 1ms tick）。
* `Shell/shell_hss.c` 的 `hss` 命令：
  `hss`/`status` 状态（含 input-only 提示 + GPIO 回读 + AN14/15 + 双假设 Iout）；
  `on/off/den/dsel/diag/offall` 保留（改线前仅动锁存 + 回读，用于验证）；
  `regs` 转储 P40 `OUT/IN/IOCR0/IOCR4/OMR/PDISC/PDR0/PDR1`；
  `dbg [pin]` 对指定 P40 脚做 settled 电气探测（nopull/pullup/pulldown/
  outHigh/outLow 各稳 5~10ms + IN 回读，pin0 另读 AN24 ball 电压）。
* `Adc_ReadAn24()`（`App/adc.{h,c}`）：P40.0 ball（板 AN24）经 EVADC
  G3CH0/RES0 采样（G3 已初始化，RES0 空闲，首次调用时懒加入 queue0/refill），
  为 `dbg` 提供独立于数字 IN 寄存器的 ball 电压证据。
* 采集沿用 adc 基线：EVADC 5 组 queue0/refill 后台扫描，无新增中断。
  `CMakeLists.txt` 无需改动（`App/`/`Shell/` 源码系自动收集）。

---

## 2 目录结构（相对 adc 基线的新增/修改）

```
tc397_hss/
├── App/hss.h / hss.c          # P40 映射表 + 输入初始化 + IN/DEN/DSEL 薄封装 + 延时 + K 换算
├── App/adc.h / adc.c          # + Adc_ReadAn24()（P40.0 ball 经 G3CH0 采样，调试用）
├── Shell/shell_hss.c          # hss 命令（状态/请求/regs/dbg 探测/双假设电流打印）
├── Cpu0_Main.c                # + Hss_Init()（Adc_Init 之后）+ 'hss' 提示行
├── README.md                  # 本文件（重写）
└── build/gcc/tc397_hss.{elf,hex,map}
```

`ref/WSTD6020ANDatasheet_A1_EN-*.txt` 为本地 `pdftotext -layout` 抽取，
`ref/` 不进 git（根 `.gitignore`），与其它手册 txt 惯例一致。

---

## 3 构建与下载（Ubuntu 26.04）

```bash
export PATH=/opt/tricore-gcc/bin:$PATH

cd tc397_hss
./build.sh build                        # Debug（text ~98K）
./build.sh build --build-type Release
./build.sh download                     # 需 TAS: systemctl status tas-server
./build.sh reset                        # 经 flasher -read 触发复位并运行
```

TAS 状态（本 bench 常驻，无需操作，仅备忘）：
`systemctl status tas-server`（`ss -tlnp | grep 24817`），
验证 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher -id list`
应识别 `TC39x ... TriBoard TC2XX V2.0`。
DAP MiniWiggler `058b:0043`：`lsusb` + `/dev/serial/by-id/*IFX*`（FTDI ttyUSB0）。

---

## 4 测试方法与步骤

### 4.1 基线（改线前可做，已做）

1. `./build.sh download` 烧录（自动复位运行），或 `./build.sh reset` 复位。
2. 打开串口（先确认端口空闲）：
   `python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw`。
3. 等启动日志 `ADC ready` + `HSS ready (P40 input-only...)` + `letter:/$`。
4. `help`（应有 `adc/adcdbg/hss`）→ `hss`（input-only 提示 + 8 脚全 0 +
   AN14/15≈0V）→ `adc 14`/`adc 15`（关断态零点）→ `hss regs`
   （IOCR=0 输入，PDISC 低 8 位 0）→ `hss dbg 0` / `hss dbg 4`
   （settled 上下拉正常、outHigh 不动、AN24≈0.012V）。
5. 判据：AN14/15 raw 0~1（1LSB 噪声）；AN24 高低锁存同值（~10 raw）；
   上拉稳态读 1（排除硬短路）。

### 4.2 点亮（改线后做）

1. 硬件：将真 GPO（如 P13/P14/P15 空闲脚，需查板）飞线到
   HSS0/1 的 IN0/DEN/DSEL（至少先通一路），共地确认，VBAT=12V 确认
   （`adc 10` ext≈12V）。
2. 把 `App/hss.c` 引脚表换到新 port/pin，`Hss_Init` 改回推挽输出全低，
   重编烧录。
3. `hss den 0 1` → `hss dsel 0 0` → `hss on 0 0` → `hss`
  （回读应为 1，AN14 上升）；`hss diag` 全遍历，看哪列 `Iout(K0)≈10~14mA`
   以定 §1.4 布线假设；万用表量 OUT≈12V/0V、IS 节点 Vis 交叉验证。
4. 测完 `hss offall`。

---

## 5 测试结果（2026-09-16，Debug，板上即测版 text 97724）

完整日志：`temp/hss_test.log`（首版功能）、`temp/hss_test2.log`（对照）、
`temp/hss_stat.log`（20× 分布）、`temp/hss_test3.log`（regs 定位）、
`temp/hss_dbg_an24.log`、`temp/hss_dbg_settled.log`（settled 探测）、
`temp/hss_final.log`（input-only 定稿版零点）。

* 编译通过；多次 `./build.sh download` Pass（中间两次因 bench USB 整体掉线
  Fail，见 §6）。
* `hss`（定稿版）：input-only 提示 + HSS0/1 全 0，AN14 raw=0、AN15 raw=0~1；
  `adc 14/15` 同步为 0；`regs` 示 IOCR=0（输入）、PDISC 低 8 位 0。
* 20× 统计：off 与 on 请求下 AN14 均值同为 0.4（`hss_stat.log`），
  AN15 为 0.55 vs 0.3——纯噪声，IS 无位移，与“引脚未动”一致。
* `dbg 0/4`（settled）：nopull 0 / pullup 1 / pulldown 0（输入正常），
  outHigh+speed4 仍 0 且 AN24=`0.012V raw=10`，outLow 同值——ball 未动。
* 结论：**驱动/命令/采集/诊断链全部正常，卡在 P40 无输出驱动（§1.2），
  开关动作与 IS 带载检测必须改线后重测**；关断态零点已表征（raw 0~1）。

---

## 6 注意事项 / 已知问题

1. **P40 input-only（本次最大发现）**：P40.x/P41.x 无数字输出，
   任务书的“P40 驱动 HSS”需改硬件；`hss on/off/den/dsel` 改线前只动锁存，
   看 `rb=` 回读与万用表，不要误判为已驱动。
2. **P40 与 AN 复用**：P40.0~7 即 AN24~27/32~33/36~37（同一 ball）；
   `adc` 对这 15 行（含这 8 个）显示 Reserved；`dbg` 的 AN24 读数是 ball
   真值，不受 IN 寄存器影响。
3. **关断态零点**：DEN=0（板下拉）时 IS 高阻，AN14/15 raw 0~1 正常；
   噪声 ±1LSB（5V/4096≈1.22mV pin；按 ext 假设 1LSB≈31mA Iout(K0)，
   改线后看 K0 列、10~14mA 通过即可）。
4. **Bypass 模式勿碰**：8 脉冲 DEN 序列可误入 bypass；正常单次电平操作无碍。
5. **USB bench 掉线两次**：10:31 hub `disabled by hub (EMI?)` 丢 DAP；
   10:54 串口(1-8)+DAP(1-9)相继掉线、重枚举 `-71` 错误；均为重插恢复。
   若复现：重插 DAP/串口端 USB，重跑 `-id list`，必要时换口/换线。
6. **串口占用**：沿用 adc 基线教训，先确认 `/dev/ttyACM0` 空闲。
7. 提交未推送（按任务要求暂不推送）；`temp/` 日志与 `handover/` 不进 git。

---

## 7 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* 其余移植代码内部许可
