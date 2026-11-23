#!/usr/bin/env python3
"""生成交付状态：docs/generated/status.md（例程级）与 docs/generated/matrix.md（例程<->章节<->日志矩阵）。
用法: python3 tools/gen_status.py
"""
import pathlib, re

REPO = pathlib.Path(__file__).resolve().parent.parent
MAP = {"gpio": "04", "uart": "05", "i2c": "06", "spi": "07", "can": "08/09",
       "lan8651": "10/11", "enet": "12/13", "pwm": "14", "gptmr": "15", "qei": "16",
       "plb": "17", "sdm": "17", "adc": "18", "dma": "19", "usb": "20", "flash": "21",
       "power": "22", "bench": "23/24", "reliability": "25", "shell": "03", "hello_world": "02",
       "mcal": "42~49", "app": "29~40"}

def collect():
    logs = {}
    for f in sorted((REPO / "logs").glob("*.log")):
        m = re.match(r"\d{8}-\d{6}-(.+)\.log$", f.name)
        if m:
            logs[m.group(1)] = f.name
    samples = []
    for d in sorted((REPO / "samples").glob("*/*")):
        if not d.is_dir() or d.name.startswith("_") or d.name == "src":
            continue
        samples.append((d.parent.name, d.name, d))
    return logs, samples

def main():
    logs, samples = collect()
    chapters = sorted((REPO / "docs" / "chapters").glob("*.md"))
    out = REPO / "docs" / "generated"
    out.mkdir(parents=True, exist_ok=True)

    rows = ["# 例程与实测状态（自动生成）", "",
            "> 由 `tools/gen_status.py` 生成，勿手改。", "",
            "| 类别 | 例程 | 章节 | README | 最近实测日志 |", "|---|---|---|---|---|"]
    n_readme = n_log = 0
    for cat, name, d in samples:
        has = "yes" if (d / "README.md").exists() else "-"
        if has == "yes":
            n_readme += 1
        lg = logs.get(name, "-")
        if lg != "-":
            n_log += 1
        rows.append("| %s | `samples/%s/%s` | %s | %s | %s |" % (cat, cat, name, MAP.get(cat, "-"), has, lg))
    rows += ["", "汇总：例程 %d 个，有 README %d 个，有实测日志 %d 个，章节 %d 个。" %
             (len(samples), n_readme, n_log, len(chapters))]
    (out / "status.md").write_text("\n".join(rows))

    m = ["# 例程 <-> 章节 <-> 实测日志 对照矩阵（自动生成）", "",
         "> 由 `tools/gen_status.py` 生成，勿手改；日志在 `logs/` 下。", "",
         "| 类别 | 例程 | 章节 | README | 最近实测日志 |", "|---|---|---|---|---|"]
    for cat, name, d in samples:
        has = "yes" if (d / "README.md").exists() else "-"
        m.append("| %s | `samples/%s/%s` | %s | %s | %s |" % (cat, cat, name, MAP.get(cat, "-"), has, logs.get(name, "-")))
    m += ["", "> 汇总：例程 %d 个，有 README %d 个，有实测日志 %d 个。" % (len(samples), n_readme, n_log)]
    (out / "matrix.md").write_text("\n".join(m))
    print("generated docs/generated/status.md and matrix.md")

if __name__ == "__main__":
    main()