# S2500 Linux 驱动 (out-of-tree)

onsemi **T30HM1TS2500**(S2500)10BASE-T1S MAC-PHY 的 Linux 网络驱动,
以 out-of-tree 模块形式提供, 不需要改内核源码(只建议改 DTS, 见
`patches/0006-dts-rk3506-s2500-t1s.patch`)。

## 1. 文件

| 文件 | 说明 |
| --- | --- |
| `s2500.c` | 驱动主体: SPI driver + net_device + OA-SPI 事务 + sysfs |
| `s2500_regs.h` | S2500 寄存器定义 (MMS0/1/2/3/4/12) |
| `Makefile` | `obj-m := s2500.o` |

模块名 `s2500.ko`, compatible:

* `onsemi,t30hm1ts2500` (正式)
* `onsemi,s2500`
* `microchip,lan8651` (过渡兼容: RK3506 EVM 现有 DTS 仍是 lan865x 节点,
  需要先 `unbind` 内建 lan865x 驱动才能绑定; 打了 0006 补丁后不需要)

## 2. 架构

```
  net_device (t1s0)
    ├── ndo_start_xmit ─→ skb 队列 ─┐
    ├── ndo_open/stop  ─→ 寄存器初始化/使能
    ├── ndo_get_stats64 ─→ 缓存计数 (原子上下文, 不发 SPI)
    └── sysfs (plca_*/s2500_*)
                                     │
                     SPI 线程 (kthread) ◄─ IRQ_N (GPIO0_A6, 电平低)
                       ├── TX/RX: 一次突发最多 48 块(3264B), 见下文 2.1
                       ├── 按 BUFSTS 的 TX credit / RX 可用块数组装突发
                       └── RX 按块 footer 的 SV/EV/SWO/EBO 拼帧, 校验并剥 FCS
                       └── 500ms 轮询 PHY 链路 / STATUS0 错误 / 计数
```

关键点:

* **所有 OA-SPI 传输只在 kthread / 工作队列 / probe / ndo_open 等可睡眠上下文执行**。
  `ndo_set_rx_mode`、`ndo_get_stats64` 只置标志或用缓存(内核在这两个
  回调里可能处于原子上下文, 直接 `spi_sync` 会 "scheduling while atomic")。
* 控制事务固定 12 字节: `[头4][数据4][填充4]`, MISO 回 `[0][头回显][数据]`。
* 数据事务 68 字节: `[数据头4][负载64]`, MISO 回 `[负载64][数据尾4]`。
* SPI mode 0, MSB first, 8bit; DTS 用 `spi-max-frequency = <25000000>`
  (S2500 规格上限 25 MHz), 运行时可用模块参数 `spi_hz` 覆盖。
* 复位后必须写 `PHY_CTRL(0xFF00).LCTL=1`, 否则 PHY 收发关闭。
* MAC 用标准 OA 寄存器: `MCONTROL0(MMS1 0x0000)` 的
  `FCSA|ADRF|TXEN|RXEN`; 地址过滤 0 (MADDRFILT0L/H + MASK0)。
* RX 帧带 4 字节 FCS, 驱动校验 CRC32 后剥除; `rx_fcs_strip=0` 可关闭。
* TX 由 MAC 追加 FCS(`FCSA=1`), 驱动负责补足 60 字节最小帧。

### 2.1 突发(burst)数据通路 (性能关键)

OA-SPI 允许**一次片选内连续搬多个 68 字节数据块**。驱动按 TX credit 与
RX 可用块数把若干块拼成一次 `spi_sync`(上限 48 块 / 3264 字节), 这是
S2500 能跑到接近 10BASE-T1S 线速的前提:

| 方式 | 1514B 帧的 SPI 事务数 | 25MHz 实测 TCP |
| --- | --- | --- |
| 逐块 `spi_sync` | 24 | 6.2 / 6.7 Mbit/s |
| 48 块一次突发 | 1~2 | **8.8 / 9.0 Mbit/s** |

原因: 68 字节在 25 MHz 下只需 22 us, 但一次 `spi_sync` 的实际开销约
75 us(含 DMA 映射/中断/调度), 逐块传 24 次就把 10 Mbps 的链路吃成 6.7 Mbps。
突发里 TX 与 RX 共用同一次传输: 前面的块装待发帧, 后面的块补空块取回数据,
块尾的 footer 同时带回 TX credit (bit[5:1]) 与 RX 可用块数 (bit[28:24])。

## 3. 构建

```bash
cd rk3506
# 先按 ../README.md 第 5 节配好 SDK_DIR / CROSS_COMPILE
make -C "$SDK_DIR/kernel" M="$PWD/drivers/s2500" modules
# 产物: drivers/s2500/s2500.ko
```

或直接 `bash scripts/18-install-s2500.sh build`。

## 4. 加载(不改 DTS 时)

现有 DTS 里 `spi0.0` 是 lan865x 节点, 内建 lan865x 驱动会先绑定, 因此:

```bash
echo spi0.0 > /sys/bus/spi/drivers/lan8650/unbind
insmod /tmp/s2500.ko
ip link set eth1 name t1s0
ip addr add 192.168.10.15/24 dev t1s0
ip link set t1s0 up
```

打了 `0006-dts-rk3506-s2500-t1s.patch` 并重编 DTB/FIT 后, 开机即自动绑定,
接口仍为 `ethX`, 可用 udev/init 脚本改名为 `t1s0`, 或用脚本 18 自动改名。

## 5. sysfs 接口

`/sys/class/net/t1s0/`:

| 属性 | 读写 | 说明 |
| --- | --- | --- |
| `plca_enable` | RW | PLCA 使能 (0/1) |
| `plca_id` | RW | PLCA node id (0=协调者, 1..254) |
| `plca_ncnt` | RW | 节点数 |
| `plca_totmr` | RW | TO timer (bit times) |
| `plca_status` | RO | `pst=<0/1> crps=<0/1>` |
| `plca_cr` | RW | 协调者冗余 (PLCAEXT.CREN) |
| `s2500_sqi` | RO | `sqi=<0..7> sqi_plus=<0..255>` |
| `s2500_hdd` | RW | 写 1 触发 HDD, 读返回 `valid/short_open/one_wire/raw` |
| `s2500_counters` | RO | MAC 统计 + 驱动统计 |
| `s2500_regs` | RO | 关键寄存器快照 |
| `s2500_loopback` | RW | 0=关, 1=PCS 环回, 2=PHY 环回 (无总线自测) |
| `s2500_led` | RW | DIO0/DIO1 内部 LED 功能开关 |

模块参数: `plca_en plca_id plca_ncnt plca_totmr plca_cr led rx_fcs_strip spi_hz`。

## 6. 已知限制

* 只实现了单队列、store-and-forward; cut-through、TC10、TC6 时间戳未接入
  Linux 框架(寄存器层已具备)。
* PLCA 没有走 phylib/ethtool netlink(设备不是 MDIO PHY), 统一用 sysfs,
  与 MCU 固件的命令语义一致。
* 未做 suspend/resume、ethtool 自检、RSS/多队列。
