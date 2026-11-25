#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
千兆车载以太网耐久测试 runner —— HPM5E31 + YT8011AN(1000BASE-T1) + 转换盒 + 主机 enp6s0

被测链路：板 192.168.2.100/24  <--1000BASE-T1-->  转换盒  <--1000BASE-T-->  主机 192.168.2.1/24

每个周期（默认 30 min）：
  1) tcp_h2b  600 s   主机 -> 板 TCP（板作 iperf2 server，约 250 Mbit/s）
  2) udp_h2b  600 s   主机 -> 板 UDP 900 Mbit/s（线速压 MAC/DMA/PHY）
  3) b2h_tcp  300 s   板 -> 主机 TCP，10 s 一发（lwiperf client 固定 10 s），间隙 5 s
  4) idle     300 s   空载，链路保持，只发 ping

全程：
  * ping 5 次/s（200 ms）测 RTT/丢包，按时段分桶
  * 每 30 s 采样板端统计（串口）+ 主机网卡计数（sysfs）
  * 每 5 min 发 eth + uptime（记录 PHY 链路/速率、uptime，用于识别板子复位）
  * 看门狗：串口静默 >45 s 或 ping 连续大量丢 → 记录事件并尝试恢复
      恢复 L1：串口 shell 仍活 -> eth istop 后继续
      恢复 L2：J-Link 复位（r;g，不重新烧写），等启动 banner
  * 所有日志带时间戳，落盘 logs/soak-<tag>/
"""

import argparse
import csv
import fcntl
import json
import os
import queue
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import traceback
from collections import deque
from datetime import datetime

TIOCEXCL = 0x540E  # 独占打开串口

# ----------------------------------------------------------------------------
# 小工具
# ----------------------------------------------------------------------------

def now_iso():
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def log_line(path, text):
    with open(path, "a", encoding="utf-8") as f:
        f.write(text + "\n")


class RecoveryNeeded(Exception):
    pass


# ----------------------------------------------------------------------------
# 串口：读线程 + 解析 + 命令/期望
# ----------------------------------------------------------------------------

class Board:
    RE_SW = re.compile(r"\[lwip-eth\] sw : rx_frames=(\d+) rx_input_err=(\d+) rx_pbuf_fail=(\d+) "
                       r"\| tx_frames=(\d+) tx_err=(\d+) tx_drop_len=(\d+)")
    RE_RECYCLE = re.compile(r"tx recycle: retry_ok=(\d+) retry_fail=(\d+)")
    RE_LOOP = re.compile(r"loop: gap_max=(\d+) us tx_max=(\d+) us rx_drain_max=(\d+) us")
    RE_LINKPOLL = re.compile(r"link poll: n=(\d+) last=(\d+) us max=(\d+) us")
    RE_PBUF = re.compile(r"rx pbuf pool: in_use=(\d+) peak=(\d+)/(\d+)")
    RE_HW = re.compile(r"hw : rx_frames=(\d+) rx_bytes=(\d+) rx_empty=(\d+) rx_err=(\d+) "
                       r"\| tx_frames=(\d+) tx_bytes=(\d+) tx_busy=(\d+) tx_err=(\d+)")
    RE_PHY = re.compile(r"phy: link=(\w+) known=(\d) speed=(\d+) Mbps duplex=(\w+) role=(\w+)")
    RE_PHYREG = re.compile(r"PHY BMCR=(\w+) BMSR=(\w+) GBSR=(\w+) PHYSR=(\w+)")
    RE_GW = re.compile(r"GW=\S+\s+link=(\w+)")
    RE_UPTIME = re.compile(r"uptime: ([\d.]+) s")
    RE_IPERF = re.compile(r"\[iperf\] type=(\d+) remote=(\S+) bytes=(\d+) ms=(\d+) => (\d+) kbit/s")
    RE_BANNER = re.compile(r"=== mcal_eth_lwip")
    RE_PROMPT = re.compile(r"letter:/\$")

    def __init__(self, port, baud, logdir, events):
        self.port_name = port
        self.baud = baud
        self.logdir = logdir
        self.events = events
        self.logf = open(os.path.join(logdir, "serial.log"), "a", encoding="utf-8", buffering=1)
        self.ser = None
        self.seq = 0
        self.recent = deque(maxlen=6000)
        self.cv = threading.Condition()
        self.wlock = threading.Lock()
        self.stop = False
        self.last_rx = time.time()
        self.data = {}                  # 解析出的最新值（累计计数/链路/uptime）
        self.data_ts = 0.0
        self.open_errors = 0
        self.bytes_rx = 0
        self.thread = threading.Thread(target=self._run, daemon=True, name="serial")

    # -- 生命周期 --------------------------------------------------------
    def start(self):
        self.thread.start()

    def _open(self):
        import serial
        s = serial.Serial(self.port_name, self.baud, timeout=0.2)
        try:
            fcntl.ioctl(s.fileno(), TIOCEXCL)
        except Exception:
            pass
        s.reset_input_buffer()
        return s

    def _run(self):
        buf = b""
        while not self.stop:
            try:
                if self.ser is None:
                    self.ser = self._open()
                    self.events("serial_open", "opened %s @ %d" % (self.port_name, self.baud))
                chunk = self.ser.read(4096)
                if not chunk:
                    continue
                self.bytes_rx += len(chunk)
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").replace("\r", "")
                    self._on_line(line)
                if len(buf) > 8192:
                    buf = b""
            except Exception as e:
                self.open_errors += 1
                self.events("serial_error", "%s: %s" % (type(e).__name__, e))
                try:
                    if self.ser is not None:
                        self.ser.close()
                except Exception:
                    pass
                self.ser = None
                time.sleep(2.0)

    def _on_line(self, line):
        ts = time.time()
        with self.cv:
            self.seq += 1
            self.seq_now = self.seq
            self.recent.append((self.seq, ts, line))
            self.last_rx = ts
            self.cv.notify_all()
        try:
            self.logf.write("%s %s\n" % (now_iso(), line))
        except Exception:
            pass
        self._parse(line)

    def _parse(self, line):
        m = self.RE_SW.search(line)
        if m:
            self.data.update(sw_rx_frames=int(m.group(1)), sw_rx_input_err=int(m.group(2)),
                             sw_rx_pbuf_fail=int(m.group(3)), sw_tx_frames=int(m.group(4)),
                             sw_tx_err=int(m.group(5)), sw_tx_drop_len=int(m.group(6)))
            self.data_ts = time.time()
            return
        m = self.RE_RECYCLE.search(line)
        if m:
            self.data.update(retry_ok=int(m.group(1)), retry_fail=int(m.group(2)))
            return
        m = self.RE_LOOP.search(line)
        if m:
            self.data.update(gap_max_us=int(m.group(1)), tx_max_us=int(m.group(2)),
                             rx_drain_max_us=int(m.group(3)))
            return
        m = self.RE_LINKPOLL.search(line)
        if m:
            self.data.update(link_poll_n=int(m.group(1)), link_poll_max_us=int(m.group(3)))
            return
        m = self.RE_PBUF.search(line)
        if m:
            self.data.update(pbuf_in_use=int(m.group(1)), pbuf_peak=int(m.group(2)),
                             pbuf_size=int(m.group(3)))
            return
        m = self.RE_HW.search(line)
        if m:
            self.data.update(hw_rx_frames=int(m.group(1)), hw_rx_bytes=int(m.group(2)),
                             hw_rx_empty=int(m.group(3)), hw_rx_err=int(m.group(4)),
                             hw_tx_frames=int(m.group(5)), hw_tx_bytes=int(m.group(6)),
                             hw_tx_busy=int(m.group(7)), hw_tx_err=int(m.group(8)))
            return
        m = self.RE_PHY.search(line)
        if m:
            self.data.update(phy_link=m.group(1), phy_known=int(m.group(2)),
                             phy_speed=int(m.group(3)), phy_duplex=m.group(4), phy_role=m.group(5))
            self.events("phy", line.strip())
            return
        m = self.RE_PHYREG.search(line)
        if m:
            self.data.update(phy_bmcr=m.group(1), phy_bmsr=m.group(2),
                             phy_gbsr=m.group(3), phy_physr=m.group(4))
            return
        m = self.RE_GW.search(line)
        if m:
            self.data.update(netif_link=m.group(1))
            return
        m = self.RE_UPTIME.search(line)
        if m:
            self.data.update(uptime_s=float(m.group(1)))
            return
        m = self.RE_IPERF.search(line)
        if m:
            self.data.update(iperf_type=int(m.group(1)), iperf_mbit=float(m.group(5)) / 1000.0,
                             iperf_bytes=int(m.group(3)), iperf_ms=int(m.group(4)))
            return
        if self.RE_BANNER.search(line):
            self.data.update(banner_ts=time.time())
            self.events("board_boot", line.strip())
            return
        m = self.RE_PROMPT.search(line)
        if m:
            self.data.update(shell_ts=time.time())

    # -- 发送 / 期望 -----------------------------------------------------
    def send(self, text):
        with self.wlock:
            if self.ser is None:
                return False
            try:
                self.ser.write((text + "\r\n").encode())
                self.ser.flush()
                with self.cv:
                    self.seq += 1
                    self.recent.append((self.seq, time.time(), ">>> " + text))
                return True
            except Exception as e:
                self.events("serial_write_error", str(e))
                return False

    def snapshot(self):
        with self.cv:
            return self.seq

    def wait_re(self, pattern, timeout, from_seq, health_cb=None):
        rx = pattern if hasattr(pattern, "search") else re.compile(pattern)
        end = time.time() + timeout
        while time.time() < end:
            with self.cv:
                for s, t, line in self.recent:
                    if s > from_seq and rx.search(line):
                        return line
            if health_cb is not None:
                health_cb()
            time.sleep(0.15)
        return None

    def is_silent(self, secs):
        return (time.time() - self.last_rx) > secs


# ----------------------------------------------------------------------------
# ping 监视
# ----------------------------------------------------------------------------

class PingMonitor:
    RE_OK = re.compile(r"icmp_seq=(\d+).*time=([0-9.]+) ms")
    RE_LOSS = re.compile(r"no answer yet for icmp_seq=(\d+)")

    def __init__(self, ip, logdir, events, interval="0.2"):
        self.ip = ip
        self.interval = interval
        self.events = events
        self.logf = open(os.path.join(logdir, "ping.log"), "a", encoding="utf-8", buffering=1)
        self.results = deque(maxlen=400000)   # (ts, ok, rtt_ms)
        self.stop = False
        self.proc = None
        self.thread = threading.Thread(target=self._run, daemon=True, name="ping")

    def start(self):
        self.thread.start()

    def _run(self):
        while not self.stop:
            try:
                cmd = ["stdbuf", "-oL", "ping", "-D", "-n", "-i", self.interval, "-W", "1", "-O", self.ip]
                self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                             text=True, bufsize=1, start_new_session=True)
                self.events("ping_start", "pid=%d %s" % (self.proc.pid, " ".join(cmd)))
                for line in self.proc.stdout:
                    self._on_line(line.rstrip("\n"))
                rc = self.proc.wait()
                if not self.stop:
                    self.events("ping_exit", "rc=%s, restarting" % rc)
                    time.sleep(2)
            except Exception as e:
                self.events("ping_error", str(e))
                time.sleep(3)

    def _on_line(self, line):
        m = self.RE_OK.search(line)
        ts = time.time()
        if m:
            rtt = float(m.group(2))
            self.results.append((ts, True, rtt))
            try:
                self.logf.write("%s ok seq=%s rtt_ms=%.3f\n" % (now_iso(), m.group(1), rtt))
            except Exception:
                pass
            return
        m = self.RE_LOSS.search(line)
        if m:
            self.results.append((ts, False, None))
            try:
                self.logf.write("%s loss seq=%s\n" % (now_iso(), m.group(1)))
            except Exception:
                pass
            return

    def window(self, secs):
        cutoff = time.time() - secs
        n = ok = 0
        rtts = []
        for ts, good, rtt in reversed(self.results):
            if ts < cutoff:
                break
            n += 1
            if good:
                ok += 1
                rtts.append(rtt)
        if n == 0:
            return {"n": 0, "ok": 0, "loss_pct": None, "rtt_avg": None, "rtt_max": None}
        return {"n": n, "ok": ok, "loss_pct": round(100.0 * (n - ok) / n, 2),
                "rtt_avg": round(sum(rtts) / len(rtts), 3) if rtts else None,
                "rtt_max": round(max(rtts), 3) if rtts else None}


# ----------------------------------------------------------------------------
# 采样（板端统计 + 主机网卡计数 + ping 窗口）
# ----------------------------------------------------------------------------

SAMPLE_COLS = [
    "ts", "epoch", "cycle", "phase", "uptime_s",
    "sw_rx_frames", "sw_rx_input_err", "sw_rx_pbuf_fail", "sw_tx_frames", "sw_tx_err", "sw_tx_drop_len",
    "retry_ok", "retry_fail", "gap_max_us", "tx_max_us", "rx_drain_max_us",
    "link_poll_n", "link_poll_max_us", "pbuf_in_use", "pbuf_peak",
    "hw_rx_frames", "hw_rx_bytes", "hw_rx_err", "hw_tx_frames", "hw_tx_bytes", "hw_tx_busy", "hw_tx_err",
    "host_rx_errors", "host_rx_dropped", "host_tx_errors", "host_rx_crc_errors",
    "host_rx_missed_errors", "host_rx_over_errors", "host_speed",
    "ping_n_60", "ping_ok_60", "ping_loss_pct_60", "ping_rtt_avg_ms_60", "ping_rtt_max_ms_60",
    "phy_link", "phy_speed",
]


class Sampler(threading.Thread):
    def __init__(self, runner, period=30.0):
        super().__init__(daemon=True, name="sampler")
        self.runner = runner
        self.period = period
        self.path = os.path.join(runner.logdir, "samples.csv")
        self.stop = False
        new = not os.path.exists(self.path) or os.path.getsize(self.path) == 0
        self.f = open(self.path, "a", newline="", encoding="utf-8")
        self.w = csv.DictWriter(self.f, fieldnames=SAMPLE_COLS)
        if new:
            self.w.writeheader()
            self.f.flush()

    def sample(self):
        r = self.runner
        d = dict(r.board.data)
        p = r.ping.window(60)
        row = {
            "ts": now_iso(), "epoch": round(time.time(), 3),
            "cycle": r.cycle, "phase": r.phase_name,
            "uptime_s": d.get("uptime_s", ""),
            "sw_rx_frames": d.get("sw_rx_frames", ""), "sw_rx_input_err": d.get("sw_rx_input_err", ""),
            "sw_rx_pbuf_fail": d.get("sw_rx_pbuf_fail", ""), "sw_tx_frames": d.get("sw_tx_frames", ""),
            "sw_tx_err": d.get("sw_tx_err", ""), "sw_tx_drop_len": d.get("sw_tx_drop_len", ""),
            "retry_ok": d.get("retry_ok", ""), "retry_fail": d.get("retry_fail", ""),
            "gap_max_us": d.get("gap_max_us", ""), "tx_max_us": d.get("tx_max_us", ""),
            "rx_drain_max_us": d.get("rx_drain_max_us", ""),
            "link_poll_n": d.get("link_poll_n", ""), "link_poll_max_us": d.get("link_poll_max_us", ""),
            "pbuf_in_use": d.get("pbuf_in_use", ""), "pbuf_peak": d.get("pbuf_peak", ""),
            "hw_rx_frames": d.get("hw_rx_frames", ""), "hw_rx_bytes": d.get("hw_rx_bytes", ""),
            "hw_rx_err": d.get("hw_rx_err", ""), "hw_tx_frames": d.get("hw_tx_frames", ""),
            "hw_tx_bytes": d.get("hw_tx_bytes", ""), "hw_tx_busy": d.get("hw_tx_busy", ""),
            "hw_tx_err": d.get("hw_tx_err", ""),
            "ping_n_60": p["n"], "ping_ok_60": p["ok"], "ping_loss_pct_60": p["loss_pct"],
            "ping_rtt_avg_ms_60": p["rtt_avg"], "ping_rtt_max_ms_60": p["rtt_max"],
            "phy_link": d.get("phy_link", ""), "phy_speed": d.get("phy_speed", ""),
        }
        host = r.host_counters()
        row.update(host)
        self.w.writerow(row)
        self.f.flush()
        return row

    def run(self):
        while not self.stop:
            try:
                self.sample()
            except Exception as e:
                self.runner.events("sample_error", "%s: %s" % (type(e).__name__, e))
            time.sleep(self.period)


# ----------------------------------------------------------------------------
# Runner
# ----------------------------------------------------------------------------

class Runner:
    def __init__(self, a):
        self.a = a
        self.tag = a.tag or datetime.now().strftime("%Y%m%d-%H%M%S")
        self.logdir = os.path.join(a.logroot, "soak-" + self.tag)
        os.makedirs(os.path.join(self.logdir, "phases"), exist_ok=True)
        self.events_path = os.path.join(self.logdir, "events.log")
        self.phases_path = os.path.join(self.logdir, "phases.jsonl")
        self.status_path = os.path.join(self.logdir, "status.json")
        self.stop = threading.Event()
        self.board = Board(a.serial, a.baud, self.logdir, self.events)
        self.ping = PingMonitor(a.board_ip, self.logdir, self.events)
        self.cycle = 0
        self.phase_name = "init"
        self.start_time = None
        self.end_time = None
        self.proc = None
        self.host_srv = None
        self.resets = 0
        self.recoveries = 0
        self.phase_results = []
        self.last_housekeeping = 0.0
        self.last_status = 0.0
        self.start_uptime = None
        self.last_udp_end = 0.0
        self.last_recover_ts = 0.0

    # -- 基础设施 --------------------------------------------------------
    def events(self, kind, msg):
        line = "%s %-16s %s" % (now_iso(), kind, msg)
        try:
            log_line(self.events_path, line)
        except Exception:
            pass
        if kind not in ("sample_error", "serial_write_error"):
            print(line, flush=True)

    def host_counters(self):
        base = "/sys/class/net/" + self.a.nic + "/statistics/"
        out = {}
        for k in ("rx_errors", "rx_dropped", "tx_errors", "rx_crc_errors",
                  "rx_missed_errors", "rx_over_errors"):
            try:
                with open(base + k) as f:
                    out["host_" + k] = int(f.read().strip())
            except Exception:
                out["host_" + k] = ""
        try:
            with open("/sys/class/net/" + self.a.nic + "/speed") as f:
                out["host_speed"] = int(f.read().strip())
        except Exception:
            out["host_speed"] = ""
        return out

    def write_status(self):
        p = self.ping.window(60)
        st = {
            "ts": now_iso(), "tag": self.tag, "cycle": self.cycle, "phase": self.phase_name,
            "elapsed_h": round((time.time() - self.start_time) / 3600.0, 3) if self.start_time else None,
            "duration_h": self.a.duration_h, "resets": self.resets, "recoveries": self.recoveries,
            "ping_60": p, "uptime_s": self.board.data.get("uptime_s"),
            "board_rx_err": self.board.data.get("hw_rx_err"),
            "board_tx_err": self.board.data.get("hw_tx_err"),
            "board_rx_frames": self.board.data.get("hw_rx_frames"),
            "board_tx_frames": self.board.data.get("hw_tx_frames"),
            "phy_link": self.board.data.get("phy_link"),
            "serial_silent_s": round(time.time() - self.board.last_rx, 1),
            "phases_done": len(self.phase_results),
            "pid": os.getpid(),
        }
        try:
            tmp = self.status_path + ".tmp"
            with open(tmp, "w") as f:
                json.dump(st, f, indent=1)
            os.replace(tmp, self.status_path)
        except Exception:
            pass
        self.last_status = time.time()

    # -- 健康 / 恢复 -----------------------------------------------------
    def health(self):
        # 串口静默是唯一强判据：板子每 10 s 打一次统计；静默 >45 s 视为挂死
        if self.board.is_silent(45):
            raise RecoveryNeeded("board serial silent %.0fs" % (time.time() - self.board.last_rx))
        # UDP 线速压测阶段/其后 120 s 内，板端本来就会丢 ICMP（软件容量上限，约 285 Mbit/s 封顶），
        # 此时 ping 丢包不代表链路异常；b2h 阶段也不看 ping（避免误判把 iperf 客户端会话打断）
        if self.phase_name in ("udp_h2b", "b2h_tcp"):
            return
        if (time.time() - self.last_udp_end) < 120.0:
            return
        p = self.ping.window(30)
        if p["n"] >= 20 and p["loss_pct"] is not None and p["loss_pct"] > 60.0:
            raise RecoveryNeeded("ping loss %.0f%% in last 30s" % p["loss_pct"])

    def health_cb(self):
        self.health()

    def kill_proc(self, proc):
        if proc is None:
            return
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except Exception:
            try:
                proc.terminate()
            except Exception:
                pass
        try:
            proc.wait(timeout=5)
        except Exception:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except Exception:
                pass

    def recover(self, why):
        self.recoveries += 1
        # 15 min 内二次恢复 / 抖动停摆 / 监听丢失 → 直接 JLink 复位（L1 的 istop 修不了监听态）
        hard = (("stalled" in why) or ("listener lost" in why) or
                (time.time() - self.last_recover_ts < 900.0))
        self.last_recover_ts = time.time()
        self.events("recover_begin", why + (" [HARD]" if hard else ""))
        self.kill_proc(self.proc)
        self.kill_proc(self.host_srv)
        self.proc = None
        self.host_srv = None

        # L1: shell 还活着吗？（hard 模式跳过，直接复位）
        if not hard:
            self.board.send("")
            if self.board.wait_re(Board.RE_PROMPT, 2.0, self.board.snapshot()):
                self.events("recover_shell", "shell alive, istop + continue")
                self.board.send("eth istop")
                time.sleep(0.5)
                self.board.send("uptime")
                self.board.wait_re(Board.RE_UPTIME, 2.0, self.board.snapshot())
                time.sleep(1.0)
                return

        # L2: J-Link 复位（不重新烧写）
        self.events("recover_jlink", "shell dead -> JLink reset")
        seq_before = self.board.snapshot()
        jl = os.path.join(self.logdir, "reset.jlink")
        with open(jl, "w") as f:
            f.write("JTAGConf -1,-1\nr\nh\nr\ng\nqc\n")
        cmd = ["JLinkExe", "-NoGui", "1", "-AutoConnect", "1", "-Device", self.a.jlink_device,
               "-If", "JTAG", "-Speed", self.a.jlink_speed, "-ExitOnError", "1", "-CommandFile", jl]
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            tail = "\n".join(out.stdout.strip().splitlines()[-6:])
            self.events("recover_jlink_out", tail.replace("\n", " | "))
        except Exception as e:
            self.events("recover_jlink_error", str(e))
        if self.board.wait_re(Board.RE_BANNER, 25.0, seq_before):
            self.resets += 1
            self.events("recover_ok", "board rebooted, banner seen")
            time.sleep(4.0)   # 等上电自证 ping 结束
        else:
            self.events("recover_fail", "no banner after JLink reset")
            time.sleep(5.0)

    # -- 板端命令 --------------------------------------------------------
    def board_cmd(self, cmd, pattern, timeout=5.0):
        seq = self.board.snapshot()
        self.board.send(cmd)
        line = self.board.wait_re(pattern, timeout, seq, health_cb=self.health_cb)
        if line is None:
            self.events("cmd_timeout", "%s (expect %s)" % (cmd, pattern))
        return line

    def arm_server(self, proto):
        # lwIP lwiperf 的监听 pcb 在一次会话结束后不会关闭（s_iperf_session 被回调置 NULL，
        # 但 listen pcb 仍绑在 5001），所以第二次起 "eth iserver X" 必然返回
        # "server start FAILED (已有会话在跑?)" —— 这是正常的，旧监听仍在服务新连接。
        # 因此这里不 istop（istop 在 s_iperf_session 指向监听态时会把监听关掉），只发一次。
        pat = re.compile(r"iperf\] %s server started|iperf\] server start FAILED" % proto.upper())
        for attempt in (1, 2):
            seq = self.board.snapshot()
            self.board.send("eth iserver " + proto)
            line = self.board.wait_re(pat, 5.0, seq, health_cb=self.health_cb)
            if line is not None:
                started = "started" in line
                self.events("arm_server", "%s -> %s" % (proto, "started" if started else "already-listening"))
                return True
            self.events("arm_server_retry", "%s attempt=%d no response" % (proto, attempt))
            time.sleep(1.0)
        return False

    def traffic_with_retry(self, name, dur, fn, logname):
        res = fn(dur)
        if res.get("bytes"):
            return res
        # 0 字节：监听可能真的没了（板子重启后/被 istop 关掉），重建一次再跑
        self.events("traffic_retry", "%s got 0 bytes, re-arm" % name)
        self.board.send("eth istop")
        time.sleep(0.6)
        seq = self.board.snapshot()
        self.board.send("eth iserver " + ("tcp" if name == "tcp_h2b" else "udp"))
        line = self.board.wait_re(r"iperf\] %s server started" % ("TCP" if name == "tcp_h2b" else "UDP"),
                                  5.0, seq, health_cb=self.health_cb)
        if line is None:
            raise RecoveryNeeded("%s: iperf listener lost and cannot restart" % name)
        return fn(dur)

    def housekeeping(self, force=False):
        if not force and (time.time() - self.last_housekeeping) < self.a.housekeeping_s:
            return
        self.last_housekeeping = time.time()
        seq = self.board.snapshot()
        self.board.send("eth")
        line = self.board.wait_re(Board.RE_PHYREG, 4.0, seq, health_cb=self.health_cb)
        if line is None:
            return
        time.sleep(0.2)
        seq = self.board.snapshot()
        self.board.send("uptime")
        line = self.board.wait_re(Board.RE_UPTIME, 3.0, seq, health_cb=self.health_cb)
        if line:
            try:
                up = float(Board.RE_UPTIME.search(line).group(1))
                prev = self.board.data.get("uptime_prev")
                if prev is not None and up + 30.0 < prev:
                    self.events("reset_detected", "uptime went backwards: %.1f -> %.1f" % (prev, up))
                    self.resets += 1
                self.board.data["uptime_prev"] = up
                if self.start_uptime is None:
                    self.start_uptime = up
            except Exception:
                pass
        # lwIP 资源快照（固件带 lwipstat 时）：pcb 池 / mem 堆用量，落 serial.log
        seq = self.board.snapshot()
        self.board.send("lwipstat")
        self.board.wait_re(r"MEM PBUF_POOL", 3.0, seq, health_cb=self.health_cb)

    # -- 流量 ------------------------------------------------------------
    def run_iperf(self, argv, logname, max_secs=None):
        path = os.path.join(self.logdir, "phases", logname)
        f = open(path, "a", encoding="utf-8", buffering=1)
        f.write("# %s  %s\n" % (now_iso(), " ".join(argv)))
        self.events("iperf_start", " ".join(argv))
        proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, bufsize=1, start_new_session=True)
        self.proc = proc
        q = queue.Queue()

        def pump():
            try:
                for line in proc.stdout:
                    q.put(line)
            finally:
                q.put(None)

        threading.Thread(target=pump, daemon=True).start()
        deadline = time.time() + (max_secs or 36000)
        last_bytes = 0
        last_interval = 0.0
        final_mbit = None
        last_rx = self.board.data.get("hw_rx_frames", 0)
        last_progress = time.time()
        rx_hist = deque(maxlen=400)
        t_start = time.time()
        while True:
            try:
                line = q.get(timeout=1.0)
            except queue.Empty:
                self.health()
                rx = self.board.data.get("hw_rx_frames", 0)
                now_t = time.time()
                rx_hist.append((now_t, rx))
                if rx != last_rx:
                    last_rx = rx
                    last_progress = now_t
                # 速率判据：ping 只有 5 帧/s，打流阶段若 30 s 平均 <200 帧/s 视为停摆
                if (now_t - t_start) > 60.0 and self.phase_name in ("tcp_h2b", "udp_h2b"):
                    old = None
                    for ts0, rx0 in rx_hist:
                        if now_t - ts0 <= 30.0:
                            old = (ts0, rx0)
                            break
                    if old is not None and (now_t - old[0]) >= 20.0:
                        rate = (rx - old[1]) / (now_t - old[0])
                        if rate < 200.0:
                            self.events("traffic_stalled", "%s: board rx rate %.0f fps (<200) for 20s"
                                        % (logname, rate))
                            self.kill_proc(proc)
                            raise RecoveryNeeded("traffic stalled: rx rate %.0f fps" % rate)
                elif (now_t - last_progress) > 45.0:
                    self.events("traffic_stalled", "%s: no board rx frames for 45s" % logname)
                    self.kill_proc(proc)
                    raise RecoveryNeeded("traffic stalled: no board rx frames for 45 s")
                if time.time() > deadline:
                    self.events("iperf_timeout", logname)
                    self.kill_proc(proc)
                    break
                if proc.poll() is not None and q.empty():
                    break
                continue
            if line is None:
                break
            f.write(line)
            parts = line.rstrip("\n").split(",")
            if len(parts) >= 9 and parts[6].count("-") == 1:
                try:
                    a, b = parts[6].split("-")
                    iv = float(b) - float(a)
                    if iv > 0:
                        last_interval = float(b)
                        last_bytes = int(float(parts[7]))
                        final_mbit = float(parts[8]) / 1e6
                except Exception:
                    pass
        try:
            proc.wait(timeout=15)
        except Exception:
            self.kill_proc(proc)
        self.proc = None
        f.write("# end %s rc=%s bytes=%s mbit=%s\n" % (now_iso(), proc.returncode, last_bytes, final_mbit))
        f.close()
        return {"bytes": last_bytes, "secs": last_interval, "mbit": final_mbit}

    # -- 阶段 ------------------------------------------------------------
    def record_phase(self, rec):
        rec["ts"] = now_iso()
        rec["cycle"] = self.cycle
        with open(self.phases_path, "a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
        self.phase_results.append(rec)
        self.events("phase_done", json.dumps(rec, ensure_ascii=False))

    def phase_tcp_h2b(self, dur):
        self.arm_server("tcp")
        return self.traffic_with_retry(
            "tcp_h2b", dur,
            lambda d: self.run_iperf([self.a.iperf, "-c", self.a.board_ip, "-t", str(d),
                                      "-i", str(self.a.interval_s), "-f", "m", "-y", "C"],
                                     "%02d-tcp_h2b.log" % self.cycle, max_secs=d + 90),
            "tcp")

    def phase_udp_h2b(self, dur):
        self.arm_server("udp")
        try:
            return self.traffic_with_retry(
                "udp_h2b", dur,
                lambda d: self.run_iperf([self.a.iperf, "-c", self.a.board_ip, "-u", "-b", self.a.udp_rate,
                                          "-t", str(d), "-i", str(self.a.interval_s), "-f", "m", "-y", "C"],
                                         "%02d-udp_h2b.log" % self.cycle, max_secs=d + 90),
                "udp")
        finally:
            self.last_udp_end = time.time()

    def phase_b2h_tcp(self, dur):
        path = os.path.join(self.logdir, "phases", "%02d-b2h_tcp.log" % self.cycle)
        f = open(path, "a", encoding="utf-8", buffering=1)
        f.write("# %s board->host TCP, bursts\n" % now_iso())
        self.kill_proc(self.host_srv)
        self.host_srv = subprocess.Popen([self.a.iperf, "-s", "-i", "30", "-f", "m"],
                                         stdout=open(path + ".host", "a"), stderr=subprocess.STDOUT,
                                         start_new_session=True)
        self.events("host_server", "iperf -s pid=%d" % self.host_srv.pid)
        time.sleep(1.0)
        bursts = 0
        ok = 0
        total_bytes = 0
        mbits = []
        t_end = time.time() + dur
        while time.time() < t_end and not self.stop.is_set():
            seq = self.board.snapshot()
            self.board.send("eth iclient %s tcp" % self.a.host_ip)
            line = self.board.wait_re(re.compile(r"\[iperf\] type=1 |\[iperf\] client start FAILED"),
                                      18.0, seq, health_cb=self.health_cb)
            if line is None or "client start FAILED" in line:
                self.events("b2h_timeout", "no client report (%s); istop + restart host server" %
                            ("no line" if line is None else "start failed"))
                self.board.send("eth istop")   # 清掉占着会话槽的监听态，下一轮 iserver 会重建
                time.sleep(0.5)
                self.kill_proc(self.host_srv)
                self.host_srv = subprocess.Popen([self.a.iperf, "-s", "-i", "30", "-f", "m"],
                                                 stdout=open(path + ".host", "a"),
                                                 stderr=subprocess.STDOUT, start_new_session=True)
                time.sleep(1.0)
                continue
            m = Board.RE_IPERF.search(line)
            bursts += 1
            if m and m.group(1) == "1":
                ok += 1
                total_bytes += int(m.group(3))
                mbits.append(float(m.group(5)) / 1000.0)
                f.write(line + "\n")
            time.sleep(self.a.b2h_gap)
        self.kill_proc(self.host_srv)
        self.host_srv = None
        f.close()
        return {"bytes": total_bytes, "secs": dur,
                "mbit": (sum(mbits) / len(mbits)) if mbits else None,
                "bursts": bursts, "bursts_ok": ok}

    def phase_idle(self, dur):
        t_end = time.time() + dur
        seq = self.board.snapshot()
        self.board.send("eth istop")
        while time.time() < t_end and not self.stop.is_set():
            self.health()
            time.sleep(1.0)
        return {"bytes": 0, "secs": dur, "mbit": None}

    # -- 主流程 ----------------------------------------------------------
    def preflight(self):
        self.events("preflight", "start")
        if not os.path.isdir("/sys/class/net/" + self.a.nic):
            raise SystemExit("NIC %s not found" % self.a.nic)
        # 确保主机侧有 192.168.2.1/24（板子网关）
        r = subprocess.run(["ip", "-4", "addr", "show", self.a.nic], capture_output=True, text=True)
        if self.a.host_ip + "/24" not in r.stdout:
            self.events("preflight_ip", "adding %s/24 to %s" % (self.a.host_ip, self.a.nic))
            subprocess.run(["sudo", "-n", "ip", "addr", "add", self.a.host_ip + "/24", "dev", self.a.nic],
                           capture_output=True, text=True)
        # 清理残留 iperf
        subprocess.run(["pkill", "-x", "iperf"], capture_output=True)
        time.sleep(0.5)
        r = subprocess.run(["ping", "-c", "5", "-W", "1", self.a.board_ip], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("preflight: board %s unreachable\n%s" % (self.a.board_ip, r.stdout))
        self.events("preflight_ping", r.stdout.strip().splitlines()[-2] if len(r.stdout.strip().splitlines()) > 1 else "ok")
        # 板端信息
        self.board_cmd("ver", r"build\s+:", 4.0)
        self.housekeeping(force=True)
        # 让两个监听都在（lwIP 的监听在会话结束后仍留在 5001，这里幂等）
        self.arm_server("tcp")
        self.arm_server("udp")
        if self.board.data.get("phy_link") != "UP":
            self.events("preflight_warn", "phy link is %s" % self.board.data.get("phy_link"))
        self.events("preflight_ok", "phy=%s speed=%s uptime=%s" % (
            self.board.data.get("phy_link"), self.board.data.get("phy_speed"),
            self.board.data.get("uptime_s")))

    def run(self):
        self.start_time = time.time()
        self.end_time = self.start_time + self.a.duration_h * 3600.0
        self.events("run_start", "tag=%s duration=%.2fh dir=%s" % (self.tag, self.a.duration_h, self.logdir))
        self.board.start()
        self.ping.start()
        time.sleep(1.0)
        try:
            self.preflight()
        except SystemExit:
            raise
        except Exception as e:
            self.events("preflight_fail", "%s: %s" % (type(e).__name__, e))
            raise
        sampler = Sampler(self)
        sampler.start()
        self.write_status()
        if self.board.data.get("uptime_s") is not None:
            self.start_uptime = self.board.data["uptime_s"]

        normal_end = False
        errors = 0
        try:
            while time.time() < self.end_time and not self.stop.is_set():
                try:
                    self.run_cycle()
                    errors = 0
                except RecoveryNeeded as e:
                    self.events("cycle_recover", str(e))
                    try:
                        self.recover(str(e))
                    except Exception:
                        self.events("recover_error", traceback.format_exc().replace("\n", " | "))
                    time.sleep(3.0)
                except Exception:
                    errors += 1
                    self.events("cycle_error", traceback.format_exc().replace("\n", " | "))
                    time.sleep(10.0)
                    if errors >= 20:
                        self.events("run_abort", "too many consecutive cycle errors")
                        break
            normal_end = (not self.stop.is_set()) and time.time() >= self.end_time
        except KeyboardInterrupt:
            self.events("run_interrupt", "KeyboardInterrupt")
        finally:
            self.shutdown()
            sampler.stop = True
            try:
                self.board.send("eth istop")
            except Exception:
                pass
            self.board.stop = True
            self.ping.stop = True
            self.write_status()
            self.events("run_end", "elapsed_h=%.2f" % ((time.time() - self.start_time) / 3600.0))
            if normal_end:
                try:
                    with open(os.path.join(self.logdir, "DONE"), "w") as f:
                        f.write("%s normal completion, elapsed %.2f h\n" % (
                            now_iso(), (time.time() - self.start_time) / 3600.0))
                except Exception:
                    pass
            self.final_report()

    def run_cycle(self):
        self.cycle += 1
        for name, dur in self.a.plan:
            if time.time() >= self.end_time or self.stop.is_set():
                break
            self.phase_name = name
            self.events("phase_begin", "cycle=%d %s %ss" % (self.cycle, name, dur))
            t0 = time.time()
            rec = {"phase": name, "dur_plan": dur, "cycle": self.cycle}
            for attempt in (1, 2):
                try:
                    if name == "tcp_h2b":
                        res = self.phase_tcp_h2b(dur)
                    elif name == "udp_h2b":
                        res = self.phase_udp_h2b(dur)
                    elif name == "b2h_tcp":
                        res = self.phase_b2h_tcp(dur)
                    elif name == "idle":
                        res = self.phase_idle(dur)
                    else:
                        raise RuntimeError("unknown phase " + name)
                    rec.update(res)
                    rec["ok"] = True
                    if name in ("tcp_h2b", "udp_h2b") and not res.get("bytes"):
                        rec["ok"] = False
                        rec["error"] = "no traffic"
                        self.events("phase_no_traffic", "%s cycle=%d" % (name, self.cycle))
                    break
                except RecoveryNeeded as e:
                    self.events("phase_recover", "%s attempt=%d %s" % (name, attempt, e))
                    rec["recovered"] = True
                    self.recover(str(e))
                    time.sleep(2.0)
                    if attempt == 2:
                        rec["ok"] = False
                        rec["error"] = str(e)
            rec["dur_actual"] = round(time.time() - t0, 1)
            self.record_phase(rec)
            self.housekeeping()
            self.write_status()

    def shutdown(self):
        self.kill_proc(self.proc)
        self.kill_proc(self.host_srv)
        self.proc = None
        self.host_srv = None

    def final_report(self):
        try:
            script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "soak_report.py")
            if os.path.exists(script):
                subprocess.run([sys.executable, script, self.logdir], timeout=300)
                self.events("report_written", os.path.join(self.logdir, "summary.md"))
        except Exception as e:
            self.events("report_error", str(e))


def parse_plan(s, scale=1.0):
    out = []
    for item in s.split(","):
        name, dur = item.split(":")
        out.append((name, max(2, int(float(dur) * scale))))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--serial", default="/dev/ttyACM1")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--board-ip", default="192.168.2.100")
    ap.add_argument("--host-ip", default="192.168.2.1")
    ap.add_argument("--nic", default="enp6s0")
    ap.add_argument("--iperf", default="/usr/bin/iperf")
    ap.add_argument("--udp-rate", default="900M")
    ap.add_argument("--interval-s", type=int, default=60)
    ap.add_argument("--b2h-gap", type=float, default=5.0)
    ap.add_argument("--housekeeping-s", type=int, default=300)
    ap.add_argument("--duration-h", type=float, default=15.0)
    ap.add_argument("--plan", default="tcp_h2b:600,b2h_tcp:300,udp_h2b:300,idle:300")
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--tag", default="")
    ap.add_argument("--logroot", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "logs"))
    ap.add_argument("--jlink-device", default="HPM5E31xGNx")
    ap.add_argument("--jlink-speed", default="4000")
    a = ap.parse_args()
    a.plan = parse_plan(a.plan, a.scale)
    r = Runner(a)
    signal.signal(signal.SIGTERM, lambda *_: r.stop.set())
    try:
        r.run()
    except SystemExit as e:
        r.events("fatal", str(e))
        raise
    except Exception:
        r.events("fatal", traceback.format_exc())
        raise


if __name__ == "__main__":
    main()