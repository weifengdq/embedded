#!/usr/bin/env python3
"""tools/figs/net2_fig_13_1_lwip_layers.py —— 图 13-1：本板 lwIP 移植的分层与文件对应关系

数据来源：hpm_sdk v1.13.0 的 samples/lwip/ports/baremetal、samples/lwip/common/single 源码，
          以及本仓库 samples/enet/enet_lwip_iperf 的 CMakeLists（实际编译的文件清单）。
输出：docs/figures/13/fig-13-1-lwip-layers.png
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from _common import render_dot  # noqa: E402

DOT = r"""
digraph lwip_layers {
  graph [rankdir=TB, fontname="Helvetica", nodesep=0.22, ranksep=0.30, bgcolor="white"];
  node  [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=9,
         fillcolor="#F6F8FA", color="#57606A", penwidth=0.9, width=4.6];
  edge  [color="#57606A", penwidth=0.9, arrowsize=0.7];

  app   [label="应用层\nsrc/main.c：shell 命令 ip/link/iserver/iclient/istop/mode\nlwiperf（iperf2 协议，端口 5001）", fillcolor="#DDF4FF", color="#1F6FEB"];
  api   [label="lwIP 核心（SDK middleware/lwip）\nlwip_init、netif_add、etharp、ip4/tcp/udp/icmp、sys_check_timeouts"];
  glue  [label="适配层（SDK samples/lwip，未修改）\nports/baremetal/ethernetif.c：ethernetif_init / ethernetif_input\nports/baremetal/arch/sys_arch.c：sys_now / sys_timer_callback（1 ms tick）\ncommon/single/common.c：enet_init / TX/RX 零拷贝 / enet_common_handler\ncommon/single/netconf.c：静态 IP netif_config"];
  hooks [label="本仓库补充的薄适配（samples/enet/enet_lwip_iperf/src/phy_glue.c）\nboard_init_enet_phy()：调用 yt8011 驱动完成 PHY 初始化\nboard_get_enet_phy_status()：PHY 状态 → enet_phy_status_t\nlwip_enet_phy_adaptive_poll()：链路变化 → netif_set_link_up/down", fillcolor="#FFF8C5", color="#D29922"];
  drv   [label="MCU 外设驱动\nhpm_sdk/drivers/src/hpm_enet_drv.c（MAC/DMA 描述符）\ncomponents/enet_phy_yt8011（MDIO + YT8011AN 寄存器）", fillcolor="#DAFBE1", color="#2EA043"];
  hw    [label="硬件\nHPM5E31 ENET0 (RGMII) ── YT8011AN ── 1000BASE-T1 单对线", fillcolor="#FFEBE9", color="#DA3633"];

  app -> api -> glue -> hooks -> drv -> hw;
}
"""


def main():
    render_dot(DOT, 13, "fig-13-1-lwip-layers")


if __name__ == "__main__":
    main()
