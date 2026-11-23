# j1939_tp —— J1939-21 传输协议实测（BAM / RTS-CTS）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

CAN0 = 发送节点（SA=0x00），CAN1 = 接收节点（SA=0xF9），250 kbit/s 板内双节点。
对 PGN 0xEF00（广播/BAM）与 0xEF01（点对点/RTS-CTS）传输 100/500 字节，
接收方按序号重组并用 CRC32 + 内容模板校验。

## 命令

```bash
bash scripts/hwrun.sh app/j1939_tp --secs 24 --lock-wait 1800
# shell: bench | bam <n> | rts <n> | rev <n> | stat | info
```

## 上电自测（7 项断言）

1. BAM 100 B；2. BAM 500 B；3. RTS/CTS 100 B；4. RTS/CTS 500 B；
5. 反向 RTS/CTS 500 B（SA 0xF9 → 0x00）；6. 丢包/超时（接收方停摆 2.6 s，整帧传输期间不轮询）；
7. 恢复测试（停摆后重新传 100 B 正常）。

## 实测（logs/20261030-170808-j1939_tp.log）

```
BAM  100B: 15 pkts, time=700351 us, 142.8 B/s, gap min/avg/max=49268/49936/50012 us
BAM  500B: 72 pkts, time=3550255 us, 140.8 B/s, gap min/avg/max=49209/49987/50024 us
RTS/CTS 100B: 15 pkts, time=15914 us, 6283.8 B/s, gap min/avg/max=800/973/1012 us
RTS/CTS 500B: 72 pkts, time=72802 us, 6867.9 B/s, gap min/avg/max=664/993/1120 us
RTS/CTS 500B (reverse): 72797 us, 6868.4 B/s
stalled receiver: not reassembled (done_rx=0), timeout/err seen=1
recovery after drop: BAM 100B OK
=== RESULT: PASS (7 PASS / 0 FAIL) ===
```

## 坑

- **接收方收满 CTS 窗口必须续发 CTS**，否则发送方卡 T3（本实现修好后 500 B 才 PASS）；
- BAM 是硬性 50 ms/包，500 B 要 3.55 s；RTS/CTS 才能跑满带宽（约 49 倍）；
- 接收方停摆超过 T1=750 ms，会话会因序号不连续被丢弃，必须支持"丢弃 + 下次重传"；
- 上一次传输的 EOMA/CTS 可能滞留在对端 RX FIFO，开始新传输前要 drain，否则会误杀新会话
  （测试代码里 `drain_ms(5)` 就是干这个的）。
