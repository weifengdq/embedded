#!/usr/bin/env bash
# test_5m.sh —— CAN FD 数据段 5 Mbit/s 复测（R8，对应 ch42 42.7 与 U-40-18）
#
# 拓扑：板内 CAN0<->CAN1 直连 + 两端 120Ω 拨码；两端 TCAN1044AV（数据手册 5 Mbit/s 上限）。
# 用法：
#   bash host/gs_usb/test_5m.sh            # 全量（core + ext）
#   bash host/gs_usb/test_5m.sh --core     # 经典 500k / FD 1M-2M / FD 1M-5M 双向突发
#   bash host/gs_usb/test_5m.sh --ext      # DLC 0~64 扫描 / 5M 分段突发 / 8M 记录 / autosuspend
#   bash host/gs_usb/test_5m.sh --soak N   # 1M/5M 满速长测（N 帧，验证 TX 不再卡死）
#
# 测量口径（写进 ch42 时必须一致）：
#   * 收侧用 candump -t a -n N（-t a = 绝对时间戳；-n = 收满 N 帧正常退出并 flush）；
#     帧率 = (收到帧数-1)/(末帧时间戳-首帧时间戳)，时间戳来自内核 CAN 栈接收时刻；
#   * cangen 的 -L 是**字节数**不是 DLC 编码：FD 64 B 必须写 -L 64（写 15 会被内核映射成 16 B，
#     R5 的 test_5m.sh 正是这么写的，所以 R5 那批 5M 数据其实只发了 16 B，R8 已修正）；
#   * 丢帧率 = 1 - rx_delta/tx_delta（tx/rx 取 /sys/class/net/canX/statistics 增量）；
#   * 误码看 canX tx_errors/rx_errors 增量与 berr-counter，BUS-OFF/错误帧看 ip -details；
#   * 每一步后用 dmesg --since 基线扫描 oops/WARN（common.sh 的 gs_check_kernel）。
set -u
. "$(dirname "$0")/common.sh"

PHASES="core ext"
SOAK_N=200000
case "${1:-}" in
    --core) PHASES="core";;
    --ext)  PHASES="ext";;
    --soak) PHASES="soak"; SOAK_N="${2:-200000}";;
    "")     ;;
    *) echo "用法: $0 [--core|--ext|--soak N]"; exit 2;;
esac

gs_begin "5m"
gs_require_ifaces 2 || { gs_end; exit $?; }
ART=$GS_ARTIFACT_DIR/5m-$GS_TS
mkdir -p "$ART"
: > "$ART/tp.jsonl"

stat_of() { cat "/sys/class/net/$1/statistics/$2" 2>/dev/null || echo 0; }
berr_of() { ip -details link show "$1" 2>/dev/null | grep -oE 'berr-counter tx [0-9]+ rx [0-9]+' | head -1; }
# ip -details 先打印 netdev 的 "state UP"，再打印 CAN 的 "state ERROR-ACTIVE"；取最后一个才是 CAN 状态
state_of() { ip -details link show "$1" 2>/dev/null | grep -oE 'state [A-Z-]+' | tail -1; }
dtag() { echo "[$GS_TS $1]"; }
record() { printf '%s\n' "$1" >> "$ART/tp.jsonl"; gs_log "$1"; }

# ---------------- 配置 ----------------
cfg_pair() {
    local nom="$1" db="$2" out rc got
    gs_can_down 0; gs_can_down 1
    out=$($GS_SUDO ip link set can0 type can bitrate "$nom" dbitrate "$db" fd on \
        loopback off listen-only off one-shot off 2>&1); rc=$?
    [ -n "$out" ] && gs_log "  can0 cfg: $out"
    [ "$rc" != 0 ] && return 1
    out=$($GS_SUDO ip link set can1 type can bitrate "$nom" dbitrate "$db" fd on \
        loopback off listen-only off one-shot off 2>&1); rc=$?
    [ -n "$out" ] && gs_log "  can1 cfg: $out"
    [ "$rc" != 0 ] && return 1
    $GS_SUDO ip link set can0 up 2>&1 | tee -a "$GS_LOG"
    $GS_SUDO ip link set can1 up 2>&1 | tee -a "$GS_LOG"
    got=$(ip -details link show can0 | grep -oE 'dbitrate [0-9]+' | head -1 | awk '{print $2}')
    if [ "$got" != "$db" ]; then
        gs_log "  [FAIL] 内核下发的 dbitrate=$got，期望 $db"
        return 1
    fi
    return 0
}

cfg_classic() {
    local nom="$1" out rc
    gs_can_down 0; gs_can_down 1
    out=$($GS_SUDO ip link set can0 type can bitrate "$nom" fd off loopback off listen-only off one-shot off 2>&1); rc=$?
    [ -n "$out" ] && gs_log "  can0 cfg: $out"
    [ "$rc" != 0 ] && return 1
    out=$($GS_SUDO ip link set can1 type can bitrate "$nom" fd off loopback off listen-only off one-shot off 2>&1); rc=$?
    [ -n "$out" ] && gs_log "  can1 cfg: $out"
    [ "$rc" != 0 ] && return 1
    $GS_SUDO ip link set can0 up 2>&1 | tee -a "$GS_LOG"
    $GS_SUDO ip link set can1 up 2>&1 | tee -a "$GS_LOG"
    return 0
}

# ---------------- 突发（单向） ----------------
burst() {
    local tag="$1" nom="$2" db="$3" n="$4" src="$5" dst="$6" dlc="$7"
    local dump="$ART/dump-$tag.txt"
    local tx0 rx0 te0 re0 t0 t1 rc dpid tmo
    gs_step "$(dtag "$tag"): can$src -> can$dst nom=$nom data=$db n=$n"

    if [ "$db" = "0" ]; then
        cfg_classic "$nom" || { record "{\"tag\":\"$tag\",\"status\":\"config_rejected\"}"; gs_fail "$tag 配置被拒"; return 1; }
    else
        cfg_pair "$nom" "$db" || { record "{\"tag\":\"$tag\",\"status\":\"config_rejected\"}"; gs_fail "$tag 配置被拒"; return 1; }
    fi
    gs_log "  can0: $(ip -details link show can0 | grep -E 'bitrate' | tr -s ' ' | tr '\n' ' ')"

    tx0=$(stat_of "can$src" tx_packets); rx0=$(stat_of "can$dst" rx_packets)
    te0=$(stat_of "can$src" tx_errors);  re0=$(stat_of "can$dst" rx_errors)
    : > "$dump"
    $GS_SUDO timeout 30 candump -t a -n "$n" "can$dst" > "$dump" 2>&1 &
    dpid=$!
    sleep 0.5
    t0=$(date +%s%N)
    if [ "$db" = "0" ]; then
        $GS_SUDO timeout -k 2 30 cangen "can$src" -g 0 -n "$n" -L "$dlc" -p 100 -i >/dev/null 2>>"$GS_LOG"
    else
        $GS_SUDO timeout -k 2 30 cangen "can$src" -f -b -g 0 -n "$n" -L "$dlc" -p 100 -i >/dev/null 2>>"$GS_LOG"
    fi
    rc=$?
    t1=$(date +%s%N)
    tmo=0
    while kill -0 "$dpid" 2>/dev/null && [ "$tmo" -lt 150 ]; do sleep 0.1; tmo=$((tmo+1)); done
    kill -TERM "$dpid" 2>/dev/null || true
    wait "$dpid" 2>/dev/null || true

    local tx1 rx1 te1 re1
    tx1=$(stat_of "can$src" tx_packets); rx1=$(stat_of "can$dst" rx_packets)
    te1=$(stat_of "can$src" tx_errors);  re1=$(stat_of "can$dst" rx_errors)
    res=$(python3 - "$tag" "$nom" "$db" "$n" "$src" "$dst" "$dump" "$((tx1-tx0))" "$((rx1-rx0))" \
        "$((te1-te0))" "$((re1-re0))" "$rc" "$(berr_of can0)" "$(berr_of can1)" "$(state_of can0)" \
        "$(state_of can1)" <<'PY'
import json, re, sys
(tag, nom, db, n, src, dst, dump, txd, rxd, tex, rex, rc, b0, b1, st0, st1) = sys.argv[1:17]
n = int(n); txd = int(txd); rxd = int(rxd)
lines = []
try:
    with open(dump, errors="replace") as f:
        lines = [l.rstrip("\n") for l in f]
except FileNotFoundError:
    pass
pat = re.compile(r"\((\d+\.\d+)\)\s+can(\d)\s+([0-9A-Fa-f]+)\s+\[(\d+)\]")
ts, dlcs = [], []
for ln in lines:
    m = pat.search(ln)
    if m:
        ts.append(float(m.group(1))); dlcs.append(int(m.group(4)))
span = (ts[-1] - ts[0]) if len(ts) >= 2 else 0.0
fps = ((len(ts) - 1) / span) if span > 0 else 0.0
loss = (1.0 - rxd / txd) * 100.0 if txd else 100.0
dlc_ok = (len(set(dlcs)) <= 1)
r = {"tag": tag, "nom": int(nom), "dbit": int(db), "n": n, "src": "can"+src, "dst": "can"+dst,
     "tx_delta": txd, "rx_delta": rxd, "recv": len(ts),
     "rx_dlc": (dlcs[0] if dlcs else -1), "dlc_ok": dlc_ok,
     "span_s": round(span, 4), "fps": round(fps, 1),
     "payload_mbps": round(fps * (dlcs[0] if dlcs else 0) * 8 / 1e6, 2),
     "loss_pct": round(loss, 3), "tx_err": int(tex), "rx_err": int(rex), "cangen_rc": int(rc),
     "berr0": b0, "berr1": b1, "state0": st0, "state1": st1,
     "verdict": ("PASS" if (len(ts) == n and loss < 1.0 and int(tex) == 0 and int(rex) == 0
                            and "ERROR-ACTIVE" in st0 and "ERROR-ACTIVE" in st1) else "FAIL")}
print("  %s: recv=%d/%d 丢帧=%.3f%% 帧率=%.1f fps 载荷=%.2f Mbit/s DLC=%d tx_err=%d rx_err=%d %s | %s | %s" %
      (tag, len(ts), n, loss, fps, r["payload_mbps"], r["rx_dlc"], int(tex), int(rex), b0, b1, st0))
print(json.dumps(r, ensure_ascii=False))
PY
)
    gs_log "$res"
    printf '%s\n' "$res" | tail -1 >> "$ART/tp.jsonl"
    if printf '%s' "$res" | tail -1 | grep -q '"verdict": "PASS"'; then
        gs_pass "$tag"
    else
        gs_fail "$tag（见上方 recv/丢帧/错误计数）"
    fi
    return 0
}

# ---------------- 判定汇总 ----------------
verdict_all() {
    python3 - "$ART/tp.jsonl" "$1" <<'PY' | tee -a "$GS_LOG"
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
phase = sys.argv[2]
print("")
print("  ==== %s 汇总 ====" % phase)
need = []
for r in rows:
    if r.get("status") == "config_rejected":
        print("  [INFO] %s: 内核拒绝该 FD 档位" % r["tag"]); continue
    if r.get("status") in ("dlc_sweep", "burst_loop", "soak"):
        print("  %-16s %s" % (r["tag"], r["verdict"]))
        need.append(r); continue
    print("  %-16s %-10s recv=%4d/%-4d 丢帧=%6.3f%% %8.1f fps %6.2f Mbit/s tx_err=%d rx_err=%d %s" %
          (r["tag"], ("%d/%d" % (r["nom"], r["dbit"])) if r["dbit"] else "classic",
           r["recv"], r["n"], r["loss_pct"], r["fps"], r["payload_mbps"], r["tx_err"], r["rx_err"], r["verdict"]))
    need.append(r)
bad = [r["tag"] for r in need if r["verdict"] != "PASS"]
if bad:
    print("  [FAIL] 未达判据: %s" % ", ".join(bad))
else:
    print("  [PASS] 全部档位达判据（丢帧<1%、tx_err/rx_err=0、双向 ERROR-ACTIVE）")
PY
}

# ---------------- DLC 扫描 ----------------
dlc_sweep() {
    local nom="$1" db="$2" src="$3" dst="$4" tag="dlc-$1-$2-can$3"
    local sizes=(0 1 2 3 4 5 6 7 8 12 16 20 24 32 48 64)
    local n=16
    gs_step "$(dtag "$tag"): FD DLC 扫描 $nom/$db，can$src -> can$dst，$n 档"
    cfg_pair "$nom" "$db" || { gs_fail "DLC 扫描配置失败"; return 1; }
    local dump="$ART/dump-$tag.txt" dpid i hex tmo
    : > "$dump"
    $GS_SUDO timeout 20 candump -t a -n "$n" "can$dst" > "$dump" 2>&1 &
    dpid=$!
    sleep 0.4
    for i in "${sizes[@]}"; do
        hex=$(python3 -c "print('11'*$i)")
        if [ -z "$hex" ]; then
            $GS_SUDO timeout 2 cansend "can$src" "123##1" >>"$GS_LOG" 2>&1
        else
            $GS_SUDO timeout 2 cansend "can$src" "123##1$hex" >>"$GS_LOG" 2>&1
        fi
        sleep 0.03
    done
    tmo=0
    while kill -0 "$dpid" 2>/dev/null && [ "$tmo" -lt 150 ]; do sleep 0.1; tmo=$((tmo+1)); done
    kill -TERM "$dpid" 2>/dev/null || true
    wait "$dpid" 2>/dev/null || true
    dres=$(python3 - "$tag" "${sizes[*]}" "$dump" <<'PY'
import json, re, sys
tag, sizes, dump = sys.argv[1], [int(x) for x in sys.argv[2].split()], sys.argv[3]
got = []
pat = re.compile(r"\[(\d+)\]")
for ln in open(dump, errors="replace"):
    m = pat.search(ln)
    if m:
        got.append(int(m.group(1)))
exp = sizes
ok = (got == exp)
bad = [(e, g) for e, g in zip(exp, got) if e != g]
print("  DLC 期望 %s" % exp)
print("  DLC 实收 %s" % got)
if ok:
    print("  [PASS] %s: FD DLC 0~64 共 %d 档全部按原长度收到（BRS 生效）" % (tag, len(exp)))
else:
    print("  [FAIL] %s: 长度不符/丢帧 %s" % (tag, bad))
print(json.dumps({"tag": tag, "status": "dlc_sweep", "expected": exp, "got": got,
                  "verdict": "PASS" if ok else "FAIL"}, ensure_ascii=False))
PY
)
    gs_log "$dres"
    printf '%s\n' "$dres" | grep -E '^\{' >> "$ART/tp.jsonl"
    if printf '%s' "$dres" | grep -q '\[FAIL\]'; then
        gs_fail "$tag（DLC 长度不符/丢帧）"
    else
        gs_pass "$tag: DLC 0~64 全部按原长度收到"
    fi
    return 0
}

# ---------------- 5M 分段突发（突发耐久） ----------------
burst_loop() {
    local nom="$1" db="$2" rounds="$3" n="$4" tag="loop-$1-$2"
    gs_step "$(dtag "$tag"): FD $nom/$db 连发 $rounds 轮 x $n 帧（can0->can1 与 can1->can0 交替）"
    cfg_pair "$nom" "$db" || { gs_fail "突发配置失败"; return 1; }
    local r dir src dst tx0 rx0 dtx drx tot=0 okr=0
    for r in $(seq 1 "$rounds"); do
        if [ $((r % 2)) = 1 ]; then dir=01; else dir=10; fi
        src="${dir:0:1}"; dst="${dir:1:1}"
        tx0=$(stat_of "can$src" tx_packets); rx0=$(stat_of "can$dst" rx_packets)
        $GS_SUDO timeout -k 2 30 cangen "can$src" -f -b -g 0 -n "$n" -L 64 -p 100 -i >/dev/null 2>>"$GS_LOG"
        sleep 0.3
        dtx=$(( $(stat_of "can$src" tx_packets) - tx0 ))
        drx=$(( $(stat_of "can$dst" rx_packets) - rx0 ))
        tot=$((tot+drx))
        if [ "$drx" -ge "$n" ]; then okr=$((okr+1)); fi
        gs_log "  第 $r 轮 can$src->can$dst: tx=$dtx rx=$drx $(state_of "can$src") $(berr_of "can$src")"
    done
    if [ "$okr" = "$rounds" ]; then
        gs_pass "$tag: $rounds 轮 x $n 帧全部收到（累计 $tot 帧，无 TX 队列停住）"
        record "{\"tag\":\"$tag\",\"status\":\"burst_loop\",\"rounds\":$rounds,\"n\":$n,\"total_rx\":$tot,\"ok_rounds\":$okr,\"verdict\":\"PASS\"}"
    else
        gs_fail "$tag: 只 $okr/$rounds 轮收满（累计 $tot 帧）"
        record "{\"tag\":\"$tag\",\"status\":\"burst_loop\",\"rounds\":$rounds,\"n\":$n,\"total_rx\":$tot,\"ok_rounds\":$okr,\"verdict\":\"FAIL\"}"
    fi
    return 0
}

# ---------------- autosuspend 现状 + 短空闲 ----------------
autosuspend_check() {
    gs_step "$(dtag autosuspend): usbcore.autosuspend / 端口 power-control / 10 s 空闲存活"
    gs_log "  usbcore.autosuspend=$(cat /sys/module/usbcore/parameters/autosuspend 2>/dev/null)"
    local dev pctl
    dev=$(gs_usb_present) && gs_log "  gs_usb sysfs 设备: $dev"
    if [ -n "${dev:-}" ]; then
        pctl=$(cat "/sys/bus/usb/devices/$dev/power/control" 2>/dev/null)
        gs_log "  $dev/power/control=$pctl"
    fi
    local i gone=0
    for i in $(seq 1 20); do
        sleep 0.5
        gs_usb_present >/dev/null || gone=$((gone+1))
    done
    if [ "$gone" = 0 ]; then
        gs_pass "autosuspend: 10 s 空闲 20 次轮询 0 掉线（300 s 结论见 ch42 42.7.6 与 U-40-14）"
    else
        gs_fail "autosuspend: 10 s 空闲期间 $gone/20 次轮询设备离线"
    fi
    gs_check_kernel || true
}

# ---------------- 固件计数器（经调试串口，--post 期间串口空闲） ----------------
serial_gsstat() {
    local f=/tmp/gs_serial_stat.$$.txt
    stty -F /dev/ttyACM0 921600 raw -echo 2>/dev/null
    # 先开读端再发命令：tty open 会丢弃此前到达的输入，先 printf 会把响应丢掉
    timeout 1.5 cat /dev/ttyACM0 > "$f" 2>/dev/null &
    local cpid=$!
    sleep 0.2
    printf 'gsstat\n' > /dev/ttyACM0 2>/dev/null
    wait "$cpid" 2>/dev/null
    tr -d '\r' < "$f" | grep -E 'rx_frames=|usb: configured=' | head -2
    rm -f "$f"
}
retry_of() { printf '%s\n' "$1" | grep -oE 'tx_retry=[0-9]+' | head -1 | cut -d= -f2; }
ipstats() { ip -s -d link show can0 2>/dev/null | tr -s ' ' | tr '\n' ' '; }

# ---------------- 满速长测（验证 TX URB 不泄漏 / netdev 队列不停） ----------------
soak() {
    local nom="$1" db="$2" n="$3" tag="soak-$1-$2"
    gs_step "$(dtag "$tag"): 满速 cangen -g 0，$n 帧，can0 -> can1（长测）"
    cfg_pair "$nom" "$db" || { gs_fail "长测配置失败"; return 1; }
    local tx0 rx0 te0 re0 t0 t1 rc dtx drx secs fps loss te1 re1
    tx0=$(stat_of can0 tx_packets); rx0=$(stat_of can1 rx_packets)
    te0=$(stat_of can0 tx_errors);  re0=$(stat_of can1 rx_errors)
    local s0 r0
    s0=$(serial_gsstat)
    gs_log "  长测前 固件: $(printf '%s' "$s0" | tr '\n' ' ')"
    gs_log "  长测前 can0 ip -s: $(ipstats)"
    t0=$(date +%s%N)
    $GS_SUDO timeout -k 5 180 cangen can0 -f -b -g 0 -n "$n" -L 64 -p 100 -i >/dev/null 2>>"$GS_LOG"
    rc=$?
    t1=$(date +%s%N)
    sleep 1
    dtx=$(( $(stat_of can0 tx_packets) - tx0 ))
    drx=$(( $(stat_of can1 rx_packets) - rx0 ))
    secs=$(python3 -c "print(round(($t1-$t0)/1e9, 2))")
    te1=$(( $(stat_of can0 tx_errors) - te0 ))
    re1=$(( $(stat_of can1 rx_errors) - re0 ))
    local s1 r1 retry_d=-1
    s1=$(serial_gsstat)
    gs_log "  长测后 固件: $(printf '%s' "$s1" | tr '\n' ' ')"
    gs_log "  长测后 can0 ip -s: $(ipstats)"
    r0=$(retry_of "$s0"); r1=$(retry_of "$s1")
    if [ -n "$r0" ] && [ -n "$r1" ]; then retry_d=$((r1 - r0)); fi
    fps=$(python3 -c "print(round($n/$secs, 1) if $secs else 0)")
    loss=$(python3 -c "print(round((1-$drx/$dtx)*100, 3) if $dtx else 100)")
    gs_log "  rc=$rc 用时=${secs}s tx_delta=$dtx rx_delta=$drx 丢帧=$loss% 帧率=$fps fps tx_err=$te1 rx_err=$re1 固件tx_retry增量=$retry_d"
    gs_log "  can0: $(state_of can0) $(berr_of can0) / can1: $(state_of can1) $(berr_of can1)"
    if [ "$rc" = 0 ] && [ "$dtx" -ge "$n" ] && [ "$drx" -ge "$n" ] && [ "$te1" = 0 ]; then
        gs_pass "$tag: $n 帧满速发完，tx_delta=$dtx rx_delta=$drx，固件 tx_retry 增量=$retry_d（背压生效、URB 未泄漏、队列未停）"
        record "{\"tag\":\"$tag\",\"status\":\"soak\",\"nom\":$nom,\"dbit\":$db,\"n\":$n,\"secs\":$secs,\"tx_delta\":$dtx,\"rx_delta\":$drx,\"loss_pct\":$loss,\"fps\":$fps,\"tx_err\":$te1,\"rx_err\":$re1,\"tx_retry_delta\":$retry_d,\"verdict\":\"PASS\"}"
    else
        gs_fail "$tag: 长测未达判据（rc=$rc tx=$dtx rx=$drx tx_err=$te1）"
        record "{\"tag\":\"$tag\",\"status\":\"soak\",\"nom\":$nom,\"dbit\":$db,\"n\":$n,\"secs\":$secs,\"tx_delta\":$dtx,\"rx_delta\":$drx,\"loss_pct\":$loss,\"fps\":$fps,\"tx_err\":$te1,\"rx_err\":$re1,\"tx_retry_delta\":$retry_d,\"verdict\":\"FAIL\"}"
    fi
    return 0
}

# ================= core =================
if [ "${PHASES/core/}" != "$PHASES" ]; then
    gs_step "==== core：经典 500k + FD 1M/2M + FD 1M/5M（双向突发） ===="
    burst classic-500k-01 500000 0 2000 0 1 8
    burst classic-500k-10 500000 0 2000 1 0 8
    burst fd-1m2m-01 1000000 2000000 2000 0 1 64
    burst fd-1m2m-10 1000000 2000000 2000 1 0 64
    burst fd-1m5m-01 1000000 5000000 4000 0 1 64
    burst fd-1m5m-10 1000000 5000000 4000 1 0 64
    gs_check_kernel || { gs_end; exit 9; }
    verdict_all "core"
fi

# ================= ext =================
if [ "${PHASES/ext/}" != "$PHASES" ]; then
    gs_step "==== ext：DLC 扫描 + 5M 分段突发 + 8M 记录 + autosuspend ===="
    dlc_sweep 1000000 5000000 0 1
    dlc_sweep 1000000 5000000 1 0
    burst_loop 1000000 5000000 6 1000
    burst fd-1m8m-01 1000000 8000000 1000 0 1 64
    autosuspend_check
    gs_check_kernel || { gs_end; exit 9; }
    verdict_all "ext"
fi

# ================= soak =================
if [ "${PHASES/soak/}" != "$PHASES" ]; then
    gs_step "==== soak：满速长测 ===="
    soak 1000000 5000000 "$SOAK_N"
    gs_check_kernel || { gs_end; exit 9; }
    verdict_all "soak"
fi

for i in 0 1; do gs_can_down "$i"; done
gs_end

