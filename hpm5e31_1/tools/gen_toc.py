#!/usr/bin/env python3
# 生成 docs/00-目录.md（章节清单 + 状态按文件是否存在/行数显示）
# 2026-11-02 R8：章号与标题都从 docs/chapters 实际文件解析（章号 = 文件名前两位，
# 排序连续；标题 = 各章 H1 去掉 "第 N 章"），负责人/部分分组用下面的映射表（按文件名键）。
# 这样重编号或改章标题后，本脚本一跑，目录自动同步，不会与正文漂移。
import pathlib
import re

R = pathlib.Path(__file__).resolve().parent.parent
D = R / "docs" / "chapters"
BT = chr(96)

PARTS = [
    ("第一部分 基础与工具", [
        ("00-preface.md", "Lead"),
        ("01-chip-and-board.md", "Lead"),
        ("02-toolchain-build-flash.md", "Lead"),
        ("03-letter-shell.md", "tp-bus"),
    ]),
    ("第二部分 外设驱动与上板实测", [
        ("04-gpio-ioc.md", "tp-bus"),
        ("05-uart.md", "tp-bus"),
        ("06-i2c.md", "tp-bus"),
        ("07-spi.md", "tp-bus"),
        ("08-canfd.md", "tp-can"),
        ("09-canfd-perf.md", "tp-can"),
        ("10-lan8651-t1s.md", "tp-net"),
        ("11-lan8651-plca-lwip.md", "tp-net"),
        ("12-yt8011an-rgmii.md", "tp-net"),
        ("13-lwip-iperf.md", "tp-net"),
        ("14-pwmv2-hrpwm.md", "tp-timer"),
        ("15-gptmr.md", "tp-timer"),
        ("16-qei-qeo.md", "tp-timer"),
        ("17-sdm-plb-trgm.md", "tp-timer"),
        ("18-adc16.md", "tp-sys"),
        ("19-dma.md", "tp-sys"),
        ("20-usb-device.md", "tp-sys"),
        ("21-xpi-flash-boot.md", "tp-sys"),
        ("22-wdg-power-temp.md", "tp-sys"),
    ]),
    ("第三部分 系统、性能与可靠性", [
        ("23-benchmarks.md", "tp-sys"),
        ("24-bus-throughput.md", "tp-sys"),
        ("25-reliability.md", "Lead"),
        ("26-registers-errata.md", "Lead"),
        ("27-comparison-reading.md", "Lead"),
    ]),
    ("第四部分 应用层协议与实战", [
        ("28-app-bench.md", "tp-can"),
        ("29-isotp.md", "tp-app1"),
        ("30-uds.md", "tp-app1"),
        ("31-doip.md", "tp-app3"),
        ("32-j1939.md", "tp-app1"),
        ("33-canopen.md", "tp-app1"),
        ("34-xcp-calibration.md", "tp-app1"),
        ("35-someip-vsomeip.md", "tp-app2"),
        ("36-xrce-dds-ros2.md", "tp-app2"),
        ("37-mqtt.md", "tp-app3"),
        ("38-ptp-time-sync.md", "tp-app2"),
        ("39-dlt-vss-canlog.md", "tp-app2"),
        ("40-app-comparison.md", "Lead"),
        ("41-gs-usb-candlelight.md", "tp-gs"),
    ]),
    ("第五部分 AUTOSAR MCAL（第六轮起，第七轮补 Eth）", [
        ("42-mcal-overview.md", "Lead"),
        ("43-mcal-infra.md", "Lead"),
        ("44-mcal-core.md", "mcal-core"),
        ("45-mcal-timer.md", "mcal-timer"),
        ("46-mcal-io.md", "mcal-io"),
        ("47-mcal-can.md", "mcal-can"),
        ("48-mcal-mem-crc.md", "mcal-sys"),
        ("49-mcal-eth.md", "eth-core"),
        ("50-mcal-compliance.md", "mcal-ref"),
    ]),
    ("第六部分 附录", [
        ("51-appendix.md", "Lead"),
    ]),
]

RE_H1 = re.compile(r"^#\s*第\s*(\d{1,2})\s*章\s*(.+?)\s*$")


def chapter_no(fn):
    m = re.match(r"^(\d{2})-", fn)
    return int(m.group(1)) if m else 0


def chapter_title(fn):
    p = D / fn
    if not p.exists():
        return "（待写）"
    for line in p.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("# "):
            m = RE_H1.match(line)
            if m:
                return m.group(2).strip()
            return line[2:].strip()
    return "（缺一级标题）"


def main():
    lines = []
    a = lines.append
    a("# 《HPM5E31 实战手册》目录与分工")
    a("")
    a("> 由 " + BT + "tools/gen_toc.py" + BT + " 生成（章号与标题从 docs/chapters 各章 H1 解析，状态按文件行数显示），**不要手改**。")
    a("> 最终由 " + BT + "bash tools/build_pdf.sh" + BT + " 合成 " + BT + "docs/build/HPM5E31-Practical-Guide-v<版本>.pdf" + BT + "。")
    a(">")
    a("> 阅读顺序按下面的部分分组；附录放在全书最后。")
    a("")

    present = 0
    total = 0
    listed = set()
    for part, items in PARTS:
        a("## " + part)
        a("")
        a("| 章 | 标题 | 文件 | 作者 | 状态 |")
        a("|---|---|---|---|---|")
        for fn, owner in items:
            total += 1
            listed.add(fn)
            no = chapter_no(fn)
            title = chapter_title(fn)
            p = D / fn
            if p.exists():
                n = len(p.read_text(encoding="utf-8", errors="ignore").splitlines())
                st = "已交付（%d 行）" % n
                present += 1
            else:
                st = "待写"
            a("| %02d | %s | %s | %s | %s |" % (no, title, fn, "作者", st))
        a("")

    extra = []
    for p in sorted(D.glob("*.md")):
        if p.name not in listed:
            extra.append(p.name)
    if extra:
        a("## 未登记章节（请补进 tools/gen_toc.py 的 PARTS）")
        a("")
        for fn in extra:
            a("- %s（%d 行）" % (fn, len((D / fn).read_text(encoding="utf-8", errors="ignore").splitlines())))
        a("")

    a("已完成章节：%d / %d。" % (present, total))
    a("")
    a("## 图目录")
    a("")
    a(BT + "docs/figures/<章号>/..." + BT + "；生成脚本 " + BT + "tools/figs/<name>.py|dot" + BT + "。")
    a("")
    a("## 版本规则")
    a("")
    a("- 每次 PDF 合成写 " + BT + "docs/VERSION" + BT + "（语义化版本），PDF 内封与页脚都带版本与日期；")
    a("- 章节大改 → minor+1；修错别字/数据 → patch+1。")
    (R / "docs" / "00-目录.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("toc generated: %d/%d chapters present%s"
          % (present, total, ("，%d 个未登记" % len(extra)) if extra else ""))


if __name__ == "__main__":
    main()

