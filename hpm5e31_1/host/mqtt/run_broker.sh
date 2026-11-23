#!/usr/bin/env bash
# host/mqtt/run_broker.sh - 起/停本机 mosquitto 测试实例（只监听车载以太网口 192.168.2.1:1883）
#
# 为什么不用系统服务？系统 mosquitto（systemd）只监听 127.0.0.1，而且用它的配置会把
# 板端测试参数写进系统配置。本脚本起一个独立实例：
#   - 配置：host/mqtt/mosquitto_test.conf 模板 -> $HOME/tmp2/mosq_test.conf
#   - 日志：$HOME/tmp2/mosq_test.log（log_type all，含 PINGREQ/PUBACK）
#   - 保护：listener 只绑 192.168.2.1，不暴露到内网/外网
#
# 注意（Ubuntu 24.04 的坑）：/usr/sbin/mosquitto 被 AppArmor 限定只能读 /etc/mosquitto/**，
# 用 /tmp 或 home 下的配置会报 "Unable to open config file"。这里把二进制复制到 temp2 下
# 运行（路径不匹配 AppArmor profile 即不再受限），不改系统策略、不重启系统服务。
#
# 用法：
#   bash host/mqtt/run_broker.sh start
#   bash host/mqtt/run_broker.sh status
#   bash host/mqtt/run_broker.sh stop
#   bash host/mqtt/run_broker.sh logs 40
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMP=$HOME/temp2
CONF="$TMP/mosq_test.conf"
LOG="$TMP/mosq_test.log"
OUT="$TMP/mosq_test.out"
BIN="$TMP/mosquitto_test_bin"
PIDFILE="$TMP/mosq_test.pid"
BIND_IP=192.168.2.1
PORT=1883

start() {
  mkdir -p "$TMP"
  sed -e "s/@BIND_IP@/$BIND_IP/" -e "s#@LOG@#$LOG#" "$HERE/mosquitto_test.conf" > "$CONF"
  if pgrep -f "$BIN" >/dev/null 2>&1; then
    echo "[broker] already running, restarting"
    stop
  fi
  [ -x "$BIN" ] || cp /usr/sbin/mosquitto "$BIN"
  rm -f "$LOG"
  nohup "$BIN" -c "$CONF" >"$OUT" 2>&1 &
  echo $! > "$PIDFILE"
  for _ in $(seq 1 50); do
    if ss -lnt 2>/dev/null | grep -q "$BIND_IP:$PORT"; then break; fi
    sleep 0.1
  done
  if ss -lnt 2>/dev/null | grep -q "$BIND_IP:$PORT"; then
    echo "[broker] listening on $BIND_IP:$PORT (pid=$(cat "$PIDFILE"), log=$LOG)"
    tail -2 "$LOG" || true
  else
    echo "[broker] START FAILED"; cat "$OUT" || true; exit 1
  fi
}

stop() {
  if [ -f "$PIDFILE" ]; then
    kill "$(cat "$PIDFILE")" 2>/dev/null || true
    rm -f "$PIDFILE"
  fi
  pkill -f "$BIN" 2>/dev/null || true
  sleep 0.3
  if ss -lnt 2>/dev/null | grep -q "$BIND_IP:$PORT"; then
    echo "[broker] still listening on $BIND_IP:$PORT"
    return 1
  fi
  echo "[broker] stopped"
}

status() {
  ss -lntp 2>/dev/null | grep -E "($BIND_IP|127.0.0.1|\[::1\]):$PORT" || echo "[broker] not listening"
  echo "--- recent log"
  tail -5 "$LOG" 2>/dev/null || true
}

case "${1:-}" in
  start) start ;;
  stop) stop ;;
  status) status ;;
  logs) tail -"${2:-40}" "$LOG" ;;
  *) echo "usage: $0 start|stop|status|logs [n]"; exit 1 ;;
esac
