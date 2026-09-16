# tc397_hss — TC397XX + 双路 WSTD6020AN 高边开关 (P40 控制 + IS 电流检测)

本工程由 `tc397_adc` 拷贝而来（基线 commit `b63bea7`，仅改名无功能变化），
在 **EVADC AN0~AN47 监测 + `adc`/`adcdbg` 命令** 之外新增 **双片稳先微
WSTD6020AN 高边开关驱动 + `hss` Shell 命令**。
其余（UART0 921600、P13.0 LED、心跳、`mcu/temp/sysinfo` 等命令）与 adc 基线一致。

* 工具链/下载：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ TAS/DAS 8.3.0 + `aurix_flasher`
  （复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`，TC3xx 通用）
* 串口：`/dev/ttyACM0`（1a86:55d3，921600-8N1，ASCLIN0 P14.0 TX / P14.1 RX），
  DAP MiniWiggler `058b:0043` 仅用于 TAS 下载
* 电源域：**VDDM / VREF 均接 TLF35584 输出的 VREF（5V），ANx 电源域 5V**，
  故 ADC 满量程 5V，`Vpin = raw × 5.0 / 4096`（12bit）。P40 为 5V 域 GPIO。

---

## 1 原理说明

### 1.1 WSTD6020AN 主要特性（数据手册 `ref/WSTD6020ANDatasheet_A1_EN-*.pdf`）

> 数据手册 17 页，已用 `pdftotext -layout` 抽取为同目录 `.txt`
>（`ref/` 不进 git，见根 `.gitignore`；本地查阅即可）。
> 下列为与本板相关的要点摘录，页码指 PDF 页：

* 双通道智能高边开关，DFN9×6-14L，AEC-Q100，`RON=18mΩ/ch (Tj=25℃)`，
  单通道标称 9A、双通道同时 7A/ch，限流约 40A，工作电压 8~36V（本板 VBAT=12V）。
* 逻辑输入 3.3V/5V CMOS 兼容：`VLOW≤0.9V`、`VHIGH≥2.1V`、迟滞约 0.2V；
  P40 高电平约 5V 在允许的 `VIN/V DEN/V DSEL ≤6.0V` 范围内。
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
  OUT 不允许对地；IN/DEN/DSEL 不用时经 15K 到地（本板均由 P40 驱动，无悬空）。
* Bypass 模式：8 个特定 DEN 脉冲 + IN 脉冲进入（每通道 100mA），`DEN>5ms` 退出；
  本工程不使用（保持常规开关 + 诊断模式）。

### 1.2 板级连接与 P40 引脚映射

| HSS 信号 | TC397 引脚 | 复用 AN | 方向 | 说明 |
| --- | --- | --- | --- | --- |
| HSS0_IN0 | P40.0 | AN24 | OUT | ch0 开关，高=开 |
| HSS0_DEN | P40.1 | AN25 | OUT | 高=IS 使能 |
| HSS0_DSEL | P40.2 | AN26 | OUT | 低=ch0/高=ch1 |
| HSS0_IN1 | P40.3 | AN27 | OUT | ch1 开关 |
| HSS1_IN0 | P40.4 | AN32 | OUT | ch0 开关 |
| HSS1_DEN | P40.5 | AN33 | OUT | 高=IS 使能 |
| HSS1_DSEL | P40.6 | AN36 | OUT | 低=ch0/高=ch1 |
| HSS1_IN1 | P40.7 | AN37 | OUT | ch1 开关 |
| HSS0_IS | — | AN14 | ADC | 经板上 1K→GND，`Vis=IIS×1K` |
| HSS1_IS | — | AN15 | ADC | 同上 |

注意：P40.0~7 与 AN24~27/32~33/36~37 是**同一物理引脚复用**。
本工程将 P40.0~7 配为 GPIO 推挽输出，因此对应的 8 个 AN 在 `adc` 表中
本就显示 `Reserved (P40.x GPIO, not sampled)`，无采样冲突。
`Hss_Init()` 上电即把 8 脚全部拉低（OUT 关、DEN 关→IS 高阻），为安全状态。

### 1.3 负载与预期电流（1K 对地，VBAT=12V）

每路 `HS_OUTx` 与 GND 之间接 1K 电阻（x=0..3，共 4 路）：

```
Iout ≈ VBAT / 1K = 12V / 1K ≈ 12mA（RON 压降约 0.2mV 可忽略）
```

### 1.4 IS 读数换算（两种布线假设同时打印）

`adc` 对 G1 通道按 47K+3K 打印 `pin` 与 `ext=pin×50/3`。
IS 链路到底是“经分压进 ADC”还是“直进 ADC”，仅凭原理图文字不能完全确定，
固件 `hss` 状态/诊断对每片 HSS 同时打印两种假设（`Hss_PrintSense`）：

* A) `ext = Vis`（IS 节点经 47K+3K 到 ADC）：`Iis = Vext/1K`；
* B) `pin = Vis`（IS 节点直连 ADC）：`Iis = Vpin/1K`；

再分别以 K0/K1/K2 求 `Iout = K × Iis`。以 1K 负载 12mA 为判据：

```
Iis(K0) = 12mA/1560 ≈ 7.7uA → Vis ≈ 7.7mV
LSB = 5V/4096 ≈ 1.22mV → 假设B约6 LSB可见；假设A的 pin=Vis×3/50≈0.46mV（<1 LSB，读0）
```

因此：开某一通道 + DEN=1 + DSEL 对应后，若某假设的 `Iout(K0)` 回到约 12mA，
该假设即为板上真实布线；`DEN=0` 时 IS 高阻，两种假设都应回 0V 附近。
`hss diag` 对每通道自动做 `IN=1 + DEN=1/DSEL=ch + 3ms` 再读数（覆盖手册时序）。

### 1.5 固件实现（`App/hss.{h,c}` + `Shell/shell_hss.c`）

* `Hss_Init()`（`Cpu0_Main.c` 在 `Adc_Init()` 之后调用）：
  P40.0~7 配 `outputPushPullGeneral` 并全部拉低；启动日志打印
  `HSS init ...` / `HSS ready (safe: IN/DEN low)`。
* `Hss_SetIn/GetIn/SetDen/GetDen/SetDsel/GetDsel/SelectDiag/AllOff`
  均为 `IfxPort_setPinHigh/Low/getPinState` 薄封装，参数校验（dev 0~1，ch 0~1）。
* `Hss_DelayMs()` 轮询 `g_TickCount_1ms`（STM 1ms tick），供 `diag` 等 3ms。
* `Shell/shell_hss.c` 的 `hss` 命令（见 `Hss_PrintUsage`）：
  `hss`/`hss status` 状态（含 GPIO + AN14/15 + 双假设 Iout）；
  `hss on/off <dev> <ch>` 只动 IN；`hss den/dsel <dev> <0|1>` 只动诊断位；
  `hss diag [dev [ch]]` 自动开 IN + 选通 + 延时 + 读数（IN/DEN 保持，关请用
  `hss off` / `hss offall`）；`hss offall` 回安全状态。
* 采集沿用 adc 基线：EVADC 5 组 queue0/refill 后台扫描，`Adc_ReadAn(14/15)`
  读最新结果（3 轮重试），无需新增中断。`CMakeLists.txt` 无需改动
  （`App/`/`Shell/` 源码系自动收集）。

---

## 2 目录结构（相对 adc 基线的新增/修改）

```
tc397_hss/
├── App/hss.h / hss.c          # P40 GPIO 驱动：映射表 + 初始化 + IN/DEN/DSEL + 延时 + K 换算
├── Shell/shell_hss.c          # hss 命令（状态/开关/诊断/双假设电流打印）
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
./build.sh build                        # Debug（text ~96K，hex ~280K）
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

1. `./build.sh download` 烧录（自动复位运行），或 `./build.sh reset` 复位。
2. 打开串口（注意：串口被其他程序占用时会无输出，先确认端口空闲）：
   `python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw`。
3. 等启动日志出现 `ADC ready` + `HSS ready (safe: IN/DEN low)` + `letter:/$`。
4. 基线检查：`help`（应有 `adc/adcdbg/hss`）→ `adc 14`、`adc 15`
  （安全态 DEN=0，期望 pin≈0V）；→ `hss`（8 脚全 0，IS≈0V）。
5. 单通道点亮（以 HSS0_CH0 为例）：
   `hss den 0 1` → `hss dsel 0 0` → `hss on 0 0` → 等 100ms → `hss`
   判据：AN14 上升，A/B 其中一种假设 `Iout(K0)≈12mA`（见 §1.4），
   另一种接近 0/偏小；`adc 14` 同步变化。
6. 全遍历：`hss diag`（4 通道各开 + 选通 + 3ms + 读数，IN/DEN 保持开）→
   记录 4 组 `pin/ext/raw` 与双假设电流；→ `hss offall` → `hss`
   确认回 0V（IS 高阻）。
7. 异常时用 `adcdbg` 看 G1 queue/RES14~15 的 VF/RESULT 是否更新；
   用万用表量 HS_OUTx 对地（开时≈VBAT 12V，关时≈0V）与 IS 节点 Vis 交叉验证。

---

## 5 测试结果

### 5.1 编译（2026-09-16，Debug，通过）

```
[295/306] Building C object .../Shell/shell_hss.c.obj
[304/306] Linking C executable tc397_hss.elf
[305/306] Generating tc397_hss.hex
text 95968 data 2184 bss 77096（adc 基线 text 92312，增量约 3.6K）
```

### 5.2 烧录/板测（本次未完成——DAP 掉线，待复测）

* TAS 服务正常（`tas-server.service active`，24817 监听）。
* 10:29 `aurix_flasher -id list` 曾识别
  `Target 0: TC39x WLH978 (TriBoard TC2XX V2.0 TB5SL2XK)`（Bus 001 Device 091）。
* 10:31 `./build.sh download` 报 `No Target Connected`；
  `journalctl` 显示 `usb usb1-port9: disabled by hub (EMI?), re-enabling...`
  后 `USB disconnect, device number 91`，`/dev/serial/by-id` 下 IFX 设备消失，
  仅剩串口 `1a86:55d3`。`1-9` 节点已无，软件无法恢复，需**重插 DAP 端 USB**。
* 串口抽查（板上仍为旧 adc 固件）：`help` 有 `adc/adcdbg`、无 `hss`，
  证明新固件尚未上板；待 DAP 恢复后按 §4 重测并把 `hss`/`hss diag` 实录补入本节。

### 5.3 待补判据（DAP 恢复后）

* `hss` 安全态：8 脚全 0，AN14/15 pin≈0V（raw 0~2）。
* `hss diag`：每通道其中一种假设 `Iout(K0)` ≈ 10~14mA（1K@12V），
  由此确定 §1.4 的真实布线假设并在 README 定稿。
* `hss offall` 后回零；万用表 OUT 开/关≈12V/0V。

---

## 6 注意事项 / 已知问题

1. **P40 与 AN 复用**：P40.0~7 配输出后 AN24~27/32~33/36~37 的 ADC 值无意义，
   `adc` 对这 15 行（含这 8 个）显示 Reserved，不要误判为故障。
2. **安全态**：上电 `Hss_Init` 全拉低；`diag` 会保持 IN/DEN 开，
   测完务必 `hss offall`（1K 负载常开仅 0.14W，但避免长期带电混淆后人）。
3. **小电流精度**：12mA 远 below K2/K3/K4 的 2~7A 标称点，务必看 K0 列；
   K0 本身 ±15%，加上 1K/分压电阻容差，10~14mA 均算通过，不要按 12.00mA 卡。
4. **IS 高阻**：`DEN=0` 时 IS 悬空（仅 1K 到地），读≈0V 正常；
   若 DEN=0 仍有明显电压，先查 P40.1/5 是否被误驱动。
5. **Bypass 模式勿碰**：8 脉冲 DEN 序列可能误入 bypass（100mA/通道）；
   正常 `den/dsel` 单次电平操作不会触发，勿手动在 DEN 上打脉冲串。
6. **DAP 掉线**：本次遇到 hub `disabled by hub (EMI?)` 导致 USB 断开；
   若复现，先重插 DAP 端 USB，重跑 `-id list`，必要时换 USB 口/线并远离干扰源。
7. **串口占用**：沿用 adc 基线教训，先确认 `/dev/ttyACM0` 空闲再开 miniterm。
8. 提交未推送（按任务要求暂不推送）；`temp/` 日志与 `handover/` 不进 git。

---

## 7 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* 其余移植代码内部许可
