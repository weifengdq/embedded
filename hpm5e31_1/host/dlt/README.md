# host/dlt —— DLT 报文接收与解析

```bash
# 板端先跑 samples/app/dlt_log（上电自动开始发），主机：
$HOME/pyenv/bin/python host/dlt/dlt_recv.py --duration 6 --out /tmp/dlt/board.dlt
```

解析内容见 `dlt_recv.py` 头注释（Standard Header 4 B + Extended Header 14 B + verbose 参数）；
统计包数/速率/MCNT 丢包/APID/CTID/level 直方图，原始报文写标准 `.dlt` 文件（可直接给 dlt-viewer/厂商工具）。
实测数据见 `docs/chapters/39-dlt-vss-canlog.md`。

## 官方工具链验证（U-38-3）

`dlt_recv.py` 写出的 .dlt 带 16 B `DltStorageHeader`，可直接用 dlt-viewer 打开：

```bash
QT_QPA_PLATFORM=offscreen dlt-viewer -c out.txt board.dlt   # 无 GUI 环境不可用，见下
# 无头截图（本仓库实测方式）：
Xvfb :96 -screen 0 1600x1000x24 & DISPLAY=:96 dlt-viewer board.dlt &
DISPLAY=:96 ffmpeg -f x11grab -video_size 1600x1000 -i :96 -frames:v 1 -y dlt_viewer.png
```

实测：dlt-viewer 2.30.0 显示 599 条消息、FileErr 0，截图
`logs/20261102-104100-dlt-viewer-host-validation.png`。

> dlt-daemon 的 TCP 3490 是**控制端口**（dlt-receive/dlt-control），不是 ECU 日志入口；
> 要走 dlt-daemon 需板端接 `dlt-adaptor-udp` 或实现 DLT 客户端握手（见第 37 章 U-38-8）。
