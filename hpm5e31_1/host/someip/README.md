# host/someip —— SOME/IP 上位机工具（Python 自写 + C++ vsomeip）

配套板端例程 `samples/app/someip_rpc|event|tp`，书稿见 `docs/chapters/35-someip-vsomeip.md`。

| 工具 | 说明 | 依赖 |
|---|---|---|
| `someip_client.py` | 自写 SD + RPC + 事件 + TP（逐字节对照规范，便于排障/教学） | 仅 python3 标准库 |
| `vsomeip_client/` | vsomeip 3.7.6 客户端（工业级中间件互操作验证） | C++14、vsomeip3、boost、cmake/ninja |
| `run_client.sh` | 一键跑（输出同时存到 `logs/*-host-someip-*.log`） | python3 |

## 1. 网络前提

接板网卡（通常 `enp6s0`）必须有 `192.168.2.1/24`，板端 `192.168.2.100`：

```bash
ip -4 addr show enp6s0 | grep 192.168.2     # 没有就加：sudo ip addr add 192.168.2.1/24 dev enp6s0
ping -c 2 192.168.2.100
```

## 2. 自写 Python 客户端（推荐先用它）

```bash
python3 host/someip/someip_client.py --help
python3 host/someip/someip_client.py --board 192.168.2.100 core      # 板端 someip_rpc：发现+RPC+事件
python3 host/someip/someip_client.py --board 192.168.2.100 event --secs 6   # 板端 someip_event
python3 host/someip/someip_client.py --board 192.168.2.100 tp --size 4096   # 板端 someip_tp
# 固件与主机测试绑在一次持锁里（避免固件被别的 hwrun 换掉）：
bash scripts/hwrun.sh app/someip_rpc --secs 6 --send "run" \
     --post "timeout 80 bash host/someip/run_client.sh core" --lock-wait 1800
```

真实输出片段（`core`，2026-10-30 17:44:53）：

```
== SD 发现 ==
  PASS 收到组播 OfferService：service 0x1234 instance 0x0001 major 1 ttl 3 s
       endpoint option: 192.168.2.100:30501 proto 0x11（UDP=0x11）
  PASS FindService -> OfferService 0.7 ms（单播回 192.168.2.100:30490）
  PASS 周期 OfferService: 2 条 / 4.0 s，重发间隔 均值 1499.7 ms
== 方法 RPC ==
  PASS add(2,3)                     rc=E_OK value=5 rtt=0.30 ms
  PASS get_version                  'hpm5e31-someip/1.0.0'
  RTT n=50 min=0.099 avg=0.105 p50=0.105 p99=0.125 max=0.125 ms
== 事件订阅 ==
  PASS SubscribeEventgroup -> Ack ttl=3 counter=1
  counter  35 条 100.01 ms σ0.189 丢包 0 时延 0.39/0.58 ms
  adc     349 条  10.03 ms σ0.535     temperature 7 条 499.94 ms σ0.415
== 汇总：discover PASS / rpc PASS / event PASS ==
```

## 3. vsomeip 客户端（C++，工业级对照）

vsomeip 不在 Ubuntu 源里，需要源码构建（仓库外参考实现 `ref2/vsomeip`）：

```bash
sudo apt install -y g++ cmake ninja-build libboost-system-dev libboost-thread-dev \
                    libboost-filesystem-dev libboost-log-dev
cmake -S /path/to/vsomeip -B /path/to/vsomeip/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/vsomeip/build -j 16
cmake --install /path/to/vsomeip/build --prefix /path/to/vsomeip-install

cmake -S host/someip/vsomeip_client -B host/someip/vsomeip_client/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/vsomeip-install
cmake --build host/someip/vsomeip_client/build -j 16
bash host/someip/vsomeip_client/run.sh          # 板端 someip_rpc 在跑时
```

配置要点（`config/someip_client.json`）：`unicast`=192.168.2.1（必须与接板网卡一致）、
`routing`=`someip_client`（单进程自任 routing manager）、`services[].unreliable`=30501、
`service-discovery.multicast`=224.244.224.245、`port`=30490。

一键互操作脚本（含环境修复）：`bash host/someip/vsomeip_client/run_interop.sh`（板端需在跑 `someip_rpc`）。

**vsomeip 互操作的两个环境易错点（实测定位，第 34 章 U-35-1）**：

1. **主机缺 SD 组播路由**：`ip route get 224.244.224.245` 默认会走 SSH 网口的默认路由，
   vsomeip 因此不会在接板网卡上 IGMP join。修复：
   ```bash
   sudo ip route replace 224.244.224.245/32 dev enp6s0 src 192.168.2.1
   ```
   修复后 vsomeip 日志出现 `usei::set_multicast_option ... Join successful` 和
   `KPI NET4 First SD multicast message from 192.168.2.100`。
2. **板端 Offer 的 Reboot 标志不能每帧都带**：`components/app_someip` 原来每条 Offer 的
   Flags 都是 0xC0（Reboot|Unicast），vsomeip 会把每帧都当成"对端重启"，
   日志里出现 `ON_STOP_OFFER_SERVICE(0000): [1234.0001:1.1]` 后服务被删，永远到不了 AVAILABLE。
   已改成"只有重启后的第一条 Offer 带 Reboot"。
3. 自定义前缀安装时插件要能被 dlopen：`LD_LIBRARY_PATH=<prefix>/lib`
   （否则报 `Configuration module could not be loaded!`）。

## 4. 常见问题

1. `join 224.244.224.245 failed`：网卡没有 192.168.2.1 或不在同网段；
2. vsomeip 报 `Configuration module could not be loaded!`：装到自定义前缀后没设 `LD_LIBRARY_PATH`
   （`run.sh` 已加 `VSOMEIP_PREFIX/lib`）；
3. 端口冲突：vsomeip 与自写 Python 客户端都占 30490，**同一主机不能同时跑**；
4. `libvsomeip3-sd/cfg` 由 `libvsomeip3` 运行时 dlopen，CMake 里不要显式 `-lvsomeip3-sd`。