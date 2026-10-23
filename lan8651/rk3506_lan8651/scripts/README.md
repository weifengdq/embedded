# scripts/ — 自动化脚本

所有脚本默认工作目录/路径由 `scripts/lib/common.sh` 统一给出，可用环境变量覆盖：

```bash
source scripts/04-setup-env.sh     # 交互/执行前先加载
```

| 脚本 | 作用 | 备注 |
| --- | --- | --- |
| `lib/common.sh` | 公共变量、日志、GCC15 兼容设置 | 被其它脚本 source |
| `lib/host-cc-shim.sh` | Host C 编译器包装（强制 `-std=gnu17`） | 由 common.sh 部署到 `$WORK_DIR/bin/rk-host-cc` |
| `01-install-host-deps.sh` | 安装主机编译依赖 | 需 root；逐个安装、失败不中断 |
| `02-install-python2.sh` | 安装 Python 2.7（U-Boot 需要） | 需 root；解包 jammy 的 .deb |
| `03-extract-sources.sh` | 解压 SDK 与交叉工具链到英文路径 | 幂等 |
| `04-setup-env.sh` | 配置 `SDK_DIR`/`CROSS_COMPILE`/`PATH` 等 | `source` 使用 |
| `05-build-all.sh` | 一键整体编译 | defconfig→uboot→kernel→rootfs→firmware→updateimg |
| `06-fix-root-build.sh` | 修复“以 root 编译”的内核模块安装问题 + 幂等补丁 | **构建前会自动跑**；见 `docs/07` 2.2.1 |
| `07-apply-network-config.sh` | 应用网络配置：eth0→DHCP、PATH 补全、加 iw/curl | 一次性/幂等；改完需重编 rootfs |
| `08-enable-socketcan.sh` | 启用 SocketCAN 内核模块（vcan/can/j1939/isotp/gs_usb…）+ 别名 | 改完需重编 kernel/rootfs |
| `09-install-gs_usb.sh` | 安装适配到 6.1 的新版 gs_usb（N32H765 8路CANFD/candleLight）+ Kconfig | 改完需重编 kernel/rootfs |
| `10-build-uboot.sh` | 编译 U-Boot | |
| `11-build-kernel.sh` | 编译 Kernel | |
| `12-build-rootfs.sh` | 编译 Buildroot rootfs | 生成后请确认 `target/lib/modules/` 非空 |
| `13-pack-firmware.sh` | 打包 firmware / update.img | |
| `14-clean.sh` | 清理（cleanall/clean-kernel/...） | |
| `15-install-lan865x.sh` | 把 LAN8651（OA-TC6/LAN865x/PLCA）回移装进 6.1 内核树 | 幂等；需先 `source 04` |
| `16-build-ethtool.sh` | 交叉编译 ethtool 6.11（含 PLCA 命令）到 arm32 | 产物给板端 `/userdata/ethtool` |
| `17-apply-lan8651-network.sh` | 写入 `S39rename-t1s`（LAN8651→t1s0）+ `interfaces` | 最终镜像用；幂等 |
| `20-build-demos.sh` | 交叉编译 `peripherals/` 例程 | |
| `30-make-tf-card.sh` | Linux 下制作并写入 TF 启动卡（dd + 重建 GPT + 撑满 rootfs） | `sudo`；`-y` 免确认；会写块设备 |
| `31-deploy-kernel-ssh.sh` | 通过 SSH 增量部署新 FIT（p3）+ 全部内核模块 | 无需读卡器；自动备份旧模块 |
| `40-deploy-modules.sh` | 通过**网络**把内核模块部署到已启动板卡 | 临时/在线修复用 |
| `fetch-yuque-docs.sh` | 抓取语雀目录 | 正文需登录 |

## 典型流程

```bash
sudo bash scripts/01-install-host-deps.sh
sudo bash scripts/02-install-python2.sh
bash      scripts/03-extract-sources.sh
source    scripts/04-setup-env.sh
bash      scripts/05-build-all.sh
bash      scripts/20-build-demos.sh
sudo bash scripts/30-make-tf-card.sh /dev/sdX
```

> 注意：`05-build-all.sh` 通过 `common.sh` 自动应用 GCC15 兼容设置，
> 直接手工运行 `./build.sh rootfs` 时需先 `source scripts/04-setup-env.sh`
> （或 `source scripts/lib/common.sh`），否则可能编译失败（见 `docs/04-build-guide.md` 第 6 节）。
