# patches/ — 源码修改与补丁

本目录存放对 SDK 源码的修改（patch 形式）与说明。

## 补丁清单

| 文件 | 目标 | 说明 |
| --- | --- | --- |
| `0001-buildroot-rk3506-post-build-fast-display-idempotent.patch` | `buildroot/board/rockchip/rk3506/post-build-fast-display.sh` | 使重复构建幂等：`S05async-commit.sh` 已改名时不再报错 |
| `0002-gs_usb-6.8-backport-to-6.1.patch` | `drivers/net/can/usb/gs_usb.c` | 新版 gs_usb(6.8) 回移到 6.1：去掉 `__counted_by`、换用 6.1 rx-offload API、`U8_MAX` |
| `0003-kernel-CAN_GS_USB-select-CAN_RX_OFFLOAD.patch` | `drivers/net/can/usb/Kconfig` | 让 `CAN_GS_USB` 选中 `CAN_RX_OFFLOAD`（新版驱动依赖） |
| `0004-rk3506-6.1-lan865x-plca-core.patch` | 内核 6.1 的 UAPI/phylib/ethtool/Kconfig | 回移 PLCA（ethtool netlink + `genphy_c45_plca_*`）、10BASE-T1S link mode、OA-TC6/LAN865X 挂点 |
| `0005-dts-rk3506-lan8651-t1s.patch` | `arch/arm/boot/dts/HZ-RK3506G2_MiniEVM_TF.dts` | 启用 SPI0（**RM_IO8-11 / GPIO0_B0-B3**）+ LAN8651 节点（RESET/IRQ/15 MHz）；禁用与 SPI0 复用的 UART2/UART3 |

应用方式：
* `0001`–`0003`：`bash ../scripts/06-fix-root-build.sh` 自动应用（幂等）。
* `0004`/`0005`：`bash ../scripts/15-install-lan865x.sh` 自动应用（幂等），
  并把 `drivers/lan865x/` 下的新文件拷进内核树。

## 其它“源码修改”的位置说明

* **外设例程**：修改直接体现在 `peripherals/*/Makefile`
  （交叉编译器由 aarch64 绝对路径改为 `arm-none-linux-gnueabihf-` / buildroot 工具链）。
* **Buildroot 的 GCC 15 兼容**：用**环境变量**注入（`HOSTCC` 包装器 +
  `HOST_CFLAGS/HOST_CXXFLAGS`），不改 SDK 源码，见 `scripts/lib/common.sh`、
  `scripts/lib/host-cc-shim.sh`。
* **以 root 编译的内核模块问题**：通过 `chown root:root $SDK_DIR` 解决，
  见 `scripts/06-fix-root-build.sh`。
