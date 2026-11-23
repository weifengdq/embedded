#!/usr/bin/env bash
# test_ros2_bridge.sh —— XRCE-DDS Agent -> ROS 2 桥接验证（板端固件运行期间执行）
#
# 前置：
#   1) Agent 已启动（bash host/xrce/run_agent.sh -d）
#   2) 板端正在跑 samples/app/xrce_pubsub（或 xrce_ros2_bridge）固件
#
# 典型用法（与 hwrun 配合，串口抓完后持锁执行本脚本，一次占锁完成全部验证）：
#   bash scripts/hwrun.sh app/xrce_pubsub --secs 25 --send "stat" \
#        --post "timeout 70 bash host/xrce/test_ros2_bridge.sh" --lock-wait 1800
#
# 注意：rclpy 属于系统 Python（ROS 2 Lyrical 装在 /opt/ros/lyrical，Python 3.14），
#       不要用 $HOME/pyenv（那是 python-can 等工具的 venv，没有 rclpy）。
set +u
source "${ROS_DIR:-/opt/ros/lyrical}/setup.bash"
export LD_LIBRARY_PATH="${XRCE_BUILD_DIR:-${TMPDIR:-/tmp}/xrce_agent_build}/agent_install/lib:${LD_LIBRARY_PATH:-}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="${ROS_PYTHON:-python3}"
DUR="${DUR:-10}"
STATUS_TOPIC="${STATUS_TOPIC:-/hpm5e31/hello}"
RC=0

# 环境自检：rclpy 只在系统 Python（Ubuntu 3.14）里，pyenv 里没有
if ! "$PY" -c "import rclpy" >/dev/null 2>&1; then
    echo "FAIL: 当前 python3 (\$("$PY" -V 2>&1)) 没有 rclpy；请先 source /opt/ros/lyrical/setup.bash，"
    echo "      并使用系统 python3（不要用 $HOME/pyenv/bin/python）。"
    exit 4
fi

echo "==================== [1] Agent 监听状态 ===================="
ss -lunp 2>/dev/null | grep -F ":2018" || { echo "FAIL: Agent 未监听 2018"; exit 2; }

echo "==================== [2] ROS 2 侧可见的 XRCE 话题 ===================="
timeout 20 ros2 topic list 2>&1 | grep -F "hpm5e31" || { echo "FAIL: ROS 2 看不到 hpm5e31 话题"; exit 3; }

echo "==================== [3] ros2 topic hz /hpm5e31/adc（5 s 窗口） ===================="
timeout 25 ros2 topic hz /hpm5e31/adc --window 40 > /tmp/xrce_hz.txt 2>&1 || true
head -6 /tmp/xrce_hz.txt
grep -q 'average rate\|failed to initialize' /tmp/xrce_hz.txt || { echo "  重试 hz…"; timeout 25 ros2 topic hz /hpm5e31/adc --window 40 2>&1 | head -6; }

echo "==================== [4] ros2 topic echo --once ===================="
timeout 15 ros2 topic echo /hpm5e31/adc --once > /tmp/xrce_echo.txt 2>&1 || true
head -12 /tmp/xrce_echo.txt

echo "==================== [5] adc 订阅统计（频率/抖动/丢帧） ===================="
timeout 40 "$PY" "$HERE/ros2_adc_sub.py" --duration "$DUR" 2>&1 || RC=1

echo "==================== [6] cmd -> LED 控制 + 命令往返 RTT ===================="
timeout 40 "$PY" "$HERE/ros2_cmd_pub.py" --value 2 --repeat 6 --interval 0.4 --ack-topic "$STATUS_TOPIC" 2>&1 || RC=1

echo "==================== [7] hello 话题（String，板端 ack 也在这里） ===================="
timeout 12 ros2 topic echo "$STATUS_TOPIC" --once > /tmp/xrce_status.txt 2>&1 || true
head -6 /tmp/xrce_status.txt

echo "BRIDGE_DONE rc=$RC"
exit $RC
