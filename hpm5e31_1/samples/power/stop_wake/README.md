# stop_wake —— STOP 模式唤醒与 Errata E00042 尝试

## 功能

按 SDK `samples/power_mode_switch` 的 `enter_stop_mode` 流程进入 STOP：
`sysctl_set_cpu0_lp_retention()` + `sysctl_set_cpu0_lp_mode(trigger_system_lp)` + `WFI()`，
唤醒源用 GPTMR1 的 10 ms reload 中断，并预先武装 3 s EWDG 兜底（卡死 → 复位留证据）。

三组配置各 100 次：

| 配置 | retention | RESOURCE[AXIS].MODE |
|---|---|---|
| c1_ret3F | 0x3F（SOC/CPU0 全保留 + XTAL + PLL0） | auto |
| c2_ret37 | 0x37（bit3=0，CPU0 外设/寄存器掉电 ＝ E00042 条件） | auto |
| c3_ret37ax | 0x37 | force_on（勘误规避） |

## 实测结论（2026-11-02）

**未通过**：c1 第一次 WFI 就没返回，3 s 后 EWDG 复位；重启打印 `BOOT_RESET flag=0x00010000`。
补上 SDK 的 `pcfg_dcdc_set_lp_current_limit()` + `pcfg_dcdc_set_current_hys_range()`（`prepare_soc_low_power` 的内容）
后重烧仍然卡死。

推断（未证实）：STOP 模式下可唤醒的中断可能只有 WKUP 域（SDK 示例只用 `IRQn_DGO_PAD_WAKEUP`），GPTMR 不在其中。
下一步把唤醒源换成 WKUP 引脚中断再跑。日志：`logs/20261102-124252-stop_wake-r5d.log`。

## 命令

| 命令 | 说明 |
|---|---|
| `stopinfo` | 打印 BOOT_RESET / GPTMR 频率 / 当前 retention |
| `stopwake [iters]` | 三组配置各跑 iters 次，统计成功率与唤醒延迟 |
