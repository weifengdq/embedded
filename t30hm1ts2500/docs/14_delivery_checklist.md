# 交付与验收清单

本篇给**项目经理、测试和客户**用：一份可以逐项打勾的交付清单，以及现场 60 分钟验收流程。

## 本篇目录

| 章节 | 内容 |
| --- | --- |
| 1 | 交付物清单 |
| 2 | 环境基线（冻结版本） |
| 3 | 现场验收流程（7 个阶段 / 60 分钟） |
| 4 | 验收记录表（可直接打印填写） |
| 5 | 客户自查表（10 问） |
| 6 | 已知问题与豁免说明 |
| 7 | 交付签字页 |

---

## 1. 交付物清单

### 1.1 软件交付物

| # | 交付物 | 路径 | 验收方式 | 状态 |
| --- | --- | --- | --- | --- |
| S1 | 可移植驱动（平台无关） | `common/s2500/` | `make -C common/s2500 check` 通过 | ☐ |
| S2 | lwIP 2.2 + 配置 | `common/lwip/`、`common/lwip-port/` | 5 平台编译通过 | ☐ |
| S3 | letter-shell + 配置 | `common/letter-shell*/` | 串口能进 shell | ☐ |
| S4 | STM32H723 工程（当前 STM32 平台） | `stm32h723/` | `./build.sh`（CMake）通过 | ☐ |
| S5 | HPM5361 工程 | `hpm5361/` | `./build.sh` + `./flash.sh` | ☐ |
| S6 | S32K312 工程 | `s32k312/` | `./build.sh` + `./flash.sh` | ☐ |
| S7 | TC387 工程 | `tc387/` | `./build.sh` + `./flash.sh` | ☐ |
| S8 | RK3506 Linux 驱动 | `rk3506/` | `insmod` + probe 成功 | ☐ |
| S9 | 主机侧工具 | `host/`、`tools/s25.py` | `acceptance.sh` 跑通 | ☐ |
| S10 | 无硬件单元测试 | `tools/sim/` | 打印 `ALL TESTS PASSED` | ☐ |
| S11 | 文档（14 篇 + 根 README） | `docs/` | 齐全、可读 | ☐ |
| S11b | STM32F407 历史工程（不交付） | —（未随本目录发布） | 仅存档；板子已损坏停用 | — |
| S12 | 总 PDF 手册（可选） | 自行用 `pandoc` 合并 `docs/*.md` | 中文不乱码、目录页码正确 | — |

### 1.2 文档交付物

| # | 文档 | 覆盖内容 | 状态 |
| --- | --- | --- | --- |
| D1 | 根 `README.md` | 项目简介、拓扑、快速开始、状态表、FAQ | ☐ |
| D2 | `docs/12_dev_env_setup.md` | 开发环境从零搭建 | ☐ |
| D3 | `docs/13_10baset1s_primer.md` | 10BASE-T1S / PLCA 入门 | ☐ |
| D4 | `docs/01_hardware.md` | 硬件说明 | ☐ |
| D5 | `docs/02_bus_plan.md` | 总线规划与端接 | ☐ |
| D6 | `docs/03_spi_protocol.md` | SPI 协议 | ☐ |
| D7 | `docs/04_register_map.md` | 寄存器速查 | ☐ |
| D8 | `docs/00_porting_guide.md` | 移植指南 | ☐ |
| D9 | `docs/05_lan8651_vs_s2500.md` | 与 LAN8651 对比 | ☐ |
| D10 | `docs/06_test_matrix.md` | 测试矩阵 | ☐ |
| D11 | `docs/07_performance.md` | 性能方法与结果 | ☐ |
| D12 | `docs/08_shell_commands.md` | 命令手册 | ☐ |
| D13 | `docs/09_debug_env.md` | 调试环境 | ☐ |
| D14 | `docs/10_hardware_troubleshooting.md` | 硬件排障 | ☐ |
| D15 | `docs/11_test_examples.md` | 测试例子集 | ☐ |
| D16 | `各平台目录下的 README.md` | 各平台说明（H723/HPM/S32K/TC387/RK3506） | ☐ |
| D17 | `docs/results/` | 实测记录归档 | ☐ |

### 1.3 硬件交付物（如包含）

| # | 项目 | 数量 | 备注 |
| --- | --- | --- | --- |
| H1 | S2500_1 最小系统板 | — | 原理图 `docs/datasheet/S2500_1_refsch.pdf` |
| H2 | 总线线束（含两端 100 Ω 终端） | — | **中间节点不带终端** |
| H3 | USB-10T1S 适配器（EVB-LAN8670-USB） | 1 | 主机侧 PLCA 协调者 |
| H4 | 调试器 | — | STLINK-V3 / J-Link / FT2232H / DAP |

---

## 2. 环境基线（冻结版本）

验收时必须记录实际版本，与下表对比：

| 项 | 基线值 | 实测值（填写） |
| --- | --- | --- |
| Ubuntu | 22.04 / 24.04 LTS | |
| 内核 | ≥ 6.9（PLCA 需要） | |
| `arm-none-eabi-gcc` | 14.2.1（支持 Cortex-M7） | |
| S32K312 SPI 后端 | LPSPI0 15.137 MHz（默认） | |
| `openocd` | 0.12.0 | |
| `iperf` | 2.1.x（**必须 v2**） | |
| `ethtool` | ≥ 6.19 | |
| TriCore GCC | 13.4.1（`/opt/tricore-gcc-13.4.1`） | |
| HPM RISC-V GCC | `sdk_env` 自带 multilib | |
| S2500 IDVER | 0x00000011 | |
| S2500 ONICVER | 0xA7A813D2 | |
| 总线 NCNT / TOTMR | 8 / 32 BT | |

---

## 3. 现场验收流程（7 个阶段 / 60 分钟）

### 阶段 1：主机与设备（10 分钟）

```bash
lsusb | grep -E "0483:3754|1366:0105|0403:6010|058b:0043|184f:0051"
ls -l /dev/s2500-*                       # 或记录实际 ttyACM / ttyUSB 编号
uname -r && iperf --version | head -1 && ethtool --version
```

| 检查点 | 期望 | 结果 |
| --- | --- | --- |
| 五类 USB 设备都在 | 列出 5 条 | ☐ |
| 串口可访问 | 无 Permission denied | ☐ |
| iperf 是 v2 | 2.1.x | ☐ |

### 阶段 2：无硬件软件检查（5 分钟）

```bash
make -C common/s2500 check
gcc -std=c11 -Icommon/s2500 tools/sim/s2500_sim.c common/s2500/s2500.c \
    -o /tmp/sim && /tmp/sim
```

| 检查点 | 期望 | 结果 |
| --- | --- | --- |
| 驱动编译 | 0 error | ☐ |
| 协议单测 | `ALL TESTS PASSED` | ☐ |

### 阶段 3：单板自检（不接总线，10 分钟）

```bash
./tools/s25.py --auto <board> -c "s2500 reg 0 0"     # 0x00000011
./tools/s25.py --auto <board> -c "s2500 info"
./tools/s25.py --auto <board> -c "s2500 selftest"    # PCS / PHY PASS
./tools/s25.py --auto <board> -c "s2500 counters clear"
```

| 检查点 | 期望 | 结果 |
| --- | --- | --- |
| IDVER | 0x00000011 | ☐ |
| 出厂 MAC | 非全 0、单播 | ☐ |
| PCS 环回 | PASS | ☐ |
| PHY 环回 | PASS | ☐ |

### 阶段 4：总线与 PLCA（10 分钟）

```bash
sudo ./host/setup_usb10t1s.sh
ethtool --get-plca-status enx001ec0d1c337            # Status: on
./tools/s25.py --auto <board> -c "s2500 plca id N ncnt 8 totmr 32 en 1"
./tools/s25.py --auto <board> -c "s2500 status"      # PST=1
ping -c 20 -i 0.05 192.168.10.NN                     # 0% 丢包
```

| 检查点 | 期望 | 结果 |
| --- | --- | --- |
| 主机 PLCA | `Status: on` | ☐ |
| 板端 PST | 1 | ☐ |
| PLCADIAG | 0（无 RXINTO/UNEXPB/BCNBFTO） | ☐ |
| ping 20 次 | 0% 丢包 | ☐ |

### 阶段 5：吞吐与诊断（10 分钟）

```bash
./tools/s25.py --auto <board> -c "iperf -s" &
iperf -c 192.168.10.NN -t 10 -i 5 -f m
./tools/s25.py --auto <board> -c "s2500 bench tx 1000 500"
./tools/s25.py --auto <board> -c "s2500 sqi"
./tools/s25.py --auto <board> -c "s2500 hdd"
```

| 检查点 | 期望（HPM5361 基线） | 结果 |
| --- | --- | --- |
| TCP 吞吐 | ≥ 7 Mbit/s（硬件 SPI） | |
| 纯发送 bench | ≥ 9 Mbit/s（约 2380 fps@500B） | |
| SQI | 正常链路 7/7 | |
| HDD | 记录 `SHORT_OPEN` 值（本项目常见 =2，见豁免 R1） | |

### 阶段 6：多节点并发（10 分钟）

```bash
for i in 12 13 14; do ping -c 5 -i 0.05 192.168.10.$i & done; wait
```

| 检查点 | 期望 | 结果 |
| --- | --- | --- |
| 三节点 PST | 全 1 | ☐ |
| 并发 ping | 0% 丢包 | ☐ |
| PLCADIAG | 全 0 | ☐ |

### 阶段 7：文档与 PDF（5 分钟）

```bash
ls docs/*.md | wc -l          # 期望 >= 15
# 总 PDF 为可选项：本目录不附带打包脚本，可用 pandoc 自行合并 docs/*.md
```

| 检查点 | 期望 | 结果 |
| --- | --- | --- |
| 文档数量 | ≥ 15 篇 | ☐ |
| 总 PDF（可选） | 自行用 pandoc 合并 docs/*.md | — |
| 中文显示 | 首末页无方块 / 乱码 | ☐ |
| 目录页码 | 与实际章节页码一致 | ☐ |

---

## 4. 验收记录表

| 项 | 记录 |
| --- | --- |
| 验收日期 | |
| 验收地点 | |
| 参与人 | |
| 被测平台 | ☐ STM32H723 ☐ HPM5361 ☐ S32K312 ☐ TC387 ☐ RK3506（F407 历史平台，不交付） |
| 板卡序列号 / MAC | |
| 主机网卡 MAC | |
| 总线线长 | |
| 终端电阻配置 | ☐ 仅两端 ☐ 每板都有 |
| S2500 IDVER | |
| 阶段 1 结论 | ☐ 通过 ☐ 不通过 |
| 阶段 2 结论 | ☐ 通过 ☐ 不通过 |
| 阶段 3 结论 | ☐ 通过 ☐ 不通过 |
| 阶段 4 结论 | ☐ 通过 ☐ 不通过 |
| 阶段 5 结论 | ☐ 通过 ☐ 不通过 |
| 阶段 6 结论 | ☐ 通过 ☐ 不通过 |
| 阶段 7 结论 | ☐ 通过 ☐ 不通过 |
| 遗留问题 | |

---

## 5. 客户自查表（10 问）

| # | 问题 | 是 / 否 |
| --- | --- | --- |
| 1 | 我能从零装好开发环境并编译出固件吗？（D2） | ☐ |
| 2 | 我能用 `s25.py` 连上串口并执行 `help` 吗？ | ☐ |
| 3 | 我知道 `s2500 reg 0 0` 应该返回什么吗？（0x00000011） | ☐ |
| 4 | 我知道总线两端各 100 Ω、中间不能带终端吗？（D5 / D3） | ☐ |
| 5 | 我知道 PLCA 只有一个协调者、NCNT 必须一致吗？（D3） | ☐ |
| 6 | 我能看懂 `s2500 status` 里 PST / CRPS / 诊断位吗？（D12） | ☐ |
| 7 | 我知道 iperf 必须用 v2 吗？ | ☐ |
| 8 | 总线不通时我知道先量 50 Ω、再看 PST 吗？（D14） | ☐ |
| 9 | 我知道自己的平台吞吐基线是多少吗？（D11） | ☐ |
| 10 | 我能自己重新生成 PDF 手册吗？ | ☐ |

10 问中有 2 个以上"否"，建议先补读对应文档。

---

## 6. 已知问题与豁免说明

交付时以下问题**已记录、可豁免**，但必须让客户知晓：

| 编号 | 问题 | 影响 | 豁免条件 |
| --- | --- | --- | --- |
| E1 | 自制 S2500_1 板每块都焊了 49.9 Ω×2 终端 | 多板并联时 HDD=`SHORT_OPEN=2`、SQI 0–1/7 | 客户自行拆除中间节点 R12/R13 后复测 |
| E2 | STM32F407 板子已物理损坏（2026-11-20 停用） | 不再作为交付平台 | 已由 **STM32H723ZGT6** 接替（`stm32h723/`，同 IP/PLCA ID）；实测 TCP 9.07/8.87、协调者 5/5 通过 |
| E3 | S32K312 早期只能用软件 SPI（~3 MHz） | 吞吐约 1.2–1.4 Mbit/s | **2026-11-20 已解决**：LPSPI0 打通并设为默认，6.26/7.75；软件 SPI 保留回退 |
| E4 | TC10 / TC6 / TC14 未实测 | 特性仅代码就绪 | 明确标注"未测"，不承诺指标 |
| E5 | RK3506 曾 panic（已恢复） | 本轮实测 8.83/8.96 Mbit/s 正常 | 已闭环，保留记录 |
| E6 | 主机 PLCA 配置在 USB 重枚举后丢失 | 需重跑脚本 | 提供 systemd/udev 方案，客户可选 |
| E7 | RK3506 管理口 eth0 为 DHCP，IP 会漂移 | 照旧 IP SSH 可能连不上 | 交付时记录当前 `<board-ip>`；现场先串口 `ip -br addr` 确认；t1s0 固定 192.168.10.15 |
| E8 | S32K312 LPSPI0 未通过（已解决） | 默认已切 LPSPI0，6.26/7.75 Mbit/s | 已闭环；eDMA/零拷贝未做，不影响交付指标 |

---

## 7. 交付签字页

| 角色 | 姓名 | 签字 | 日期 |
| --- | --- | --- | --- |
| 交付方（技术负责人） | | | |
| 交付方（项目经理） | | | |
| 接收方（技术负责人） | | | |
| 接收方（项目经理） | | | |

**备注**：

---

## 相关章节

| 想看什么 | 去哪里 |
| --- | --- |
| 环境怎么搭 | [`docs/12_dev_env_setup.md`](12_dev_env_setup.md) |
| 每一项测试怎么跑 | [`docs/11_test_examples.md`](11_test_examples.md) |
| 测试矩阵全表 | [`docs/06_test_matrix.md`](06_test_matrix.md) |
| 性能基线 | [`docs/07_performance.md`](07_performance.md) |
| 出问题怎么查 | [`docs/10_hardware_troubleshooting.md`](10_hardware_troubleshooting.md) |
