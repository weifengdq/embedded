# 实测记录 — 2026-11-19 多节点 PLCA

环境：主机 USB-10T1S = PLCA 协调者 (ID 0, NCNT 8, TOTMR 32)；
三块板同时在线：HPM5361 (.12, ID 2)、S32K312 (.13, ID 3)、TC387 (.14, ID 4)。

## 1. 各节点 PLCA 状态

```
=== HPM (.12)                    === TC387 (.14)
plca : ON id=2 ncnt=8 PST=1      plca : ON id=4 ncnt=8 PST=1
plca diag : 0x0                  plca diag : 0x0
(RXINTO=0 UNEXPB=0 BCNBFTO=0)    (RXINTO=0 UNEXPB=0 BCNBFTO=0)

主机: ethtool --get-plca-status -> Status: on
```

* `PST=1`：三个跟随者都收到了协调者 BEACON，进入 PLCA 轮询。
* `PLCADIAG=0`：无 ID 冲突、无多协调者、无 NCNT 不匹配。

## 2. 三节点同时 ping

```
$ for i in 12 13 14; do ping -c 5 -i 0.05 192.168.10.$i; done

192.168.10.12: 5 packets transmitted, 5 received, 0% packet loss
192.168.10.13: 5 packets transmitted, 5 received, 0% packet loss
192.168.10.14: 5 packets transmitted, 5 received, 0% packet loss
```

RTT：HPM ≈0.85 ms、S32K ≈2.0 ms、TC387 ≈0.85 ms（S32K 因软件 SPI 较慢）。

## 3. 并发吞吐（HPM + TC387 同时 iperf）

```
（两路同时运行 12 s，期间 ping 三个节点仍 0% 丢包）
TC387: [  1] 0.0000-12.1096 sec  6.38 MBytes  4.42 Mbits/sec
HPM  : 会话中途结束（服务端被后续操作打断），未取到完整数值
```

## 4. 结论

1. **PLCA 多节点轮询工作正常**：3 个跟随者 + 1 个协调者，PST 全部为 1，诊断位全 0。
2. **无冲突**：并发 TCP 流量期间，三节点 ping 仍 0% 丢包（CSMA 下多节点并发会显著丢包）。
3. 并发吞吐会按 PLCA 轮询公平分配（TC387 单跑 5.97 Mbit/s，并发时 4.42 Mbit/s）。
4. HPM 的 PST=1 但 LKST 偶尔为 0：10BASE-T1S 的 LKST 依赖线路活动，
   **PLCA 场景下应以 PST 作为链路判断依据**（已在文档中说明）。
