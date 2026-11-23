#!/usr/bin/env python3
"""从 HPM SDK 生成 HPM5E31 外设基地址表与中断号表（供第 26/41 章引用）。
用法: python3 tools/gen_soc_tables.py [--sdk <hpm_sdk>] [--out docs/generated]
"""
import argparse, os, re, pathlib, datetime

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sdk', default=os.environ.get('HPM_SDK_BASE', '$HOME/sdk_env_v1.13.0/hpm_sdk'))
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    repo = pathlib.Path(__file__).resolve().parent.parent
    out = pathlib.Path(a.out) if a.out else repo / 'docs' / 'generated'
    out.mkdir(parents=True, exist_ok=True)
    soc = pathlib.Path(a.sdk) / 'soc' / 'HPM5E00' / 'HPM5E31'
    ip = (soc / 'hpm_soc_ip.h').read_text(errors='ignore')
    irq = (soc / 'hpm_soc_irq.h').read_text(errors='ignore')

    periphs = []
    for m in re.finditer(r'^#define\s+(HPM_[A-Za-z0-9_]+)_BASE\s+\(?\s*(0x[0-9A-Fa-f]+)UL', ip, re.M):
        periphs.append((m.group(1), int(m.group(2), 16)))
    inst = {}
    for name, addr in periphs:
        mm = re.match(r'HPM_([A-Za-z_]+?)(\d+)$', name)
        if mm:
            inst.setdefault(mm.group(1), []).append((int(mm.group(2)), addr))
    irqs = []
    for m in re.finditer(r'^#define\s+IRQn_([A-Za-z0-9_]+)\s+(\d+)', irq, re.M):
        irqs.append((m.group(1), int(m.group(2))))

    lines = []
    lines.append('# HPM5E31 外设基地址与中断号速查（自动生成）\n')
    lines.append('> 由 `tools/gen_soc_tables.py` 从 HPM SDK v1.13.0 `soc/HPM5E00/HPM5E31/hpm_soc_ip.h`、')
    lines.append('> `hpm_soc_irq.h` 自动生成，**不要手改**。生成时间：%s\n' % datetime.datetime.now().strftime('%Y-%m-%d %H:%M'))
    lines.append('## 1. 外设实例与基地址\n')
    lines.append('| 外设 | 实例数 | 基地址 |')
    lines.append('|---|---|---|')
    for base, lst in sorted(inst.items()):
        lst.sort()
        addrs = ', '.join('0x%08X' % x[1] for x in lst)
        lines.append('| %s | %d | %s |' % (base, len(lst), addrs))
    lines.append('')
    lines.append('## 2. 非编号外设基地址（DMA/PLIC/MBX 等）\n')
    lines.append('| 名称 | 基地址 |')
    lines.append('|---|---|')
    for name, addr in sorted(periphs):
        if not re.match(r'HPM_([A-Za-z_]+?)\d+$', name):
            lines.append('| %s | 0x%08X |' % (name, addr))
    lines.append('')
    lines.append('## 3. 中断号（IRQn）\n')
    lines.append('| IRQn | 编号 |')
    lines.append('|---|---|')
    for name, n in sorted(irqs, key=lambda x: x[1]):
        lines.append('| IRQn_%s | %d |' % (name, n))
    lines.append('')
    p = out / 'soc-peripherals.md'
    p.write_text('\n'.join(lines))
    print('generated', p, len(lines), 'lines')

if __name__ == '__main__':
    main()
