# port_pin_config —— AUTOSAR Port 引脚配置 + SPI1<->SPI3 杜邦线 GPIO 回环

一句话：用 `Port_Init` 一次配好 16 个引脚（方向/上下拉/开漏/驱动强度），再用板载杜邦线做
输出→输入回环、上拉/下拉、开漏和运行时改方向测试。

## 硬件要求 / 接线

| 信号 | 输出脚 | 输入脚 | 说明 |
|---|---|---|---|
| nCS  | PC11（SPI1 CS） | PA27（SPI3 CS） | GPIO 回环主测线 |
| MISO | PC12（SPI1 MISO） | PA28（SPI3 MISO） | 开漏测试（PC12 开漏 + PA28 内部上拉） |
| SCLK | PC10（SPI1 SCLK） | PA26（SPI3 SCLK） | 第二组回环 + 上下拉测试 |
| BOOT | PA02 / PA03 | — | 兼 BOOT_MODE0/1：**复位时不要按住 PA03**，否则进 ISP |

杜邦线由用户按 `docs/MCAL开发约定.md` §2 接好，无需额外器件。

## 编译与运行

```bash
bash scripts/build.sh samples/mcal/port_pin_config
bash scripts/hwrun.sh mcal/port_pin_config --secs 25 --send 'pinfo;ploop 2000;ppull;pod;pdir;prefresh;perr;pver'
```

| 命令 | 作用 |
|---|---|
| `pinfo` | 打印 16 个引脚的 pad 索引/方向/模式/上下拉/开漏/初值 |
| `ploop [n]` | 回环自测：全 0、全 1 + n 组 LFSR 随机图案，统计误码 |
| `ppull` | PA27、PA26 内部上拉读 1 / 下拉读 0（对端设高阻） |
| `pod` | PC12 开漏：释放（对端上拉）读 1，拉低读 0 |
| `pdir` | 运行时 `Port_SetPinDirection` IN->OUT |
| `prefresh` | `Port_RefreshPortDirection`（只刷新不可变方向引脚） |
| `perr` | 触发 5 条 Dev Error（非法 pin/方向/模式/不可变方向/空指针） |
| `pver` | `Port_GetVersionInfo` |

## 实测现象（串口输出）

```text
SELFTEST: loopback errors=0 -> PASS
letter:/$ ploop 2000
LOOP: n=2000 patterns=2+2000 errors=0 -> PASS
letter:/$ ppull
PULL: PA27 pull-up read=1 pull-down read=0 PAD_CTL=0x01120075 -> PASS
PULL: PA26 pull-up read=1 pull-down read=0 PAD_CTL=0x01120075 -> PASS
PULL: summary PASS
letter:/$ pod
OD: PC12 released -> PA28=1, PC12 low -> PA28=1 -> PASS
letter:/$ perr
DET: before=0 after=5 delta=5
```

日志：`logs/20261102-135603-port_pin_config.log`（最终 PASS 版本）。

## 关键点

- **Port 管方向/模式，Dio 管电平**：例程里 `Port_Init` 之后统一用 `Dio_ReadChannel` 读回，
  这就是 AUTOSAR 的分工（Dio 没有 Init API）。
- **HPM 回读坑**：GPIO 输出必须置 `IOC PADx.FUNC_CTL.LOOP_BACK=1`（"force input on"），DI 才反映真实引脚电平；
  否则 `Dio_ReadChannel` 对输出通道恒返回 0（SWS_Dio_00083 要求真实电平）。驱动已在 GPIO 模式下自动置位。
- **上下拉要等稳定**：改成内部下拉后立刻读会读到旧电平（100 kΩ 对 pad 电容），例程加了 200 us 等待才通过。
- 开漏输出写 1 是"释放"，电平由对端上拉决定——这正是 I2C 的电气模型。
- `PAD_CTL=0x01120075`：HYS=1、PRS=1(47k)、PE=1、SR=1、SPD=3、DS=5；PS=0 表示下拉档。

## 待确认项

- 已实测：2002 组回环图案 0 误码；两组上下拉；开漏；5 条 Det 错误。
- 已知限制：回环依赖板载杜邦线，若线被拔掉 `ploop` 会 FAIL（这是预期的接线自检）。
