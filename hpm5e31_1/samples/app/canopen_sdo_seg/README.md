# canopen_sdo_seg —— 分段 SDO 吞吐实测（1 KB vs 4 KB）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

CAN0 从站（节点 1）+ CAN1 主站。对厂商区两个 DOMAIN 对象做**分段 SDO** upload/download：
0x2001 = 1024 B、0x2002 = 4096 B；主站用 mcycle 计时，校验数据图案，比较 1 KB 与 4 KB 的耗时与吞吐；
另测 500 次加速读（0x1017，2 B）的速率。

## 对象

| 索引 | 名称 | 类型 | 长度 | 用途 |
|---|---|---|---|---|
| 0x2000 | vendorArray1k | ARRAY(128×U64) | 1024 B | 数组对象对照（只能逐 subindex 访问） |
| 0x2001 | vendorDomain1k | DOMAIN | 1024 B | 分段传输测试 |
| 0x2002 | vendorDomain4k | DOMAIN | 4096 B | 1 KB vs 4 KB 对比 |

## 命令

```bash
bash scripts/hwrun.sh app/canopen_sdo_seg --secs 14 --lock-wait 1800    # 上电自动 run 5
# 串口 shell：run [iterations 1..20] / stat / sdo
```

## 实测（logs/20261030-173345-canopen_sdo_seg.log）

```
  对象       大小   方向     轮数  最佳耗时     吞吐        结果
  ---------- ------ -------- ---- ------------ ------------ -------
  0x2001      1024 B upload    5       73.0 ms      14027 B/s  OK
  0x2002      4096 B upload    5      291.6 ms      14045 B/s  OK
  0x2001      1024 B download  5       72.9 ms      14048 B/s  OK
  0x2002      4096 B download  5      289.2 ms      14161 B/s  OK
  段数估算: 1024 B = 147 段 x 7 B；4096 B = 586 段 x 7 B（每段 1 请求 + 1 响应）
  expedited read 0x1017 x200   PASS  200/200 次, 0.49 ms/次, 2036 次/s
=== RESULT: 6 PASS / 0 FAIL ===
```

**结论**：1 KB 与 4 KB 的分段吞吐几乎相同（14.0~14.2 KB/s），说明开销是**每段固定成本**而非数据量；
算一下总线占用：1024 B 需要 147 段 × 2 帧 = 294 个 8 字节经典帧，73 ms × 500 kbit/s ≈ 36 500 bit，
即 294 帧平均 124 bit/帧——**总线利用率接近 100%**，应用层有效吞吐 1024 B / 72.9 ms ≈ 112 kbit/s
（500 kbit/s 的 22.5%）。要更快只能减少每段帧数（block transfer）或换 CAN FD。

## 坑

1. **分段下行请求必须发到 0x600+ID**（SDO 请求），响应才从 0x580+ID 回——本移植第一版把 segment
   request 发到了 0x580+ID，从站一直等不到分段，1 s 后回 abort `0x05040000`（SDO protocol timed out）；
2. **toggle 位必须交替**，且上传/下载的 n 字段是 3 bit（bits 3..1），不是加速传输的 2 bit（bits 3..2）；
3. **OD 对象长度决定传输方式**：dataLength ≤ 4 → expedited；> 4 → 分段。VISIBLE_STRING 若加了
   `ODA_STR` 属性（长度可变）会让上传在"未指明长度"分支行为不同，测试时容易误判，本移植字符串按固定长度实现。
