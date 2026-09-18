# tc397_adc — TC397XX (292pin) ASCLIN0 Letter-Shell + EVADC AN0~AN47 监测

本工程由 `tc397_uart_lettershell` 拷贝而来（基线 commit `ef45728`，仅改名无功能变化），
新增 **EVADC 多通道后台扫描 + `adc` Shell 命令**，按 AN0~AN47 顺序打印每路电压。
其余（UART0 921600、P13.0 LED、心跳、`mcu/temp/sysinfo` 等命令）与 uart 基线一致。

* 工具链/下载：tricore-gcc 13.4.1（`/opt/tricore-gcc`）+ TAS/DAS 8.3.0 + `aurix_flasher`
  （复用 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher`，TC3xx 通用）
* 串口：`/dev/ttyACM0`（1a86:55d3，921600-8N1，ASCLIN0 P14.0 TX / P14.1 RX），
  DAP MiniWiggler `058b:0043` 仅用于 TAS 下载
* 电源域：**VDDM / VREF 均接 TLF35584 输出的 VREF（5V），ANx 电源域 5V**，
  故 ADC 满量程 5V，`Vpin = raw × 5.0 / 4096`（12bit）

---

## 1 原理说明

### 1.1 EVADC 分组与 AN 通道映射（LFBGA292）

依据 `Libraries/iLLD/.../_PinMap/TC39xB/IfxEvadc_PinMap_TC39xB_LFBGA292.h`，本板用到的
AN 通道分属 5 个 EVADC 分组（每组内结果寄存器号 = 通道号，无冲突）：

| 分组 | 通道 | AN | 说明 |
| --- | --- | --- | --- |
| G0 | CH0~CH7 | AN0~AN7 | 预留，打印引脚电平 |
| G1 | CH0~CH7 | AN8~AN15 | 47K+3K 分压信号（见 §1.2） |
| G2 | CH0 | AN16 | VUC（TLF35584 QUC，直连） |
| G2 | CH4~CH7 | AN20~AN23 | 3V3 / 1V25 / 0V9 / HW_VERSION（直连，见 §1.3） |
| G3 | CH6~CH7 | AN30~AN31 | 预留，打印引脚电平 |
| G8 | CH2~CH3 | AN34~AN35 | SPARE / T1S_INH（直连） |
| G8 | CH8~CH15 | AN40~AN47 | EXTADC0~7（47K+3K 分压） |

以下 AN 在本工程中复用为 **P40.x GPIO，不采样**，`adc` 显示 `Reserved`：
**AN17, 18, 19, 24, 25, 26, 27, 28, 29, 32, 33, 36, 37, 38, 39**。

### 1.2 47K+3K 分压通道（外部电压 = 引脚电压 × 50/3）

分压比 `3/(47+3) = 3/50`（外部 50V → 引脚 3V），固件同时打印 `pin` 与 `ext`：

| AN | 信号 | 来源 |
| --- | --- | --- |
| AN8 | VPREREG | TLF35584 Buck 输出 |
| AN9 | VS1 | TLF35584 Boost 输出 |
| AN10 | VBAT | 外部输入电源 |
| AN11 | ETH_INH | YT8011AN 的 INH |
| AN12 | CAN0_INH | TCAN1043 的 INH |
| AN13 | IG | 点火 |
| AN14 | HSS0 | 高边开关电流检测 |
| AN15 | HSS1 | 高边开关电流检测 |
| AN40~AN47 | EXTADC0~7 | 外部 ADC 输入 0~7 |

### 1.3 直连通道

| AN | 信号 | 说明 |
| --- | --- | --- |
| AN16 | VUC | TLF35584 的 QUC，直连 |
| AN20 | 3V3 | 额外的 3V3 电源 |
| AN21 | 1V25 | MCU Core 电压 |
| AN22 | 0V9 | YT8011AN 的 0.9V |
| AN23 | HW_VERSION | VUC 经 10K+1K 分压（硬件版本 1.0），固件另打印 `vuc ~= pin×11` |
| AN35 | T1S_INH | LAN8651 10BASE-T1S 芯片的 INH |

### 1.4 预留通道

AN0~AN7、AN30、AN31、AN34 为预留，ADC 功能直接支持，仅打印引脚电平（命名 `SPARE`）。

### 1.5 固件实现（`App/adc.c` + `Shell/shell_adc.c`）

* `Adc_Init()`（`Cpu0_Main.c` 在启动打印之后调用，见 §6）：
  `IfxEvadc_Adc_initModule` 使能 EVADC → 配 5 个独立 master 分组
  （queue0 使能、门控 `always`）→ 最后一组（G8）置 `startupCalibration=TRUE`
  做上电校准 → 每通道结果寄存器 = 通道号 → 全部以 `REFILL` 加入 queue0 →
  `startQueue`，之后各组 **Free-Running 后台循环扫描**，Shell 只读最新结果。
* `Adc_ReadAn(an, &raw, &volt)`：轮询结果寄存器 VF 标志（3 轮 × 20 万次重试，
  覆盖刚启动的转换空窗），`Vpin = raw×5.0/4096`。
* `Shell/shell_adc.c` 的 `adc` 命令：`adc` 打印 AN0~AN47 全表，
  `adc <n>` 打印单通道（0~47）。P40 复用行显示 Reserved。
* `Shell/shell_adcdbg.c` 的 `adcdbg` 命令：转储 G0/G1 的 queue 状态
  （QSR/Q0R/QMR0/CHCTR3/VFR）与 RES0~7 的 VF/RESULT，以及 G2/G3/G8 的 QSR，
  用于定位采样异常（见 §6）。
* `CMakeLists.txt`：`.cproject` 默认排除了 Evadc 目录（与 Dts 同理），故显式追加
  `Evadc/Std/IfxEvadc.c` + `Evadc/Adc/IfxEvadc_Adc.c`；另按 sdmmc 项目的血泪教训
  去掉了 `-fdata-sections`（tricore-gcc 会生成 `.sym` 裸段，Lcf 的 copy/clear 表
  不覆盖 → 变量上电随机 → 野指针 trap，uart 基线同样潜伏此坑）。

---

## 2 目录结构（相对 uart 基线的新增/修改）

```
tc397_adc/
├── App/adc.h / adc.c        # EVADC 驱动：AN 表 + 初始化 + 读取 + 换算
├── Shell/shell_adc.c        # adc 命令（全表/单通道）
├── Shell/shell_adcdbg.c     # adcdbg 寄存器级诊断命令
├── Cpu0_Main.c              # + Adc_Init()（启动打印之后）+ 'adc' 提示行
├── CMakeLists.txt           # + Evadc 两源文件；去掉 -fdata-sections
└── build/gcc/tc397_adc.{elf,hex,map}
```

---

## 3 构建与下载（Ubuntu 26.04）

```bash
export PATH=/opt/tricore-gcc/bin:$PATH

cd tc397_adc
./build.sh build                        # Debug（text ~92K，hex ~277K）
./build.sh build --build-type Release
./build.sh download                     # 需 TAS: systemctl status tas-server
./build.sh reset                        # 经 flasher -read 触发复位并运行
```

TAS 未启动时先起服务（本 bench 一直运行，无需操作，仅备忘）：
`systemctl status tas-server`（`ss -tlnp | grep 24817`），
验证 `/home/z/lz/tc387/ref/aurix_flasher_linux-master/linux/aurix_flasher -id list`
应识别 `TC39x ... TriBoard TC2XX V2.0`。

---

## 4 测试方法与步骤

1. `./build.sh download` 烧录（自动复位运行），或 `./build.sh reset` 复位。
2. 打开串口（注意：串口被其他程序占用时会无输出，先确认端口空闲）：
   `python3 -m serial.tools.miniterm /dev/ttyACM0 921600 --raw`。
3. 等启动日志出现 `ADC ready, try 'adc' (AN0..AN47)` + `letter:/$`。
4. 执行 `adc`（全表）与 `adc <n>`（如 `adc 3`、`adc 16` 单通道）。
5. 判据：48 行按 AN00~AN47 顺序；15 个 P40 复用行显示 Reserved；
   其余 33 路有 `pin` 电压 + `raw`，分压路另有 `ext`，HW_VERSION 另有 `vuc~`；
   同一电源多次复位读数稳定（±几 LSB）；VUC 与 HW_VERSION 反推的 vuc 应一致
   （电阻容差内，见 §5）。
6. 异常时用 `adcdbg` 转储 queue/RES 状态辅助定位。

---

## 5 测试结果（2026-09-15，Debug 版，3 次复位全过）

完整日志：`../temp/adc_test.log`（trial 0~2，NO-DATA 计数均为 0）。代表值（trial 0）：

```
AN00 SPARE     pin=0.389V raw= 319 (G0CH0)     # 预留浮空（下同 ~0.4V）
AN08 VPREREG   pin=0.349V ext= 5.819V raw= 286 (G1CH0)
AN09 VS1       pin=0.680V ext=11.332V raw= 557 (G1CH1)
AN10 VBAT      pin=0.702V ext=11.698V raw= 575 (G1CH2)
AN11 ETH_INH   pin=0.198V ext= 3.296V raw= 162 (G1CH3)
AN12 CAN0_INH  pin=0.011V ext= 0.183V raw=   9 (G1CH4)
AN13 IG        pin=0.725V ext=12.085V raw= 594 (G1CH5)
AN14 HSS0      pin=0.001V ext= 0.020V raw=   1 (G1CH6)   # 高边开关关断，无电流
AN15 HSS1      pin=0.001V ext= 0.020V raw=   1 (G1CH7)
AN16 VUC       pin=3.309V raw=2711 (G2CH0)
AN17~19/24~29/32~33/36~39  Reserved (P40.x GPIO, not sampled)   # 15 行
AN20 3V3       pin=3.315V raw=2716 (G2CH4)
AN21 1V25      pin=1.244V raw=1019 (G2CH5)
AN22 0V9       pin=0.917V raw= 751 (G2CH6)
AN23 HW_VERSION pin=0.300V vuc~3.303V raw= 246 (G2CH7)  # 反推 vuc≈VUC 实测值
AN30/31 SPARE  pin≈0.40V（浮空）；AN34 SPARE pin=0.375V
AN35 T1S_INH   pin=3.303V raw=2706 (G8CH3)
AN40~47 EXTADC0~7  pin≈0.000V（外部无输入）
```

交叉验证：VUC 直连 3.309V vs HW_VERSION（10K+1K）反推 3.303V，偏差 0.2%，
与电阻容差自洽，证明 VREF=5V 假设与分压换算正确。
`adcdbg` 显示 G0/G1 全 RES 的 VF=1 且读数持续更新（RES0 319→317），
G2/G3/G8 的 QSR 非空，转换在各组正常进行。

---

## 6 注意事项 / 已知问题

1. **串口被占用时无输出**：本次联调曾出现“已知好的 uart 基线也无输出”，
   实为串口被另一进程占用；确认端口空闲后再测（`ls /dev/ttyACM*` + 关掉占用者）。
2. **首刷后 AN03 偶发一次 NO-DATA**：首次烧录后的第 1 次启动曾出现 AN03
   （G0CH3）连续 3 次 `NO-DATA`，复位后自愈，之后 3 次复位 48/48 全过。
   `adcdbg` 证实硬件 RES3 的 VF/RESULT 正常，属读取侧偶发（转换空窗），
   已在 `Adc_ReadAn` 加 3 轮重试；若复现，用 `adcdbg` 看 QSR/VFR 并记录。
3. **G8 队列为 10 通道 refill（8 级 queue + QBUR 备份周转）**：当前全通道
   VF=1、读数更新正常；`adcdbg` 中 G8 QSR=0x09（FILL 满 + 备份）属正常稳态。
4. **浮空 SPARE 脚约 0.4V**：AN0~7/30/31/34 未接信号，悬空读数 ~0.37~0.40V，
   属正常现象，不代表电源异常。
5. **EXTADC0~7 读 0V**：外部无输入，属预期；接信号后应按 `ext=pin×50/3` 换算。
6. `Adc_Init()` 放在启动打印之后：即使 EVADC 初始化异常挂起，启动日志仍可见，
   便于二分定位（本次联调即用此法排除过 ADC 初始化嫌疑）。
7. 本次仅验证 Debug 版；Release 未测（改动与优化等级无关，风险低）。
8. 提交未推送（按任务要求暂不推送）；`temp/` 日志与 `handover/` 不进 git。

---

## 7 Windows 11 + TASKING 构建（2026-09-18 已验证）

* 工具链：`C:\z\app\TASKING\TriCore_v6.3r1`，Studio 1.10.36，串口 COM165。
  `build.sh`（Ubuntu/GCC）不受影响，两套脚本共存：

```powershell
.\build.ps1 -Compiler tasking -Action download     # Tasking 编译并烧录
```

* 本工程 Tasking 实测：编译 303 obj 0 error；`adc` 48 路电压正常
 （VUC 3.309V / 3V3 3.248V / 1V25 1.235V / 0V9 0.916V / VBAT 11.8V / IG 12.0V）。
* 通用兼容改动见 `tc397/temp/tasking_porting_log.md`（GCC 行为不变）。

---

## 8 许可

* iLLD/Libraries：Infineon Boost Software License 1.0
* Letter-Shell：MIT
* 其余移植代码内部许可
