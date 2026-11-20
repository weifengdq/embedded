# HEX 固件说明

本目录是 **T30HM1TS2500（S2500）10BASE-T1S** 五个平台的可烧录固件，用于快速验证与现场演示。
**所有固件均构建于 2026-11-20**；源码就在本目录各平台工程里（`stm32h723/`、`hpm5361/`、
`s32k312/`、`tc387/`、`rk3506/`），可以自行重新编译。

固件已内置：静态 IPv4、PLCA 跟随者（`en=1`，ID 见下表）、lwIP 2.2（ARP / ICMP / UDP / TCP / iperf）、
letter-shell 命令集、S2500 自愈看门狗。上电后串口执行 `s2500 info`，正常应看到
`IDVER = 0x00000011`；执行 `s2500 status`，PLCA 场景以 `PST=1` 判断链路。

## 文件清单

| 平台 | 固件文件 | 大小 | IPv4 | PLCA ID | SPI | 调试串口 |
| --- | --- | --- | --- | --- | --- | --- |
| STM32H723 | `stm32h723_s2500.hex` / `stm32h723_s2500.bin` | 269 KB / 96 KB | 192.168.10.11 | 1 | SPI1 25 MHz + DMA | USART1 115200 |
| HPM5361 | `hpm5361_s2500.hex` / `hpm5361_s2500.bin` | 430 KB / 164 KB | 192.168.10.12 | 2 | SPI1 25 MHz + HDMA | UART2 115200 |
| S32K312 | `s32k312_s2500.hex` | 499 KB | 192.168.10.13 | 3 | LPSPI0 15.137 MHz | LPUART0 115200 |
| TC387 | `tc387_s2500.hex` | 534 KB | 192.168.10.14 | 4 | QSPI2 25 MHz + DMA | ASCLIN0 115200 |
| RK3506 | 无 hex（Linux 驱动） | — | 192.168.10.15 | 5 | Linux SPI | UART 1.5 Mbit/s |

> RK3506 不是裸机工程，不需要烧 hex：编译/使用 `rk3506/prebuilt/s2500.ko` 即可
> （`insmod s2500.ko`，脚本会把它改名为 `t1s0`），详见 [../rk3506/README.md](../rk3506/README.md)。

## 烧写方式

### STM32H723

```bash
# 方式一：STM32CubeProgrammer
STM32_Programmer_CLI -c port=SWD -w stm32h723_s2500.hex -v -rst

# 方式二：OpenOCD + STLINK
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "program stm32h723_s2500.hex verify reset exit"
```

### HPM5361

```bash
# OpenOCD 必须带 hpm_xpi flash 驱动（系统自带 0.12 不带）
openocd -f boards/hpm5361.cfg \
  -c "program hpm5361_s2500.hex verify reset exit"
```

也可以用 [../hpm5361/flash.sh](../hpm5361/flash.sh)（会按 `HPM_SDK_BASE` 找 OpenOCD）。

### S32K312

```bash
# J-Link Commander
JLinkExe -device S32K312 -if SWD -speed 4000
# 进入后：
#   loadfile s32k312_s2500.hex
#   r
#   g
```

### TC387

用 Infineon **DAS / AURIX Flasher**（DAP MiniWiggler）烧写 `tc387_s2500.hex`；
也可用 [../tc387/flash.sh](../tc387/flash.sh)（需设置 `AURIX_FLASHER` 路径）。

### RK3506

```bash
insmod rk3506/prebuilt/s2500.ko
ip -br addr show t1s0          # 期望 192.168.10.15/24
cat /sys/class/net/t1s0/plca_status
```

## 上电自检

```bash
./tools/s25.py --auto hpm -c "s2500 info"       # IDVER / PHYID / MAC
./tools/s25.py --auto hpm -c "s2500 plca status" # PST=1 表示已加入 PLCA
ping -c 3 192.168.10.12
```

## 重新编译

| 平台 | 命令 |
| --- | --- |
| STM32H723 | `cd stm32h723 && ./build.sh` |
| HPM5361 | `cd hpm5361 && ./build.sh`（需 `HPM_SDK_BASE`） |
| S32K312 | `cd s32k312 && ./build.sh` |
| TC387 | `cd tc387 && ./build.sh`（需 TriCore GCC） |
| RK3506 | 见 [../rk3506/README.md](../rk3506/README.md) |

> 重编产物路径：STM32H723 `build/*.hex`；HPM5361 `build/output/demo.elf`（再用
> `riscv32-unknown-elf-objcopy -O ihex` 转 hex）；S32K312 `build/Debug/*.hex`；TC387 `build/*.hex`。
