# host/rust —— 自研 Rust 上位机工具（candump 解析 / ISO-TP / UDS）

本目录是三个**离线可测**的 Rust 工具，用 `cargo workspace` 组织，**不依赖任何外部
crates**（纯 std），因此在没有外网/没有 cargo 镜像的机器上也能 `cargo build/test` 通过。

| crate | 类型 | 作用 |
|---|---|---|
| `candump2csv` | lib + bin | 解析 can-utils `candump` 日志（紧凑 / 默认间隔两种格式，含 CAN FD 与远程帧）→ 帧率、ID 直方图、周期抖动、乱序检测、CSV |
| `isotp` | lib + bin | ISO-TP（ISO 15765-2）组包/解包库：SF/FF/CF/FC、12 位与 32 位长度、SN 回绕、BS/STmin、padding、CAN FD TX_DL；含 CLI 做 hex 与帧序列互转 |
| `udsdec` | lib + bin | UDS（ISO 14229）服务名 / 子功能 / NRC 名称解析，支持 `--json` |

## 1. 构建与测试

```bash
cd host/rust
cargo build --release          # 三个 bin 落 target/release/
cargo test --release           # 51 个单元测试
```

实测（本机 rustc 1.97.1）：

```text
$ cargo build --release
    Finished `release` profile [optimized] target(s) in 0.25s
$ cargo test --release 2>&1 | grep -E 'test result'
test result: ok. 14 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out   # candump2csv lib
test result: ok. 23 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out   # isotp lib
test result: ok.  2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out   # isotp bin
test result: ok. 10 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out   # udsdec lib
test result: ok.  2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out   # udsdec bin
```

## 2. candump2csv —— candump 日志 → 统计 + CSV

```text
candump2csv <文件...> [--csv out.csv] [--bitrate 500000] [--top 10] [--min-count 1] [--quiet]
cat dump.txt | candump2csv -
```

支持的格式：

```text
(1998638.680000) can0 300#05000000A55AC33C      紧凑格式（candump -L / 带 -t）
(1.0) can0 123##1AABBCC                         CAN FD（## 后 1 位 flags：bit0=BRS）
(1.0) can0 321#R                                远程帧
  can0  300   [8]  05 00 00 00 A5 5A C3 3C       默认间隔格式（可无时间戳）
  can0  123   [3]  11 22 33
  can0  123   [0]  remote request
```

### 2.1 对板端真实日志跑一遍

板端 `samples/app/can_logger` 导出的 candump 段（`logs/20261030-174943-can_logger.log`）：

```bash
awk '/# candump:/{f=1;next} /# end candump/{f=0} f' logs/20261030-174943-can_logger.log > /tmp/real_candump.txt
host/rust/target/release/candump2csv /tmp/real_candump.txt --csv /tmp/real.csv --bitrate 500000
```

实测输出：

```text
CSV 已写入 /tmp/real.csv（20 行数据）
=== 汇总 ===
帧数            : 20
数据字节        : 160
时间跨度        : 0.910 s
平均帧率        : 22.0 帧/s
总线负载(估算)  : 0.49 % @ 500000 bit/s
解析错误行数    : 0

=== ID 直方图（按帧数降序，--min-count 1）===
ID               扩展       帧数         字节       平均周期        抖动σ     乱序
0x300            11       20        160          -          -      8
```

> **真实发现**：这 20 帧的时间戳是**乱序**的（0.680 → 0.978 → 0.281 → 0.582 → 0.881 s，
> 19 个间隔里 8 个为负），所以平均周期/抖动显示为 `-`、乱序列显示 8。解析器本身没问题
> （同一份数据用 `host/canlog/candump_parse.py` 也复现同样现象），已反馈给 can_logger 维护者
> 核实板端导出的时基。

CSV 前几行：

```text
index,ts,iface,id_hex,id_dec,ext,fd,fd_flags,rtr,dlc,data_hex
0,1998638.680000,can0,300,768,0,0,0,0,8,05000000A55AC33C
1,1998638.978000,can0,300,768,0,0,0,0,8,06000000A55AC33C
2,1998638.281000,can0,300,768,0,0,0,0,8,07000000A55AC33C
```

### 2.2 混合格式（11 位 + 29 位）

```bash
printf '  can0  300   [8]  05 00 00 00 A5 5A C3 3C\n  can0  123   [3]  11 22 33\n  can0  18DAF110   [4]  03 22 F1 90\n' | host/rust/target/release/candump2csv - --top 5
```

```text
帧数            : 3
数据字节        : 15
时间跨度        : 0.000 s
解析错误行数    : 0

ID               扩展       帧数         字节       平均周期        抖动σ     乱序
0x123            11        1          3          -          -      0
0x300            11        1          8          -          -      0
0x18DAF110       29        1          4          -          -      0
```

（默认间隔格式不带时间戳，所以时间跨度/帧率/抖动为 0 或 `-`，属预期。）

### 2.3 已知偏差

* 总线负载用近似帧长：经典帧 11 位 = `47 + 8N`、29 位 = `67 + 8N`；FD 11 位 = `67 + 8N`、29 位 = `87 + 8N`。未计位填充（最坏约 +20%），用于横向比较而非绝对线速；
* 默认间隔格式无时间戳，无法算周期抖动（显示 `-`）；
* 时间戳倒退时不丢帧，只统计“乱序”次数，便于发现采集端时基异常。

## 3. isotp —— ISO-TP 组包/解包

### 3.1 库 API

```rust
use isotp::{segment, flow_control, StMin, Reassembler};

// 发送侧：载荷 -> 帧数据段（经典 8 字节，padding=0xCC）
let frames = segment(&[0x22, 0xF1, 0x90], 8, Some(0xCC))?;      // 1 帧
let frames = segment(&payload_20_bytes, 8, Some(0xCC))?;        // FF + 2 x CF

// CAN FD：TX_DL=64，>7 字节单帧自动用转义 SF（0x00 + 长度）
let frames = segment(&payload_30_bytes, 64, Some(0xCC))?;

// 接收侧：状态机，收满一条报文返回 Some(payload)
let mut r = Reassembler::default();
for f in frames {
    if let Some(payload) = r.push(&f)? { /* 完整报文 */ }
}

// 流控帧：BS=8、STmin=50 ms
let fc = flow_control(8, StMin(0x32), 8, Some(0xCC))?;          // 30 08 32 CC CC CC CC CC
```

覆盖的协议细节：SF/FF/CF/FC；FF 12 位长度与 32 位转义长度（>4095 字节）；FD 长 SF 转义；
SN 0..15 回绕与乱序检测；BS/STmin 编码（`0x00~0x7F` 毫秒、`0xF1~0xF9` 百微秒、保留值按 0x7F 处理）；
padding；CAN FD TX_DL 合法集合 8/12/16/20/24/32/48/64。

### 3.2 CLI 实测

```text
$ isotp seg --hex 22F190
载荷 3 字节 -> 1 帧（TX_DL=8）：
  #1  4 B  0322F190

$ isotp seg --text "hpm5e31 isotp demo: 40 bytes payload!!" --tx-dl 64 --pad CC --id 7E0
载荷 38 字节 -> 1 帧（TX_DL=64, pad=0xCC）：
  #1 64 B  002668706D356533312069736F74702064656D6F3A203430206279746573207061796C6F61642121CCCC…

$ isotp pci --hex "10 14 00 11 22 33 44 55"
FF（首帧）：总长 20，数据偏移 2，本帧数据 001122334455

$ isotp pci --hex "21 66 77 88"
CF（连续帧）：SN=1，数据 667788

$ isotp pci --hex "30 08 32 00 00 00 00 00"
FC（流控帧）：状态 Continue，BS=8，STmin=50 ms (raw=0x32)

$ isotp fc --bs 8 --stmin 0x32 --tx-dl 8 --pad CC
FC: 300832CCCCCCCCCC
```

从 candump 日志直接解包（取每行 # 之后的段）：

```bash
candump -L can0 | awk '{print $3}' | sed 's/.*#//' | host/rust/target/release/isotp reasm
```

## 4. udsdec —— UDS 报文名称解析

```text
$ udsdec 22F190
22 F1 90                         请求 SID=0x22 ReadDataByIdentifier（DID=0xF190）

$ udsdec "62 F1 90 48 50 4D 35"
62 F1 90 48 50 4D 35             正响应 SID=0x62 ReadDataByIdentifier（DID=0xF190）

$ udsdec 7F2231
7F 22 31                         否定响应 针对 SID=0x22 ReadDataByIdentifier：NRC=0x31 requestOutOfRange

$ udsdec --json 7F3178
{"kind":"negative","raw":"7F3178","sid":"0x31","service":"RoutineControl","nrc":"0x78","nrc_name":"requestCorrectlyReceived-ResponsePending"}
```

支持 30+ 服务的 SID 名称、22 个常见 NRC、以及 0x10/0x11/0x14/0x19/0x22/0x27/0x2E/0x31/0x3E/0x85 的参数摘要。

## 5. 与 Python 工具的分工

| 场景 | 用哪个 | 理由 |
|---|---|---|
| 需要 python-can / udsoncan / canopen 生态，或画图 | host 下各 `*.py`（pyenv） | 生态全、开发快 |
| 只做文本/二进制解析、要单文件可分发、要零依赖 | **本目录 Rust 工具** | 单个静态二进制，无 Python 环境也能跑 |
| 百万行 candump 的离线批处理 | **本目录 Rust 工具** | 无解释器开销 |

## 6. 性能（粗测，单线程）

| 任务 | 数据量 | 实测 |
|---|---|---|
| candump2csv 解析 + 统计 + CSV | 20 行真实板端日志 | < 10 ms（含进程启动） |
| cargo test --release（51 个测试） | — | < 1 s |
| isotp 组包 5000 字节（FD TX_DL=64）+ 解包回环 | 5000 B | 单元测试 reassemble_5000_bytes_32bit_len 内 < 1 ms |

## 7. 目录

```text
host/rust/
├── Cargo.toml          # workspace（resolver=2，无外部依赖）
├── .gitignore          # 忽略 target/
├── candump2csv/{Cargo.toml,src/{lib.rs,main.rs}}
├── isotp/{Cargo.toml,src/{lib.rs,main.rs}}
└── udsdec/{Cargo.toml,src/{lib.rs,main.rs}}
```
