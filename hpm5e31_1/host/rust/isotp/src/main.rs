//! isotp —— ISO-TP(ISO 15765-2) 命令行工具：hex 载荷 <-> 帧序列
//!
//! 用法：
//!   isotp seg --hex "22F190" [--tx-dl 8] [--pad CC] [--id 7E0]     载荷 -> 帧序列
//!   isotp seg --text "hello" --tx-dl 64 --pad CC                ASCII 载荷 -> 帧序列
//!   isotp reasm [--file frames.txt]                             帧序列 -> 载荷（每行一帧的 hex 数据段）
//!   isotp fc --bs 8 --stmin 0x32 --tx-dl 8 --pad CC             生成流控帧
//!   isotp pci --hex "10 14 ..."                                 打印每帧 PCI 类型/解析结果
//!
//! 帧序列输出格式与 candump 兼容：可带 --id 前缀直接喂给 cansend（如 7E0#0322F190）。

use std::io::{self, BufRead};
use std::process::ExitCode;

use isotp::{flow_control, frame_type, parse_first_frame, parse_flow_control, segment, FrameType, Reassembler, StMin};

const USAGE: &str = "\
isotp —— ISO-TP (ISO 15765-2) 编解码工具

子命令:
  seg    --hex <HEX> | --text <STR>   组包：载荷 -> 帧序列
         选项: --tx-dl <8|12|16|20|24|32|48|64>  --pad <HEX字节>  --id <CAN ID, 十六进制>
  reasm  [--file <路径>]              解包：每行一帧 hex 数据段 -> 完整载荷（默认 stdin）
  fc     --bs <N> --stmin <0xNN>      生成流控帧
         选项: --tx-dl, --pad
  pci    --hex <HEX>                  仅解析单帧 PCI

示例:
  isotp seg --hex 22F190
  isotp seg --text \"hello isotp\" --tx-dl 64 --pad CC --id 7E0
  isotp fc --bs 8 --stmin 0x32 --pad CC
  candump -L can0 | awk '{print $3}' | sed 's/.*#//' | isotp reasm
";

fn hex_bytes(s: &str) -> Result<Vec<u8>, String> {
    // 支持直接粘贴 candump 片段：7E0#0322F190 -> 取 # 之后的数据段
    let s = match s.rsplit_once('#') {
        Some((_, data)) => data,
        None => s,
    };
    let digits: Vec<u8> = s
        .bytes()
        .filter(|b| !b.is_ascii_whitespace() && *b != b'_')
        .collect();
    if digits.len() % 2 != 0 {
        return Err(format!("十六进制长度必须是偶数: {s}"));
    }
    let mut out = Vec::new();
    for c in digits.chunks(2) {
        let hi = (c[0] as char).to_digit(16).ok_or_else(|| format!("非法 hex: {}", c[0] as char))?;
        let lo = (c[1] as char).to_digit(16).ok_or_else(|| format!("非法 hex: {}", c[1] as char))?;
        out.push((hi as u8) << 4 | lo as u8);
    }
    Ok(out)
}

fn hex_str(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02X}")).collect()
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() || args[0] == "-h" || args[0] == "--help" {
        print!("{USAGE}");
        return if args.is_empty() { ExitCode::from(1) } else { ExitCode::SUCCESS };
    }
    match run(&args[1..], &args[0]) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("错误: {e}");
            ExitCode::from(1)
        }
    }
}

struct Opts {
    hex: Option<String>,
    text: Option<String>,
    file: Option<String>,
    tx_dl: usize,
    pad: Option<u8>,
    id: Option<u32>,
    bs: u8,
    stmin: u8,
}

fn parse_opts(args: &[String]) -> Result<Opts, String> {
    let mut o = Opts { hex: None, text: None, file: None, tx_dl: 8, pad: None, id: None, bs: 0, stmin: 0 };
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--hex" => o.hex = Some(it.next().ok_or("--hex 缺少参数")?.clone()),
            "--text" => o.text = Some(it.next().ok_or("--text 缺少参数")?.clone()),
            "--file" => o.file = Some(it.next().ok_or("--file 缺少参数")?.clone()),
            "--tx-dl" => o.tx_dl = it.next().ok_or("--tx-dl 缺少参数")?.parse().map_err(|_| "tx-dl 非法")?,
            "--pad" => {
                let v = hex_bytes(it.next().ok_or("--pad 缺少参数")?)?;
                if v.len() != 1 {
                    return Err("--pad 需要 1 字节".into());
                }
                o.pad = Some(v[0]);
            }
            "--id" => {
                let raw = it.next().ok_or("--id 缺少参数")?.trim_start_matches("0x");
                o.id = Some(u32::from_str_radix(raw, 16).map_err(|_| "id 非法")?);
            }
            "--bs" => o.bs = it.next().ok_or("--bs 缺少参数")?.parse().map_err(|_| "bs 非法")?,
            "--stmin" => {
                let raw = it.next().ok_or("--stmin 缺少参数")?.trim_start_matches("0x");
                o.stmin = u8::from_str_radix(raw, 16).map_err(|_| "stmin 非法")?;
            }
            other => return Err(format!("未知参数 {other}")),
        }
    }
    Ok(o)
}

fn print_frame(data: &[u8], o: &Opts) {
    let id = match o.id {
        Some(id) if id > 0x7FF => format!("{id:08X}#"),
        Some(id) => format!("{id:03X}#"),
        None => String::new(),
    };
    println!("{id}{}", hex_str(data));
}

fn run(args: &[String], cmd: &str) -> Result<(), String> {
    if cmd == "reasm" {
        let o = parse_opts(args)?;
        let reader: Box<dyn BufRead> = match &o.file {
            Some(f) => Box::new(io::BufReader::new(std::fs::File::open(f).map_err(|e| e.to_string())?)),
            None => Box::new(io::BufReader::new(io::stdin())),
        };
        let mut r = Reassembler::default();
        let mut done = 0usize;
        for line in reader.lines() {
            let line = line.map_err(|e| e.to_string())?;
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            // 允许 "7E0#..." 或纯 hex
            let payload = match line.split_once('#') {
                Some((_, d)) => d,
                None => line,
            };
            let data = hex_bytes(payload)?;
            match r.push(&data).map_err(|e| e.to_string())? {
                Some(p) => {
                    done += 1;
                    println!("[{done}] {} 字节: {}", p.len(), hex_str(&p));
                }
                None => {}
            }
        }
        if done == 0 {
            return Err("没有解析出完整报文（帧不完整或序号错误）".into());
        }
        return Ok(());
    }

    let o = parse_opts(args)?;
    match cmd {
        "seg" => {
            let payload: Vec<u8> = if let Some(h) = &o.hex {
                hex_bytes(h)?
            } else if let Some(t) = &o.text {
                t.as_bytes().to_vec()
            } else {
                return Err("需要 --hex 或 --text".into());
            };
            let frames = segment(&payload, o.tx_dl, o.pad).map_err(|e| e.to_string())?;
            println!("载荷 {} 字节 -> {} 帧（TX_DL={}{}）：", payload.len(), frames.len(), o.tx_dl,
                     match o.pad { Some(p) => format!(", pad=0x{p:02X}"), None => String::new() });
            for (i, f) in frames.iter().enumerate() {
                println!("  #{} {:2} B  {}", i + 1, f.len(), hex_str(f));
            }
            Ok(())
        }
        "fc" => {
            let fc = flow_control(o.bs, StMin(o.stmin), o.tx_dl, o.pad).map_err(|e| e.to_string())?;
            println!("FC: {}", hex_str(&fc));
            print_frame(&fc, &o);
            Ok(())
        }
        "pci" => {
            let h = o.hex.ok_or("pci 需要 --hex")?;
            let data = hex_bytes(&h)?;
            match frame_type(&data) {
                Ok(FrameType::Single) => {
                    let len = usize::from(data[0] & 0x0F);
                    let (len, off) = if len == 0 { (usize::from(data[1]), 2) } else { (len, 1) };
                    println!("SF（单帧）：长度 {len}，数据 {}（偏移 {off}）", hex_str(&data[off..(off + len).min(data.len())]));
                }
                Ok(FrameType::First) => {
                    let info = parse_first_frame(&data).map_err(|e| e.to_string())?;
                    println!("FF（首帧）：总长 {}，数据偏移 {}，本帧数据 {}",
                             info.total_len, info.data_offset, hex_str(&data[info.data_offset..]));
                }
                Ok(FrameType::Consecutive) => {
                    println!("CF（连续帧）：SN={}，数据 {}", data[0] & 0x0F, hex_str(&data[1..]));
                }
                Ok(FrameType::FlowControl) => {
                    let (st, bs, sm) = parse_flow_control(&data).map_err(|e| e.to_string())?;
                    println!("FC（流控帧）：状态 {:?}，BS={}，STmin={}", st, bs, sm.describe());
                }
                Err(e) => return Err(e.to_string()),
            }
            Ok(())
        }
        other => Err(format!("未知子命令 {other}\n{USAGE}")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hex_helpers() {
        assert_eq!(hex_bytes("03 22 F1 90").unwrap(), vec![0x03, 0x22, 0xF1, 0x90]);
        assert_eq!(hex_bytes("7E0#0322F190").unwrap(), vec![0x03, 0x22, 0xF1, 0x90]);
        assert!(hex_bytes("123").is_err());
        assert_eq!(hex_str(&[0x0A, 0xFF]), "0AFF");
    }

    #[test]
    fn text_payload_segmentation() {
        // CLI 里 --text 走 ASCII，这里只验证字节序列本身
        let payload = b"hello isotp".to_vec();
        let frames = segment(&payload, 64, Some(0xCC)).unwrap();
        assert_eq!(frames.len(), 1);
        assert_eq!(frames[0].len(), 64);
    }
}
