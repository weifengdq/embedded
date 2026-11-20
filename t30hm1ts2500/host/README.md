# Ubuntu 主机侧 (USB-10T1S)

硬件: **EVB-LAN8670-USB-D**（K2L GmbH，USB ID `184f:0051`，MAC `00:1e:c0:d1:c3:37`）。
Linux 通过 `smsc95xx` + `microchip_t1s` PHY 驱动工作，PLCA 由 `ethtool` 的
`--get/set-plca-cfg`（Linux 6.9+ / ethtool 6.19）配置。

## 一键配置

```bash
sudo ./setup_usb10t1s.sh          # 自动找网卡, 配 IP 192.168.10.10/24, 开启 PLCA 协调者
./plca.sh status                  # 查看 PLCA 状态
```

重启后需重新执行（脚本幂等，可写入 systemd/rc.local）。

## 验证

```bash
ping -c 3 192.168.10.11
ethtool --get-plca-status $(ip -br link | awk '/00:1e:c0/{print $1}')
ip -s link show enx001ec0d1c337
```

## 说明

* 该适配器的 PLCA 配置在 PHY (LAN8670) 内，通过 ethtool netlink PLCA API 访问。
* `node-cnt` 必须与所有节点一致（本仓库统一 8）。
* 若 `--set-plca-cfg` 报 `Operation not permitted`，请用 root/sudo。
* 该 USB 网卡由 USB 供电，重启/重插后 PLCA 配置会丢失。
