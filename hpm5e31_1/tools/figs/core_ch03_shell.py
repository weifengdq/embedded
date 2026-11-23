#!/usr/bin/env python3
"""第 3 章 letter-shell 插图：图 3-1 数据流、图 3-2 命令注册流程。

运行：cd hpm5e31_1 && python3 tools/figs/core_ch03_shell.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import render_dot, setup, save, COLORS, new_fig, style_axes
import matplotlib.pyplot as plt


def fig_3_1():
    dot = r"""
digraph shell_flow {
  graph [rankdir=LR, fontname="Noto Sans CJK SC", fontsize=12, nodesep=0.30, ranksep=0.45, bgcolor="white", pad=0.2];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=11, color="#57606A", fillcolor="#F6F8FA"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A"];

  pc   [label="PC 串口终端\n921600 8N1", shape=box, fillcolor="#DBEAFE"];
  uart [label="UART0\nPA00/PA01", fillcolor="#FDE68A"];
  isr  [label="RX 中断 ISR\nuart_read_byte()", fillcolor="#FCA5A5"];
  ring [label="环形缓冲\n512 B (s_rx_ring)"];
  task [label="shellTask()\n主循环调用"];
  edit [label="行编辑/解析\n参数拆分"];
  tbl  [label="shellCommand 段\n16 条命令", fillcolor="#BBF7D0"];
  cmd  [label="命令函数\nexample: cmd_info()", fillcolor="#FDE68A"];
  out  [label="shell->write()\nuart_send_data()", fillcolor="#FCA5A5"];

  pc -> uart [label="按键"];
  uart -> isr [label="DR 中断"];
  isr -> ring [label="入队"];
  ring -> task [label="取字符"];
  task -> edit;
  edit -> tbl [label="按名字查找"];
  tbl -> cmd;
  cmd -> out [label="printf/输出"];
  out -> uart [label="阻塞发送", style=dashed];
  uart -> pc [label="回显"];
}
"""
    render_dot(dot, "03-letter-shell", "fig-03-1-shell-architecture")


def fig_3_2():
    dot = r"""
digraph cmd_register {
  graph [rankdir=TB, fontname="Noto Sans CJK SC", fontsize=12, nodesep=0.30, ranksep=0.40, bgcolor="white", pad=0.2];
  node  [shape=box, style="rounded,filled", fontname="Noto Sans CJK SC", fontsize=11, color="#57606A", fillcolor="#F6F8FA"];
  edge  [fontname="Noto Sans CJK SC", fontsize=9, color="#57606A"];

  src  [label="SHELL_EXPORT_CMD(...)\n放在任意 .c 文件里", fillcolor="#DBEAFE"];
  sec  [label="链接段 shellCommand\n(每条命令一个 ShellCommand 结构体)", fillcolor="#FDE68A"];
  sym  [label="链接器生成符号\n__start_shellCommand / __stop_shellCommand", fillcolor="#BBF7D0"];
  init [label="shellInit()\ncount = (stop-start)/sizeof"];
  help [label="help / 命令查找\n线性比较命令名"];
  run  [label="调用命令函数\nmain 型: f(argc, argv)"];

  src -> sec [label="编译"];
  sec -> sym [label="链接"];
  sym -> init [label="启动时"];
  init -> help;
  help -> run [label="匹配成功"];
}
"""
    render_dot(dot, "03-letter-shell", "fig-03-2-shell-boot-flow")


if __name__ == "__main__":
    setup()
    fig_3_1()
    fig_3_2()
