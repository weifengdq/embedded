//! candump2csv —— 把 candump 日志转成 CSV 并打印统计（帧率 / ID 直方图 / 周期抖动 / 总线负载）
//!
//! 用法：
//!   candump2csv <日志文件...> [--csv out.csv] [--bitrate 500000] [--top 10] [--min-count 1] [--quiet]
//!   cat dump.txt | candump2csv -            （- 表示从 stdin 读）
//!
//! 退出码：0 = 解析到帧（且无解析错误）；2 = 有解析错误；1 = 一个帧都没读到。

use std::fs::File;
use std::io::{self, BufRead, BufReader, Write};
use std::process::ExitCode;

use candump2csv::{analyze, parse_line, to_csv, Frame, ParseError, Summary};

const USAGE: &str = "\
candump2csv —— candump 日志解析 / 统计 / CSV 导出

用法:
  candump2csv <文件...> [选项]
  cat dump.txt | candump2csv -

选项:
  --csv <路径>     把逐帧数据写成 CSV（默认不写）
  --bitrate <bps>  总线波特率，用于负载估算（默认 500000）
  --top <N>        ID 直方图只显示前 N 个（默认 10，0=全部）
  --min-count <N>  统计时忽略出现次数 < N 的 ID（仅影响直方图显示）
  --quiet          不打印统计表，只写 CSV
  -h, --help       显示本帮助

支持的格式:
  (1998638.680000) can0 300#05000000A55AC33C    紧凑格式（candump -L）
  (1.0) can0 123##1AABBCC                      CAN FD（## 后 1 位 flags）
  (1.0) can0 321#R                             远程帧
    can0  300   [8]  05 00 00 00 A5 5A C3 3C    默认间隔格式（可无时间戳）
";

#[derive(Default)]
struct Args {
    files: Vec<String>,
    csv: Option<String>,
    bitrate: u32,
    top: usize,
    min_count: usize,
    quiet: bool,
}

fn parse_args() -> Result<Args, String> {
    let mut a = Args { bitrate: 500_000, top: 10, min_count: 1, ..Default::default() };
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        match arg.as_str() {
            "-h" | "--help" => {
                print!("{USAGE}");
                std::process::exit(0);
            }
            "--csv" => a.csv = Some(it.next().ok_or("--csv 需要一个路径")?),
            "--bitrate" => {
                a.bitrate = it.next().ok_or("--bitrate 需要一个数值")?.parse().map_err(|_| "bitrate 非法")?
            }
            "--top" => a.top = it.next().ok_or("--top 需要一个数值")?.parse().map_err(|_| "top 非法")?,
            "--min-count" => {
                a.min_count = it.next().ok_or("--min-count 需要一个数值")?.parse().map_err(|_| "min-count 非法")?
            }
            "--quiet" => a.quiet = true,
            other => a.files.push(other.to_string()),
        }
    }
    if a.files.is_empty() {
        return Err("没有输入文件（用 - 表示 stdin）".into());
    }
    Ok(a)
}

fn read_lines(path: &str) -> io::Result<Box<dyn BufRead>> {
    if path == "-" {
        Ok(Box::new(BufReader::new(io::stdin())))
    } else {
        Ok(Box::new(BufReader::new(File::open(path)?)))
    }
}

fn print_summary(s: &Summary, top: usize, min_count: usize) {
    println!("=== 汇总 ===");
    println!("帧数            : {}", s.frames);
    println!("数据字节        : {}", s.bytes);
    println!("时间跨度        : {:.3} s", s.duration_s);
    println!("平均帧率        : {:.1} 帧/s", s.fps);
    println!(
        "总线负载(估算)  : {:.2} % @ {} bit/s",
        s.bus_load_ppm as f64 / 10_000.0,
        s.bitrate
    );
    println!("解析错误行数    : {}", s.parse_errors);
    println!();
    println!("=== ID 直方图（按帧数降序，--min-count {min_count}）===");
    println!(
        "{:<12} {:>6} {:>8} {:>10} {:>10} {:>10} {:>6}",
        "ID", "扩展", "帧数", "字节", "平均周期", "抖动σ", "乱序"
    );
    let mut shown = 0usize;
    for st in &s.ids {
        if st.count < min_count.max(1) {
            continue;
        }
        if top != 0 && shown >= top {
            break;
        }
        let id_s = if st.ext { format!("0x{:08X}", st.id) } else { format!("0x{:03X}", st.id) };
        let period = if st.mean_period_ms > 0.0 {
            format!("{:.3} ms", st.mean_period_ms)
        } else {
            "-".to_string()
        };
        let jitter = if st.mean_period_ms > 0.0 {
            format!("{:.3} ms", st.jitter_std_ms)
        } else {
            "-".to_string()
        };
        println!(
            "{:<12} {:>6} {:>8} {:>10} {:>10} {:>10} {:>6}",
            id_s,
            if st.ext { "29" } else { "11" },
            st.count,
            st.bytes,
            period,
            jitter,
            st.out_of_order
        );
        shown += 1;
    }
    if shown == 0 {
        println!("(没有满足 min-count 的 ID)");
    }
    let hidden = s.ids.iter().filter(|st| st.count >= min_count.max(1)).count().saturating_sub(shown);
    if hidden > 0 {
        println!("… 其余 {hidden} 个 ID 未显示（--top 0 显示全部）");
    }
}

fn main() -> ExitCode {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("错误: {e}\n");
            eprint!("{USAGE}");
            return ExitCode::from(1);
        }
    };

    let mut frames: Vec<Frame> = Vec::new();
    let mut errors = 0usize;
    let mut bad_samples: Vec<String> = Vec::new();

    for path in &args.files {
        let reader = match read_lines(path) {
            Ok(r) => r,
            Err(e) => {
                eprintln!("无法打开 {path}: {e}");
                return ExitCode::from(1);
            }
        };
        for line in reader.lines() {
            let line = match line {
                Ok(l) => l,
                Err(e) => {
                    eprintln!("读 {path} 出错: {e}");
                    return ExitCode::from(1);
                }
            };
            match parse_line(&line) {
                Ok(f) => frames.push(f),
                Err(ParseError::Empty) => {}
                Err(e) => {
                    errors += 1;
                    if bad_samples.len() < 5 {
                        bad_samples.push(format!("{path}: {e} | {}", line.trim()));
                    }
                }
            }
        }
    }

    if frames.is_empty() {
        eprintln!("没有解析到任何 CAN 帧（检查格式）");
        for b in &bad_samples {
            eprintln!("  {b}");
        }
        return ExitCode::from(1);
    }

    if let Some(csv_path) = &args.csv {
        match File::create(csv_path) {
            Ok(mut f) => {
                if let Err(e) = f.write_all(to_csv(&frames).as_bytes()) {
                    eprintln!("写 CSV 失败: {e}");
                    return ExitCode::from(1);
                }
                if !args.quiet {
                    println!("CSV 已写入 {csv_path}（{} 行数据）", frames.len());
                }
            }
            Err(e) => {
                eprintln!("无法创建 {csv_path}: {e}");
                return ExitCode::from(1);
            }
        }
    }

    let summary = analyze(&frames, args.bitrate, errors);
    if !args.quiet {
        print_summary(&summary, args.top, args.min_count);
        if !bad_samples.is_empty() {
            println!("\n=== 无法解析的样例行（最多 5 条）===");
            for b in &bad_samples {
                println!("  {b}");
            }
        }
    }

    if errors > 0 {
        ExitCode::from(2)
    } else {
        ExitCode::SUCCESS
    }
}
