# components/enet_phy_yt8011 —— YT8011AN 千兆车载以太网 PHY 驱动

YT8011AN（Motorcomm，1000BASE-T1/100BASE-T1 单对线车载以太网 PHY）的寄存器级驱动，
后端使用 HPM SDK 的 ENET MDIO 接口（`enet_read_phy()/enet_write_phy()`）。

## 目录

| 文件 | 内容 |
|---|---|
| `inc/hpm_yt8011_regs.h` | 寄存器与位域定义（Clause 22 / EXT / MMD 三套空间） |
| `inc/hpm_yt8011.h` | 对外 API |
| `src/hpm_yt8011.c` | 实现（MDIO 读写、EXT/MMD 间接访问、配置与状态解析） |

## 用法

本组件不在 SDK 的 `find_package(hpm-sdk)` 组件体系内（那需要改 SDK 或 boards/CMakeLists.txt），
由各例程显式加入源文件与头文件路径：

```cmake
set(NET2_COMPONENT ${CMAKE_CURRENT_LIST_DIR}/../../../components/enet_phy_yt8011)
sdk_inc(${NET2_COMPONENT}/inc)
sdk_app_src(${NET2_COMPONENT}/src/hpm_yt8011.c)
```

## API 一览

| API | 作用 |
|---|---|
| `yt8011_check_id(base, addr)` | 校验 ID1=0x4F51、ID2[15:10]=0x3A |
| `yt8011_reset(base, addr)` | 纯软复位（BMCR[15]），**会把速率配置恢复为默认** |
| `yt8011_soft_reset_cfg(base, addr, speed)` | **推荐**：带速率编码软复位（1000BASE-T1 写 0x8140，100BASE-T1 写 0xA100） |
| `yt8011_soft_reset_an(base, addr)` | 软复位 + 使能自协商（BMCR=0x9140） |
| `yt8011_read_ext / write_ext` | EXT 寄存器间接访问（写 MII 0x1E，读写 MII 0x1F） |
| `yt8011_read_mmd / write_mmd` | MMD 寄存器间接访问（0x0D/0x0E 序列） |
| `yt8011_basic_mode_default_config` | 填默认配置（1000BASE-T1、Slave、RXC 2 ns 延迟、TXC delay_sel=1） |
| `yt8011_basic_mode_init` | 应用配置 + 软复位 |
| `yt8011_set_mode` | 设置速率/主从（MMD1 0x834）+ 带速率编码软复位 |
| `yt8011_set_rgmii_delay / get_rgmii_delay` | EXT 0x9001（RXC 延迟使能/TXC 延迟档位/TXC 反相） |
| `yt8011_get_status` | 解析 BMSR/GBCR/GBSR/PHYSR → link/速率/主从/local_rx_ok |
| `yt8011_wait_link_up` | 轮询直到 link up 或超时 |
| `yt8011_speed_str` | 速率转字符串 |

## 重要实现说明（都是实测易错结论）

1. **EXT/MMD 都是间接访问**：EXT 用 MII 0x1E/0x1F；MMD 用 0x0D/0x0E（0x4000|devad 表示数据相）。
   数据手册 §4 只给寄存器表，访问方法在《YT8011Ax 应用说明 V2.2》§2.1.2/§2.1.3（p.10-11）。
2. **软复位必须携带速率编码**：先写 MMD1 0x834 再写 BMCR=0x8000 软复位，速率会被复位冲回
   上电默认 1000BASE-T1（实测 MMD1 0x834 从 0x8000 回到 0x8001）。必须用 0x8140/0xA100。
3. **BMSR.LINK 是锁存位**，`yt8011_get_status()` 连读两次取当前值。
4. **0x11 PHYSR 才是运行时真值**：速率 [15:14]、主从 [13]、实时 link [10]。
5. 本驱动不依赖 `CONFIG_ENET_PHY`，可被裸机例程直接调用；若要在 lwIP 中当板级 PHY 钩子用，
   见 `samples/enet/enet_lwip_iperf/src/phy_glue.c`。

## 参考

- YT8011A_1.pdf V1.4：§3（p.38）、§4.1（p.39-42）、§4.2（p.42-57）、§4.3（p.52-57）、§4.4（p.58）
- YT8011Ax_应用说明_V2.2.pdf：§2.1.2/§2.1.3（p.10-11）、§5.2（p.25）、§5.3（p.26）、§5.4（p.27）
- 本仓库第 12 章 `docs/chapters/12-yt8011an-rgmii.md`
