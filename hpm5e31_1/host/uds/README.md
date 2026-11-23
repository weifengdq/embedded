# host/uds —— ISO-TP / UDS 上位机工具

`host/uds/` 是第 29/30 章的配套上位机，全部**只用 Python 标准库**（`common.py` 内自带 SocketCAN 与 ISO-TP 实现），
另外提供 can-isotp / udsoncan / python-can 三种第三方实现的对照路径，用来验证"我们的 ECU 与别人的栈也能互通"。

## 1. 目录与依赖

| 文件 | 作用 |
|---|---|
| `common.py` | 裸 SocketCAN 收发 + ISO-TP 客户端/服务端 + UDS 客户端 + 0x27 算法 |
| `isotp_test.py` | ISO-TP 回显测试（raw / can-isotp / both） |
| `uds_tester.py` | 13 步 UDS 诊断序列（raw / udsoncan） |
| `mini_flasher.py` | 0x34/0x36/0x37 + 0x31 校验 + 0x11 复位的 32 KB 刷写 |
| `obd_scan.py` | OBD-II PID 扫描（01 00/05/0C/0D/1C + 09 02 VIN） |
| `ecu_sim.py` | 主机侧 ECU 模拟器（vcan），用于没有板卡时回归工具本身 |
| `uds_sec.py` | 0x27 种子-密钥算法离线校验（含测试向量） |
| `rust/uds27check/` | 同一算法的 Rust 实现（离线校验，无第三方 crate） |

依赖：

- 裸实现路径：只要 Python 3.8+ 与 Linux SocketCAN（vcan 或真实 CAN 卡）。
- 第三方路径：`$HOME/pyenv` 里已装 python-can 4.6.1、can-isotp 2.0.7、udsoncan 1.26.1。

## 2. 三种被测对象

### 2.1 模拟器（无需板卡，最快）

`@bash
sudo ip link add dev vcan5 type vcan 2>/dev/null || true
sudo ip link set vcan5 up
cd host/uds
PY=$HOME/pyenv/bin/python
$PY ecu_sim.py --channel vcan5 &              # UDS ECU（0x7E0/0x7E8）
$PY uds_tester.py --channel vcan5             # -> 12/12 PASS
kill %1
$PY ecu_sim.py --channel vcan5 --mode echo &  # ISO-TP 回显（0x700/0x708）
$PY isotp_test.py --channel vcan5 --impl both
kill %1
`@

用独立的 vcan5 是为了不和别人的 vcan0（Bench B 网关）打架。

### 2.2 Bench B：板卡 + CAN 网关（真实链路）

按 `docs/应用层开发约定.md` §3，板端跑 `samples/app/can_gateway`（CAN1 ↔ UDP 4000 ↔ 主机 vcan0），
DUT 固件放 CAN0（`isotp_echo` 或 `uds_ecu`），主机侧一条命令完成"烧写 + 串口 + 上位机"：

`@bash
cd hpm5e31_1
source host/can_bridge/vcan_setup.sh
bash scripts/hwrun.sh app/uds_ecu --secs 20 --send "run" \
     --post "timeout 60 python3 host/uds/uds_tester.py --channel vcan0" --lock-wait 1800
`@

注意：网关会引入约 2.3 ms 的往返延迟（第 28 章实测），所以 vcan0 上的 RTT **不能**与板内 0.47~3.1 ms 直接比较。

### 2.3 真实 CAN 卡（可选）

把 `--channel can0` 指过去即可，脚本没有绑定 vcan。

## 3. 真实运行输出

### 3.1 模拟器上的 UDS 序列（raw）

`@text
== UDS 诊断序列（impl=raw, vcan5, 0x7E0/0x7E8）==
  步骤                     响应                           RTT µs     结果
  10 03 扩展会话             50 03 00 32 01 F4            129        PASS P2=50 ms P2*=5000 ms
  22 F190/F195/F187/F18C 62 F1 90 48 50 4D 35 45 33 3 160        PASS 62 B
  2E F190 未解锁            NRC 0x33 securityAccessDenie 29         PASS NRC 0x33 securityAccessDenied
  27 01/02 安全访问          67 02                        34         PASS seed=0xD2E3FB68 key=0x2B589DBE
  2E F190 已解锁            6E F1 90                     22         PASS
  22 F190 回读             62 F1 90 48 50 4D 35 45 33 3 23         PASS 写入的 VIN 已回读
  31 01 0203 自检例程        71 01 02 03 00 01            11         PASS
  19 02 读 DTC            59 02 09 C0 DE 01 04 C1 00 8 30         PASS 15 B
  14 FFFFFF 清故障码         54                           10         PASS
  19 02 清码后为空            59 02 09                     9          PASS 3 B
  3E 00 TesterPresent    7E 00                        8          PASS
  10 01 回默认会话            50 01 00 32 01 F4            8          PASS
[uds_tester] PASS steps=12/12 avg_rtt=39 us total=0.00 s
`@

### 3.2 模拟器上的 udsoncan 对照

`@text
  change_session(extended)         720       PASS
  read_data_by_identifier F190     511       PASS
  write F190 before unlock         282       PASS NRC 0x33 (预期)
  unlock level1                    288       PASS
  write F190 after unlock          183       PASS
  routine_control 0x0203           117       PASS
  read_dtc_information             260       PASS
  clear_dtc                        117       PASS
  tester_present                   106       PASS
  change_session(default)          137       PASS
[uds_tester] PASS steps=10/10
`@

### 3.3 刷写（模拟器）

`@text
  34 请求下载(擦除)                74 20 01 02                              37 us PASS
     maxNumberOfBlockLength=258 (数据 256 B/块)
  36 传输数据                  128 块 32768 B  3560.1 KB/s  总耗时 0.009 s
  37 请求退出传输                  77                                       10 us PASS
  31 01 FF00 校验 CRC32        71 01 FF 00 00 79 1B 3A B8            16188 us PASS
     ECU CRC32=0x791B3AB8 本地=0x791B3AB8 -> MATCH
  31 01 FF01 依赖检查            71 01 FF 01 00                        15978 us PASS
[mini_flasher] PASS 总耗时 0.04 s  刷写速率 3560.1 KB/s
`@

（模拟器的"Flash"是内存，3560 KB/s 不代表真实 Flash；真实板卡刷写数据见
`samples/app/uds_flash/README.md`：总线侧 24.2 KB/s、Flash 裸写 269 KB/s。）

### 3.4 OBD 扫描（模拟器）

`@text
  01 00 支持 PID 位图 = 0x18180001 (rtt=114 us)
  01 05 冷却液温度        = 50.00    ℃    (rtt=88 us)
  01 0C 发动机转速        = 1734.00  rpm  (rtt=16 us)
  01 0D 车速           = 45.00    km/h (rtt=10 us)
  01 1C OBD 标准       = 1.00          (rtt=10 us)
  09 02 VIN = HPM5E31TESTVIN001 (rtt=28 us)
[obd_scan] PASS pids=6/6
`@

## 4. 0x27 算法与 Rust 对照

三个实现必须逐位一致：C（`app_uds_calc_key`）、Python（`common.security_key`）、Rust（`rust/uds27check`）。

`@bash
PY=$HOME/pyenv/bin/python
$PY uds_sec.py --selftest
$PY uds_sec.py --seed 0xD2E3FB68 --level 1     # -> key=0x2B589DBE
cd rust/uds27check && cargo run --release -- --selftest
`@

测试向量（三个实现共用）：

| seed | level | key |
|---|---|---|
| 0x00000000 | 1 | 0x024B1865 |
| 0x00000001 | 1 | 0x77D59865 |
| 0x12345678 | 1 | 0x64BE9E71 |
| 0xFFFFFFFF | 1 | 0x01EF031D |
| 0xD2E3FB68 | 1 | 0x2B589DBE |
| 0x00000001 | 2 | 0xF4D71E64 |
| 0x12345678 | 2 | 0xE7BC1870 |
| 0xFFFFFFFF | 2 | 0x82ED851C |

## 5. 坑

1. **can-isotp 2.x 的 `CanStack` 必须显式 `start()`**，且启动后不能再调 `process()`
   （第一版自己起线程调 process 直接抛 `Cannot call process() after a start()`）。
2. **udsoncan 读未配置的 DID 会报错**：必须先 `cfg["data_identifiers"]` 里登记长度/编解码（本目录已登记 F190/F195/F187/F18C/F191）。
3. **udsoncan 的 DTC 子功能名是驼峰** `reportDTCByStatusMask`（不是 snake_case）。
4. **vcan0 默认不限速**，多帧刷写会把 MCU 网关打爆；Bench B 请先跑 `host/can_bridge/vcan_setup.sh` 加 tbf 限速。
5. 用独立 vcan5 跑模拟器回归，避免与别人的 Bench B 抢 vcan0。
