#!/usr/bin/env python3
"""ros2_cmd_pub.py —— 向板端发布 /hpm5e31/cmd（std_msgs/UInt8）控制 LED，并测命令往返 RTT

协议：0=灭 1=亮 2=翻转。
板端收到后立刻在 /hpm5e31/hello（std_msgs/String）回一条 "cmd=<v> ack=<n>"，
本脚本据此计算 主机发布 -> 板端 -> Agent -> 主机收到 ack 的往返时延（RTT），
单向延迟上界 ≈ RTT/2（未做 PTP 时间同步，见第 36 章"待确认项"）。

用法：
  source /opt/ros/lyrical/setup.bash
  python3 host/xrce/ros2_cmd_pub.py --value 2 --repeat 10
"""
import argparse
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from std_msgs.msg import UInt8, String


class CmdPub(Node):
    def __init__(self, cmd_topic, ack_topic):
        super().__init__("hpm5e31_cmd_pub")
        qos = QoSProfile(depth=10, history=HistoryPolicy.KEEP_LAST,
                         reliability=ReliabilityPolicy.RELIABLE)
        self.pub = self.create_publisher(UInt8, cmd_topic, qos)
        self.pending = None
        self.rtts = []
        self.acks = 0
        self.create_subscription(String, ack_topic, self._on_ack, 50)

    def _on_ack(self, msg):
        if not msg.data.startswith("cmd="):
            return
        self.acks += 1
        if self.pending is not None:
            self.rtts.append((time.monotonic_ns() - self.pending) / 1e6)
            self.pending = None

    def send(self, value):
        m = UInt8()
        m.data = value & 0xFF
        self.pending = time.monotonic_ns()
        self.pub.publish(m)


def main():
    ap = argparse.ArgumentParser(description="发布 /hpm5e31/cmd 控制板端 LED 并测 RTT")
    ap.add_argument("--value", type=int, default=2, help="0=灭 1=亮 2=翻转")
    ap.add_argument("--repeat", type=int, default=5, help="发送次数")
    ap.add_argument("--interval", type=float, default=0.5, help="发送间隔（秒）")
    ap.add_argument("--cmd-topic", default="/hpm5e31/cmd")
    ap.add_argument("--ack-topic", default="/hpm5e31/hello")
    args = ap.parse_args()

    rclpy.init()
    node = CmdPub(args.cmd_topic, args.ack_topic)
    # 等发现完成
    for _ in range(20):
        rclpy.spin_once(node, timeout_sec=0.1)
    print("[cmd] 等待订阅者匹配（Agent 侧的 XRCE DataReader）…")
    t0 = time.monotonic()
    while time.monotonic() - t0 < 3.0:
        rclpy.spin_once(node, timeout_sec=0.1)
        if node.pub.get_subscription_count() > 0:
            break
    print("[cmd] matched_subscriptions = %d" % node.pub.get_subscription_count())

    for i in range(args.repeat):
        node.send(args.value)
        t_end = time.monotonic() + args.interval
        while time.monotonic() < t_end:
            rclpy.spin_once(node, timeout_sec=0.05)
        print("[cmd] #%d -> UInt8(%d)" % (i + 1, args.value))

    # 收尾，等最后一条 ack
    t_end = time.monotonic() + 1.0
    while time.monotonic() < t_end:
        rclpy.spin_once(node, timeout_sec=0.05)

    rtts = node.rtts
    node.destroy_node()
    rclpy.shutdown()

    print("=" * 68)
    print(" HPM5E31 XRCE cmd -> LED 控制 / 命令往返时延")
    print("=" * 68)
    print(" 发送次数          : %d" % args.repeat)
    print(" 收到 ack          : %d" % node.acks)
    if rtts:
        print(" RTT 最小/中位/最大: %.2f / %.2f / %.2f ms" %
              (min(rtts), sorted(rtts)[len(rtts) // 2], max(rtts)))
        print(" 单向延迟上界(RTT/2): %.2f ms" % (max(rtts) / 2.0))
    else:
        print(" RTT               : 无 ack（检查 Agent 是否运行、板端是否在跑）")
    ok = len(rtts) > 0
    print(" 结论              : %s" % ("PASS" if ok else "FAIL"))
    print("=" * 68)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
