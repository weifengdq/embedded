#!/usr/bin/env python3
# 生成第 51 章附录的自动表格（引脚复用 / 日志索引）
import argparse, os, pathlib, re

REPO = pathlib.Path(__file__).resolve().parent.parent
SDK = pathlib.Path(os.environ.get("HPM_SDK_BASE", "$HOME/sdk_env_v1.13.0/hpm_sdk"))
IOMUX = SDK / "soc" / "HPM5E00" / "HPM5E31" / "hpm_iomux.h"

PINS = ["PA00","PA01","PA02","PA03","PA09","PA19","PA20","PA21","PA22","PA23","PA24","PA30","PA31",
        "PB28","PB29","PC08","PC14","PC15","PD00","PD01","PD04","PD05",
        "PF02","PF03","PF04","PF05","PF06","PF07","PF08","PF09","PF10","PF11","PF12","PF13",
        "PF14","PF15","PF16","PF18","PF19","PF20","PF21","PF22","PF23","PF26"]

RE_PIN = re.compile(r"#define\s+IOC_([A-Z]{2}\d{2})_FUNC_CTL_([A-Za-z0-9_]+)\s+IOC_PAD_FUNC_CTL_ALT_SELECT_SET\((\d+)\)")

def parse_iomux():
    per = {}
    for line in IOMUX.read_text(errors="ignore").splitlines():
        m = RE_PIN.match(line)
        if m:
            per.setdefault(m.group(1), []).append((int(m.group(3)), m.group(2)))
    return per

def pins_table():
    per = parse_iomux()
    out = ["| 引脚 | 可选功能（ALT 编号：功能） |", "|---|---|"]
    for p in PINS:
        funcs = sorted(per.get(p, []))
        txt = "<br>".join("%d: %s" % (a, f) for a, f in funcs) if funcs else "（无）"
        out.append("| **%s** | %s |" % (p, txt))
    return "\n".join(out)

def log_table():
    logs = sorted((REPO / "logs").glob("*.log"))
    rows = ["| 日志文件 | 时间 | 例程 | 大小 |", "|---|---|---|---|"]
    for f in logs[-80:]:
        m = re.match(r"(\d{8})-(\d{6})-(.+)\.log", f.name)
        if m:
            g = m.groups()
            ts = "%s-%s-%s %s:%s:%s" % (g[0][:4], g[0][4:6], g[0][6:], g[1][:2], g[1][2:4], g[1][4:])
            rows.append("| %s | %s | %s | %d B |" % (f.name, ts, g[2], f.stat().st_size))
        else:
            rows.append("| %s | - | - | %d B |" % (f.name, f.stat().st_size))
    return "\n".join(rows)

def replace_between(text, tag, body):
    b = "<!-- BEGIN:%s -->" % tag
    e = "<!-- END:%s -->" % tag
    return re.sub(re.escape(b) + r".*?" + re.escape(e), b + "\n" + body + "\n" + e, text, flags=re.S)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    a = ap.parse_args()
    ch = REPO / "docs" / "chapters" / "51-appendix.md"
    pins, logs = pins_table(), log_table()
    if a.write and ch.exists():
        t = ch.read_text()
        t = replace_between(t, "PINS", pins)
        t = replace_between(t, "LOGS", logs)
        ch.write_text(t)
        print("updated", ch)
    else:
        print(pins[:800]); print("..."); print(logs[:600])

if __name__ == "__main__":
    main()