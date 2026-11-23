#!/usr/bin/env python3
"""图 7-3 / 图 6-3：SPI1<->SPI3、I2C2<->I2C3 板内互连与信号流（R5 新增）

生成：python3 tools/figs/app07_spi_i2c_crosslink.py
产物：docs/figures/07-spi/fig-07-3-spi1-spi3-crosslink.png
      docs/figures/06-i2c/fig-06-3-i2c2-i2c3-crosslink.png
"""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SPI = r'''
digraph spi_cross {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=10.5, bgcolor="white", pad=0.3,
         nodesep=0.20, ranksep=1.15, splines=line];
  node  [fontname="Noto Sans CJK SC", fontsize=9.5, shape=box, style="rounded,filled",
         fillcolor="#eef4f8", margin=0.12, width=2.15];
  edge  [fontname="Noto Sans CJK SC", fontsize=8.8, color="#33506b", penwidth=1.2];

  m_sclk [label="SPI1 主机 · SCLK  PC10\n（80 MHz 源时钟分频）"];
  m_cs   [label="SPI1 主机 · CS    PC11"];
  m_miso [label="SPI1 主机 · MISO  PC12"];
  m_mosi [label="SPI1 主机 · MOSI  PC13"];
  s_sclk [label="SPI3 从机 · SCLK  PA26"];
  s_cs   [label="SPI3 从机 · CS    PA27"];
  s_miso [label="SPI3 从机 · MISO  PA28"];
  s_mosi [label="SPI3 从机 · MOSI  PA29"];

  m_sclk -> s_sclk [label="时钟"];
  m_cs   -> s_cs   [label="片选"];
  m_mosi -> s_mosi [label="MOSI-MOSI 同名相连"];
  s_miso -> m_miso [label="MISO-MISO 同名相连"];

  { rank=same; m_mosi; m_miso; m_cs; m_sclk; }
  { rank=same; s_mosi; s_miso; s_cs; s_sclk; }
}
'''

I2C = r'''
digraph i2c_cross {
  rankdir=LR;
  graph [fontname="Noto Sans CJK SC", fontsize=10.5, bgcolor="white", pad=0.3,
         nodesep=0.30, ranksep=1.15, splines=line];
  node  [fontname="Noto Sans CJK SC", fontsize=9.5, shape=box, style="rounded,filled",
         fillcolor="#eef4f8", margin=0.12, width=2.35];
  edge  [fontname="Noto Sans CJK SC", fontsize=8.8, color="#33506b", penwidth=1.2];

  m_scl [label="I2C2 主机 · SCL  PD02\n（开漏 + 内部上拉）"];
  m_sda [label="I2C2 主机 · SDA  PD03"];
  s_scl [label="I2C3 从机 · SCL  PD07\n（地址 0x50）"];
  s_sda [label="I2C3 从机 · SDA  PD06"];

  m_scl -> s_scl [dir=both, label="SCL-SCL"];
  m_sda -> s_sda [dir=both, label="SDA-SDA（地址/数据双向）"];

  { rank=same; m_sda; m_scl; }
  { rank=same; s_sda; s_scl; }
}
'''

def render(dot_src, out_rel, name):
    dot_path = Path("/tmp") / (name + ".dot")
    dot_path.write_text(dot_src, encoding="utf-8")
    out = ROOT / out_rel
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(["dot", "-Tpng", "-Gdpi=300", str(dot_path), "-o", str(out)])
    print("ok", out_rel)

render(SPI, "docs/figures/07-spi/fig-07-3-spi1-spi3-crosslink.png", "fig_7_3")
render(I2C, "docs/figures/06-i2c/fig-06-3-i2c2-i2c3-crosslink.png", "fig_6_3")
