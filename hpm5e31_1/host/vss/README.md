# host/vss —— VSS 信号解码

```bash
# Bench B（板端 vss_signals + gw on）
$HOME/pyenv/bin/python host/vss/run_vss.sh
# 或手工
$HOME/pyenv/bin/python host/vss/vss_decode.py --channel vcan0 --duration 3 --json-out /tmp/vss.json
# 离线（candump 文本）
$HOME/pyenv/bin/python host/vss/vss_decode.py --file /tmp/candump.txt
```

- 映射表在 `vss_decode.py` 的 `MAP`（ID/起始位/长度/因子/偏移/单位/VSS 路径），与固件一致；
- `--vss-json` 默认 `ref2/vehicle_signal_specification/docs-gen/static/data/outputplus.json`，
  用于校验 VSS 路径叶子存在；
- 退出码 0 = 覆盖 6/6 信号、范围检查 PASS、路径校验 PASS、帧数 ≥ `--min-frames`；
- 实测见 `docs/chapters/39-dlt-vss-canlog.md`。
