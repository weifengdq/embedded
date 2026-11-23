# lan8651_spi_probe —— LAN8651 10BASE-T1S 链路与芯片自证

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

在 hpm5e31_1 上把 LAN8651（10BASE-T1S MAC-PHY）最小可用链路打通，并按顺序自证：

1. **SPI2 控制事务**：12 字节帧（4B 头 + 4B 值 + 4B 忽略）+ echo 校验 `rx[4..7] == tx[0..3]`；
2. **复位/中断链路**：nRST(PA19) / nINT(PA24) / SDO(PA22) 三态；
3. **TC6 标准寄存器**：`OA_ID / OA_PHYID / OA_STDCAP / OA_CONFIG0 / OA_BUFSTS / INT_MASK0`；
4. **内部 PHY**：Clause 22 PHY ID（期望 `0x0007C1B3`）、MMD31 厂商寄存器；
5. **AN1760 参数下发**（LAN865x Rev.B0/B1 的 17 条 fixup + SQI 12 条）；
6. **PLCA（TC14）**配置与状态回读。

## 接线

| LAN8651 | HPM5E31 | 说明 |
|---|---|---|
| SCLK | PA20 | SPI2，Mode 0，2 MHz 起 |
| SDO (MISO) | PA22 | 芯片输出，主机输入 |
| SDI (MOSI) | PA23 | 主机输出 |
| CS_N | PA21 | SPI2 硬件片选（也可 `cs 1` 切成 GPIO 片选） |
| RESET_N | PA19 | 低有效，代码复位时拉低 2 ms |
| IRQ_N | PA24 | 低有效，内部上拉 |
| VDDP/VDDAU | 3V3 | 板上已接 |

10BASE-T1S 侧：TRX_P/TRX_N 接主机 USB-10BASE-T1S 适配器 `enx001ec0d1c337`（对端脚本见 `lan8651_raw_frame`）。

## 命令

| 命令 | 作用 |
|---|---|
| `boot` | 传输层自动探测（硬件 SPI → 位翻转 fallback）+ 完整 bring-up |
| `probe` | 打印 TC6/PHY/PLCA 状态与统计 |
| `regs` | 一次读 MMS0 的 0x00~0x0F 共 16 个寄存器 + Clause 22 PHY ID |
| `rdtest` | 重复单寄存器读 vs 块读对照（查"读到上一个寄存器"现象） |
| `trans <bb\|hw>` | 切换传输层：位翻转 / SPI2 控制器 |
| `delay <nops>` | 位翻转半周期（nops），并实测得到的 SCLK |
| `hwchk` | 打印 SPI2 控制器寄存器 + 一次控制事务结果 |
| `plca <on\|off> [id] [cnt] [to] [bc] [bt]` | 配 PLCA（默认 8 节点 / to_tmr=32 / burst 0,128） |
| `rd/wr <addr> [val]` | TC6 寄存器读写 |
| `mmd/mmwr <mms> <reg> [val]` | PHY MMD 寄存器读写（4 = MMD31） |
| `levels` | 打印 nRST / nINT 电平 |

## 硬件 SPI2 修复（2026-10-30，本板最重要的一个坑）

早期版本只能靠 GPIO 位翻转跑 TC6。根因与修复分四步，缺一不可：

1. **SPI2 时钟门控**：板级 `board_init_spi_clock()` 原本不支持 SPI2 → 寄存器写被静默丢弃。修复：`clock.c` 加 `init_spi2_clock()`，驱动先调 `board_init_spi_clock(HPM_SPI2)`。
2. **CS 必须真的由软件控制**：`spi_master_get_default_control_config()` 的 `cs_index` 默认是 `spi_cs_0 = 1`（不是 0），`spi_transfer()` 每次都把 `SPI_CTRL.CS_EN` 置 1；同时 PA21 原先是 `SPI2_CS_0`。必须 **PA21 改 `GPIO_A_21` + `CTRL.CS_EN = 0` + 三段 guard 时序**（CS 低 → 1 µs → 传输 → 1 µs → CS 高 → 1 µs）。
3. **决定性根因：SCLK pad 少了 `LOOP_BACK`**：`IOC_PA20_FUNC_CTL_SPI2_SCLK | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK`。SDK 自动生成的 pinmux 在 SPI SCLK 上一直带这一位，手写 pinmux 漏掉后，控制器"发完也读回 12 字节"，但 SCLK 根本没到引脚 → LAN8651 收不到时钟，SDO 保持空闲电平（先 0xFF、后 0x00）。
4. **长事务用 FIFO 感知的传输**：`hpm_spi_transmit_receive_blocking()` 在超过 RX FIFO 的事务（16 寄存器块读 72 B、多块数据帧）会丢 RX 字节导致错位；改用底层 `spi_transfer()`（`spi_write_read_data` 带 FIFO 重试）后 72 B / 1632 B 事务全部正确。

**不依赖示波器的验证**：`hwtx` 命令用硬件 SPI 写 `OA_CONFIG0 = 0x1234`，echo 返回 `20 00 04 01 | 00 00 12 34`（命令头回显 + 写入值回显）→ CS/SCLK/MOSI/MISO 四条线全通。

**修复后扫描**（`scan`，每档 32 次控制读）：

| 请求 SCLK | 实际 | echo 错误 | 单次读 |
|---|---|---|---|
| 1 MHz | 1 MHz | 0 | 104.1 µs |
| 2 MHz | 2 MHz | 0 | 55.1 µs |
| 5 MHz | 5 MHz | 0 | 26.2 µs |
| 10 MHz | 10 MHz | 0 | 16.5 µs |
| 20 MHz | 20 MHz | 0 | 11.7 µs |

日志：`logs/20261030-150718-lan8651_spi_probe.log`（扫描）、`logs/20261030-145856-lan8651_spi_probe.log`（hwtx echo 证据）。

## 实测现象（2026-10-30）

```text
$ bash scripts/hwrun.sh lan8651/lan8651_spi_probe --secs 14 --send 'boot;regs;probe'
board_init_spi_clock(SPI2) = 80000000 Hz
[auto] HW SPI  : read STATUS0 -> ret -3  ... rx: ff ff ff ff ...
[auto] SPI2 controller gives no echo -> fall back to bit-banged SPI
[bring-up] hardware reset (nRST low 2 ms) + TC6 + AN1760 + MAC
           result: ok (0)
  0x0000 OA_ID       = 0x00000011        (MAJVER=1 MINVER=1)
  0x0001 OA_PHYID    = 0x0007c1b4        (OUI=0x0C001F MODEL=27 REV=4)
  0x0002 OA_STDCAP   = 0x000005e5        (DPRAC=1 IPRAC=0 TXFCSVC=1)
  0x0004 OA_CONFIG0  = 0x00009006        (SYNC=1 ZARFE=1)
  0x000b OA_BUFSTS   = 0x00003000        (tx_credits=16)
  C22 reg2/3 (PHY ID) = 0x0007 0xc1b3 -> 0x0007c1b3   MATCH
```

日志：`logs/20261030-113834-lan8651_spi_probe.log`（另有 `logs/20261030-111739-lan8651_spi_probe.log` 记录复位/三态诊断）。

## 关键点 / 坑

1. **SPI2 时钟门控**：板级 `clock.c` 原本没有 `init_spi2_clock()`，`board_init_spi_clock(HPM_SPI2)` 返回 0 且不开时钟 → 所有 SPI2 寄存器写被丢弃，读回全 0xFF。必须先调用 `board_init_spi_clock()` 并用其返回值当 SPI 源时钟。
2. **Clause 22 地址**：PHY 的 C22 寄存器在 **MMS0 的 0xFF00** 起始窗口，不是 MMS=0xFF。写错会读到垃圾值（本项目一度把 PHY ID 读成 `0x1EA20000`）。
3. **位翻转 fallback**：`trans bb` 不经过 SPI2 外设，用来分离"外设配置问题"与"芯片/接线问题"；半周期太小（<10 nops）时单寄存器读会不稳，优先用 `regs` 的块读交叉验证。
4. **复位后 nINT 一定是低的**：芯片按规范拉低 IRQ_N 直到主机清 RESETC 并置 SYNC，别误判成"芯片没起来"。
