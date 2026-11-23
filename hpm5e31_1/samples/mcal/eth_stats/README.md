# eth_stats —— AUTOSAR Eth 计数器口径与 DET 错误路径

## 1. 这是什么

把 SWS 的四个统计 API（Eth_GetCounterValues / GetRxStats / GetTxStats / GetTxErrorCounterValues）与 DET 开发错误路径跑一遍，给出"哪个计数在什么情况下涨"的证据，并用模式切换验证 SWS_Eth_00280/00281 的缓冲释放语义。

## 2. 硬件与接线

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1，ENET0 + YT8011AN |
| 依赖 | 无主机侧脚本（纯板端自证） |

## 3. 编译与运行

```bash
bash scripts/build.sh samples/mcal/eth_stats
```

```bash
bash scripts/hwrun.sh mcal/eth_stats --secs 25 --send "stat;stat det;stat busy;stat types;stat"
```

## 4. 命令表

| 命令 | 作用 |
|---|---|
| stat | 打印四组统计（tx/rx/错误/长度分桶）与 Det 计数 |
| stat snap | 记快照 |
| stat delta | 打印相对快照的增量 |
| stat det | 连续调 14 个非法 API，打印 Det 增量与最后一条错误 |
| stat busy | 占满 8 个 TX 缓冲制造 BUFREQ_E_BUSY，再用模式切换释放 |
| stat types | 按 1/18/46/100/500/1000/1500 B 发送，验证最小帧补齐与长度分桶 |

## 5. 实测现象与日志

- 日志：`logs/<时间戳>-eth_stats.log`（hwrun 生成；正文第 49 章表 49-11/49-14 引用）
- 复跑：`bash scripts/hwrun.sh mcal/eth_stats --secs 25 --send "stat;stat det;stat busy;stat types;stat"`

## 6. 关键点与坑

1. 统计是**软件计数 + 描述符错误位**的组合，不是 MMC 硬件计数器；口径按 SWS_Eth_91002/91003/91004 的字段含义映射（映射表见第 49 章表 49-11）。
2. `stat det` 会故意制造 14 个 Det 错误，**这是预期行为**，不要当故障。
3. `stat busy` 里"只锁定不提交"的缓冲，只有模式切换（DOWN→ACTIVE）才会按 SWS_Eth_00280/00281 释放。
