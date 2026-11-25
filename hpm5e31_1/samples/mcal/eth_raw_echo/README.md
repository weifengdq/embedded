# eth_raw_echo —— AUTOSAR Eth 驱动裸帧收发与主机对打

## 1. 这是什么

用 AUTOSAR Eth 驱动（mcal/Eth）在 HPM5E31 的 ENET0 + YT8011AN 千兆链路上收发**裸以太网帧**
（自定义 EtherType 0x88B5，不走 IP），主机侧用 AF_PACKET 原始套接字对打，验证：

- Eth_ProvideTxBuffer / Eth_Transmit / Eth_TxConfirmation 的发送主路径；
- Eth_Receive / Eth_GetRxBuffer / Eth_ReleaseRxBuffer 的接收主路径（零拷贝）；
- DMA 描述符环、缓冲池、cache/内存放置是否真的工作；
- 往返延迟（RTT）、吞吐、丢包。

## 2. 硬件与接线

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1（HPM5E31，480 MHz） |
| 板端接口 | ENET0 RGMII（PF02~PF13）+ YT8011AN 1000BASE-T1 |
| 对端 | 千兆车载以太网转换盒 → 主机网口 enp6s0（1 Gbit/s 全双工） |
| 主机要求 | Linux + root（AF_PACKET 需要 CAP_NET_RAW）；不需要配 IP |
| PHY 地址 | 1（板上 strap），主从：板端 Slave / 转换盒 Master |

## 3. 编译与运行

```bash
# 编译
bash scripts/build.sh samples/mcal/eth_raw_echo

# 只跑板端（初始化/链路/寄存器/统计）
bash scripts/hwrun.sh mcal/eth_raw_echo --secs 15 --send "eth;eth trcv;eth stat"

# 板端 + 主机对打（推荐：脚本把两者放进同一次持锁窗口）
bash samples/mcal/eth_raw_echo/host/run_host_test.sh eth_raw_echo \
     --host-mode listen --host-secs 25 --secs 22 \
     --send "eth sleep 1500;eth tx 64 2000"
```

主机脚本也可以单独跑（先让板端进入等待/回显状态）：

```bash
sudo python3 samples/mcal/eth_raw_echo/host/eth_host.py --iface enp6s0 echo --secs 25
sudo python3 samples/mcal/eth_raw_echo/host/eth_host.py --iface enp6s0 listen --secs 20
sudo python3 samples/mcal/eth_raw_echo/host/eth_host.py --iface enp6s0 flood --len 512 --secs 10
```

## 4. 命令表

| 命令 | 作用 |
|---|---|
| eth / eth info | 打印控制器模式、MAC、链路、缓冲规模 |
| eth trcv | 打印 EthTrcv 的链路/速率/双工/主从/OUI/SQI |
| eth stat | 打印计数器（tx/rx/错误/长度分桶）与 Det 计数 |
| eth regs | 打印 MAC/DMA 关键寄存器、前 4 个 TX/RX 描述符、TX 缓冲前 22 字节 |
| eth rtt <len> <n> | 广播发 n 帧，等主机回显，按序号算 RTT |
| eth tx <len> <n> | 以最大速率发 n 帧，测帧率/吞吐 |
| eth rx <secs> | 接收窗口，测帧率/吞吐/平均帧长 |
| eth echo <secs> | 板端做回显器（主机 rtt 模式用） |
| eth link [ms] | 等链路 up（轮询 EthTrcv） |
| eth mode active/down | 切控制器模式 |
| eth type <hex> | 改测试 EtherType（默认 0x88B5；改 0800 可排查中间设备按类型过滤） |
| eth dst <mac> | 改目的 MAC（默认广播） |
| eth sleep <ms> | 延时（给主机脚本留启动时间） |

## 5. 实测现象与日志

真实日志（本轮实测，日志名即例程名、banner 已核对）：

- `logs/20261102-162219-eth_raw_echo-host.log`：主机 flood（512 B）→ 板端 8 s 收到
  **180899 帧 / 95.88 MB，crc=0 / overflow=0 / err=0**；主机侧 385.5 Mbit/s。
- `logs/20261102-163212-eth_raw_echo-host.log`：板端 TX burst 64 B×2000 + 512 B×2000，
  主机侧收到 3738 帧（长度分布 64:1949 / 512:1789）。
- `logs/20261102-162509-eth_raw_echo.log`：eth regs 转储，TX 缓冲内容
  `FF FF FF FF FF FF | 02 00 00 00 01 00 | 88 B5 | 45 54 48 30`
  （广播 + 本机 MAC + 0x88B5 + "ETH0"）。

## 6. 关键点与易错点

1. **缓冲必须 DMA 可达**：TX/RX 缓冲放 `.noncacheable.bss`（AXI SRAM）；放默认 .bss（DLM）会
   出现"描述符 OWN 不翻转、计数全 0"。
2. **Eth_Init 之后要 SetControllerMode(ACTIVE)** 才能收发。
3. **主机脚本必须先起监听**：板端 burst 是毫秒级，主机晚启动就什么都抓不到（用 eth sleep 对齐）。
4. 目的 MAC 默认广播，避免依赖主机 MAC；主机回显时用收到的源 MAC 回发。
5. **测试帧要有字段齐全性自检**：本样例曾因为 payload[4..7] 的 seq 字段漏写，导致 RTT 恒为 0（主机 echo 明明成功）——用 hexdump 打出帧头才定位。
6. 板端 TX 的 fps 是"提交速率"，不等于线速：64 B 帧线速上限约 148 kfps（1 Gbit/s），
   超过时会堆在 MAC FIFO 里，表现为丢帧。

## 7. 待确认项

- 错 FCS / 对齐错误帧的主动注入（本板无外部错帧注入器）。
