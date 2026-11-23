//! udsdec —— 把 UDS 报文十六进制解析成服务名/NRC 名
//!
//! 用法：
//!   udsdec 22F190                 解码一个 hex 字符串
//!   udsdec "7F 22 31"             允许空格分隔
//!   udsdec -                     从 stdin 逐行读（每行一个报文）
//!   udsdec --json 22F190         以单行 JSON 输出（供脚本消费）
//!
//! 退出码：0 = 至少解码成功一条；1 = 参数/格式错误

use std::io::{self, BufRead};
use std::process::ExitCode;

use udsdec::{decode, Decoded};

const USAGE: &str = "\
udsdec —— UDS (ISO 14229) 报文名称解析

用法:
  udsdec <HEX>            解码一个报文，如 22F190 / 62F190484D5031 / \"7F 22 31\"
  udsdec -                从 stdin 逐行读取
  udsdec --json <HEX>     以 JSON 输出

说明:
  正响应 = 请求 SID + 0x40；否定响应 = 7F <SID> <NRC>
";

fn hex_bytes(s: &str) -> Result<Vec<u8>, String> {
    let digits: Vec<u8> = s
        .bytes()
        .filter(|b| !b.is_ascii_whitespace() && *b != b'_' && *b != b'#')
        .collect();
    if digits.is_empty() {
        return Err("空的十六进制串".into());
    }
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

fn json_escape(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"")
}

fn to_json(raw: &[u8], d: &Decoded) -> String {
    let raw_hex: String = raw.iter().map(|b| format!("{b:02X}")).collect();
    match d {
        Decoded::Positive { sid, service, detail } => format!(
            "{{\"kind\":\"positive\",\"raw\":\"{raw_hex}\",\"sid\":\"0x{sid:02X}\",\"service\":\"{}\",\"detail\":\"{}\"}}",
            json_escape(service),
            json_escape(detail)
        ),
        Decoded::Negative { sid, service, nrc, nrc_name } => format!(
            "{{\"kind\":\"negative\",\"raw\":\"{raw_hex}\",\"sid\":\"0x{sid:02X}\",\"service\":\"{}\",\"nrc\":\"0x{nrc:02X}\",\"nrc_name\":\"{}\"}}",
            json_escape(service),
            json_escape(nrc_name)
        ),
        Decoded::Request { sid, service, detail } => format!(
            "{{\"kind\":\"request\",\"raw\":\"{raw_hex}\",\"sid\":\"0x{sid:02X}\",\"service\":\"{}\",\"detail\":\"{}\"}}",
            json_escape(service),
            json_escape(detail)
        ),
        Decoded::Unknown { .. } => format!("{{\"kind\":\"unknown\",\"raw\":\"{raw_hex}\"}}"),
    }
}

fn main() -> ExitCode {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() || args[0] == "-h" || args[0] == "--help" {
        print!("{USAGE}");
        return if args.is_empty() { ExitCode::from(1) } else { ExitCode::SUCCESS };
    }
    let json = args.iter().any(|a| a == "--json");
    args.retain(|a| a != "--json");

    let mut ok = 0usize;
    let mut fail = 0usize;
    let mut handle = |raw: &str, json_out: bool| {
        match hex_bytes(raw) {
            Ok(bytes) => {
                let d = decode(&bytes);
                if json_out {
                    println!("{}", to_json(&bytes, &d));
                } else {
                    let hex: String = bytes.iter().map(|b| format!("{b:02X} ")).collect();
                    println!("{:<32} {}", hex.trim_end(), d);
                }
                ok += 1;
            }
            Err(e) => {
                eprintln!("解析失败({raw}): {e}");
                fail += 1;
            }
        }
    };

    if args.first().map(|s| s.as_str()) == Some("-") {
        let stdin = io::stdin();
        for line in stdin.lock().lines() {
            let line = match line {
                Ok(l) => l,
                Err(e) => {
                    eprintln!("读 stdin 失败: {e}");
                    return ExitCode::from(1);
                }
            };
            let t = line.trim();
            if t.is_empty() || t.starts_with('#') {
                continue;
            }
            handle(t, json);
        }
    } else {
        let joined = args.join("");
        handle(&joined, json);
    }

    if ok == 0 {
        eprintln!("{USAGE}");
        return ExitCode::from(1);
    }
    if fail > 0 {
        return ExitCode::from(1);
    }
    ExitCode::SUCCESS
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hex_parse() {
        assert_eq!(hex_bytes("22 F1 90").unwrap(), vec![0x22, 0xF1, 0x90]);
        assert_eq!(hex_bytes("7F2231").unwrap(), vec![0x7F, 0x22, 0x31]);
        assert!(hex_bytes("").is_err());
        assert!(hex_bytes("123").is_err());
    }

    #[test]
    fn json_output() {
        let raw = [0x7F, 0x22, 0x31];
        let d = decode(&raw);
        let j = to_json(&raw, &d);
        assert!(j.contains("\"kind\":\"negative\""));
        assert!(j.contains("requestOutOfRange"));
        assert!(j.contains("\"nrc\":\"0x31\""));
    }
}
