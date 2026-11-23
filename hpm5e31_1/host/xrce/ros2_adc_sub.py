#!/usr/bin/env python3
"""ros2_adc_sub.py —— 订阅板端 XRCE 发布的 /hpm5e31/adc（std_msgs/UInt16MultiArray）

统计：窗口频率、到达间隔抖动（σ）、用第 4 通道计数器反推的丢帧率、PASS/FAIL 结论。
注意：**绝对单向延迟**需要板端与主机时间同步（PTP/UTC），本章只用"命令往返 RTT"
（ros2_cmd_pub.py 与板端 ack）给出端到端延迟上界，见 README。

用法：
  source /opt/ros/lyrical/setup.bash
  python3 host/xrce/ros2_adc_sub.py --duration 10
  python3 host/xrce/ros2_adc_sub.py --topic /hpm5e31/adc --reliable --duration 5
"""
import argparse
import statistics
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy, DurabilityPolicy
from std_msgs.msg import UInt16MultiArray


class AdcSub(Node):
    def __init__(self, topic, reliable):
        super().__init__("hpm5e31_adc_sub")
        qos = QoSProfile(
            depth=50,
            history=HistoryPolicy.KEEP_LAST,
            reliability=ReliabilityPolicy.RELIABLE if reliable else ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
        )
        self._n = 0
        self._counter_prev = None
        self._counter_lost = 0
        self._t_first = None
        self._t_last = None
        self._intervals = []
        self._last_arrival = None
        self._values = []
        self.create_subscription(UInt16MultiArray, topic, self._cb, qos)

    def _cb(self, msg):
        now = time.monotonic_ns()
        self._n += 1
        if self._t_first is None:
            self._t_first = now
        if self._last_arrival is not None:
            self._intervals.append((now - self._last_arrival) / 1e6)
        self._last_arrival = now
        self._t_last = now
        d = list(msg.data)
        self._values = d
        if len(d) >= 4:
            cnt = d[3]
            if self._counter_prev is not None:
                step = (cnt - self._counter_prev) % 65536
                if step > 1:
                    self._counter_lost += step - 1
            self._counter_prev = cnt

    def summary(self):
        dur = 0.0 if self._t_first is None else (self._t_last - self._t_first) / 1e9
        rate = (self._n - 1) / dur if dur > 0 else 0.0
        jitter = statistics.pstdev(self._intervals) if len(self._intervals) > 1 else 0.0
        total = self._n + self._counter_lost
        loss = (100.0 * self._counter_lost / total) if total else 0.0
        return dict(n=self._n, dur=dur, rate=rate, jitter=jitter, lost=self._counter_lost,
                    loss=loss, values=self._values)


def main():
    ap = argparse.ArgumentParser(description="订阅 /hpm5e31/adc 并统计频率/抖动/丢帧")
    ap.add_argument("--topic", default="/hpm5e31/adc")
    ap.add_argument("--duration", type=float, default=5.0, help="采样时长（秒）")
    ap.add_argument("--reliable", action="store_true", help="用 RELIABLE QoS（默认 BEST_EFFORT）")
    args = ap.parse_args()

    rclpy.init()
    node = AdcSub(args.topic, args.reliable)
    t0 = time.monotonic()
    try:
        while time.monotonic() - t0 < args.duration:
            rclpy.spin_once(node, timeout_sec=0.1)
    finally:
        s = node.summary()
        node.destroy_node()
        rclpy.shutdown()

    print("=" * 68)
    print(" HPM5E31 XRCE-DDS -> ROS 2 ADC 订阅统计（topic=%s, qos=%s）" %
          (args.topic, "RELIABLE" if args.reliable else "BEST_EFFORT"))
    print("=" * 68)
    print(" 样本数            : %d" % s["n"])
    print(" 测量窗口          : %.2f s" % s["dur"])
    print(" 平均频率          : %.2f Hz" % s["rate"])
    print(" 到达间隔抖动 σ    : %.2f ms" % s["jitter"])
    print(" 计数器丢帧        : %d（%.2f %%）" % (s["lost"], s["loss"]))
    if s["values"]:
        print(" 最后一帧 4×u16    : %s" % (s["values"][:4],))
    ok = (s["n"] > 0) and (s["rate"] > 1.0)
    print(" 结论              : %s" % ("PASS" if ok else "FAIL"))
    print("=" * 68)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
