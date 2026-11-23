# host/doip —— DoIP(ISO 13400-2) 上位机（纯 Python 标准库）

配套板端例程：`samples/app/doip_server`（第 31 章）。
只用标准库 `socket/struct/argparse`：不装 scapy、不依赖第三方包。

## 1. 文件

| 文件 | 作用 |
|---|---|
| `doip_client.py` | DoIP 客户端 + CLI：发现/声明/实体状态/路由激活/UDS/大报文/异常用例 |
| `run_tests.py` | pytest 风格回归套件（16 个用例，可 `python3` 直接跑，也能 `pytest`） |
| `run_client.sh` | 封装：自动落 `logs/<时间戳>-host-doip-<mode>.log` |

## 2. 前置

```bash
# 板端在跑 doip_server；主机 enp6s0 = 192.168.2.1/24
ping -c 3 192.168.2.100
python3 --version          # 3.8+；本仓库实测用 $HOME/pyenv/bin/python（3.14）
```

## 3. 用法

```bash
python3 doip_client.py discover --count 5         # UDP 0x0001/0x0002/0x0003 -> 0x0004
python3 doip_client.py announce --secs 5          # 监听广播声明（需板端 doip timer period）
python3 doip_client.py entity                     # 0x4001 实体状态 + 0x4003 电源模式
python3 doip_client.py route --sa 0x0E00          # 0x0005 -> 0x0006
python3 doip_client.py uds                        # 0x10/0x22/0x2E/0x27/0x31/0x19/0x3E
python3 doip_client.py bulk --size 4000 --count 20
python3 doip_client.py err                        # 11 个异常用例
python3 doip_client.py all
python3 run_tests.py                              # 16 用例 + 实测数据表
python3 run_tests.py -k bulk --json /tmp/r.json   # 只跑大报文，输出 JSON
bash run_client.sh all                            # 自动落 logs/
```

## 4. 预期输出（真实片段）

```text
$ python3 doip_client.py discover --count 5
== 车辆发现（UDP 0x0001/0x0002/0x0003 -> 0x0004）==
  [plain] VIN=HPM5E31TESTVIN001 逻辑地址=0x1000 EID=00:00:00:00:D0:0E GID=00:00:00:00:00:01 further=0x00 载荷=32 B
       RTT min/avg/max = 119.3 / 163.7 / 318.4 us（5 次）
  0x0001 车辆识别响应 -> 0x0004                        PASS RTT avg 163.7 us
[discover] 3 项：PASS 3 / FAIL 0 -> PASS

$ python3 doip_client.py uds
== 路由激活 + UDS 诊断序列（0x10/0x22/0x2E/0x27/0x31/0x19/0x3E）==
  路由激活 0x10                                      PASS code=0x10 RTT=1325.2 us
  10 03 扩展会话                                     PASS 50 03 00 32 01 F4
  22 读 4 个 DID                                    PASS VIN=HPM5E31TESTVIN001
  2E F190 未解锁 -> NRC 0x33                        PASS 7F 2E 33
  27 01/02 安全访问                                  PASS 67 02
  31 01 0301 慢例程（NRC 0x78 + P2* 续期）            PASS pending=1 t_ack=171 us t_resp=150.7 ms
[uds] 12 项：PASS 12 / FAIL 0 -> PASS
```

## 5. 异常用例清单（`err` 子命令）

| 用例 | 期望 |
|---|---|
| 未知 SA 0x1234 | 0x0006 响应码 0x00 |
| 未配置 SA 0x0E80 | 0x0006 响应码 0x00 |
| 非法激活类型 0x02 | 0x0006 响应码 0x06 |
| 未激活就发 0x8001 | 0x8003/0x02 |
| 错误 TA=0x2000 | 0x8003/0x03 |
| 非法 SA=0x0000 | 0x8003/0x02 |
| 通用头版本取反错 | 0x0000/0x00 后断链 |
| 未知载荷类型 0x1234 | 0x0000/0x01 |
| 同 SA 双连接 | 新连接 0x03 + 老连接收 0x0007 |
| alive check 超时 | 老连接被踢、新连接可激活 |
| initial inactivity | 约 2 s 后连接断开 |

## 6. 大报文（`bulk`）

- 入向：发一条 4000 B UDS 的 0x8001（0x2E 写 F190 + 填充），板端 ACK 后 UDS 回 NRC 0x13；
  验证的是 **TCP 分帧/重组**，不是 UDS 语义。
- 出向：发 UDS 例程 `31 01 0204 <n:u16> <cnt:u16>`，板端在例程里连发 cnt 条 n 字节 0x8001（伪 UDS 响应 62 F1 90 ...），
  客户端统计条数/长度/总耗时/吞吐。这是 **DoIP 层的吞吐测试注入**，不参与 UDS 状态机。

## 7. 注意

- 收广播声明必须 bind 到 0.0.0.0（默认就是），bind 到 192.168.2.1 收不到 255.255.255.255。
- 板端默认 general inactivity 300 s，跑异常用例前用 `doip timer general 3000` 调小。
- UDS 0x27 的种子-密钥算法与 `components/app_uds` 的 `app_uds_calc_key()` 逐位一致（见 `calc_key()`）。
