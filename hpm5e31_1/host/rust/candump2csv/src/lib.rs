//! candump 日志解析与统计（纯 std，无外部 crates）
//!
//! 支持 can-utils candump 的两种常见输出：
//!
//! 1) 紧凑格式（candump -L，或默认带 -t 时）：
//!        (1998638.680000) can0 300#05000000A55AC33C
//!        (1469439874.299591) can0 18DAF110#0322F190
//!        (1469439874.300000) can0 123##1AABBCC      <- CAN FD，## 后 1 位是 flags
//!        (1469439874.300000) can0 123#R             <- 远程帧
//! 2) 默认间隔格式：
//!          can0  300   [8]  05 00 00 00 A5 5A C3 3C
//!          can0  123   [3]  11 22 33
//!          can0  123   [0]  remote request
//!
//! 解析结果为 Frame，再由 analyze() 生成帧率、ID 直方图、周期抖动与总线负载估算。

use std::collections::HashMap;

/// 一帧 CAN/CAN FD 报文
#[derive(Debug, Clone, PartialEq)]
pub struct Frame {
    /// 时间戳（秒）。默认格式不含时间戳时为 None
    pub ts: Option<f64>,
    /// 接口名，如 can0 / vcan0
    pub iface: String,
    /// CAN ID 数值（11 位或 29 位）
    pub id: u32,
    /// 是否扩展帧（29 位）
    pub ext: bool,
    /// 是否 CAN FD
    pub fd: bool,
    /// FD flags 半字节（##&lt;flags&gt;，can-utils 约定 bit0=BRS、bit1=ESI）
    pub fd_flags: u8,
    /// 是否远程帧（RTR）
    pub rtr: bool,
    /// 数据段
    pub data: Vec<u8>,
}

#[derive(Debug, PartialEq)]
pub enum ParseError {
    /// 空行/注释行（调用方应跳过）
    Empty,
    BadTimestamp(String),
    BadId(String),
    /// 数据段十六进制非法（奇数长度或非 hex 字符）
    BadData(String),
    /// 默认格式里缺少 [N] 长度标记
    MissingLenMarker(String),
    /// DLC 与实际数据字节数不一致
    LenMismatch { declared: usize, actual: usize },
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ParseError::Empty => write!(f, "empty line"),
            ParseError::BadTimestamp(s) => write!(f, "bad timestamp: {s}"),
            ParseError::BadId(s) => write!(f, "bad id: {s}"),
            ParseError::BadData(s) => write!(f, "bad data: {s}"),
            ParseError::MissingLenMarker(s) => write!(f, "missing [N] marker: {s}"),
            ParseError::LenMismatch { declared, actual } => {
                write!(f, "dlc mismatch: declared {declared}, actual {actual}")
            }
        }
    }
}

fn hexval(b: u8) -> Option<u8> {
    match b {
        b'0'..=b'9' => Some(b - b'0'),
        b'a'..=b'f' => Some(b - b'a' + 10),
        b'A'..=b'F' => Some(b - b'A' + 10),
        _ => None,
    }
}

/// 解析一串十六进制（允许中间有空格/下划线），返回字节向量
pub fn parse_hex_bytes(s: &str) -> Result<Vec<u8>, ParseError> {
    let digits: Vec<u8> = s
        .bytes()
        .filter(|b| !b.is_ascii_whitespace() && *b != b'_')
        .collect();
    if digits.len() % 2 != 0 {
        return Err(ParseError::BadData(format!("odd hex length: {s}")));
    }
    let mut out = Vec::with_capacity(digits.len() / 2);
    for chunk in digits.chunks(2) {
        let hi = hexval(chunk[0]).ok_or_else(|| ParseError::BadData(s.to_string()))?;
        let lo = hexval(chunk[1]).ok_or_else(|| ParseError::BadData(s.to_string()))?;
        out.push((hi << 4) | lo);
    }
    Ok(out)
}

/// 解析 CAN ID：1~3 位十六进制 = 11 位，4~8 位 = 29 位
fn parse_id(s: &str) -> Result<(u32, bool), ParseError> {
    let v = u32::from_str_radix(s, 16).map_err(|_| ParseError::BadId(s.to_string()))?;
    let ext = match s.len() {
        1..=3 => {
            if v > 0x7FF {
                return Err(ParseError::BadId(format!("11-bit id out of range: {s}")));
            }
            false
        }
        4..=8 => {
            if v > 0x1FFF_FFFF {
                return Err(ParseError::BadId(format!("29-bit id out of range: {s}")));
            }
            true
        }
        _ => return Err(ParseError::BadId(format!("id too long: {s}"))),
    };
    Ok((v, ext))
}

/// 解析一行 candump 文本；注释行/空行返回 ParseError::Empty（调用方跳过即可）
pub fn parse_line(line: &str) -> Result<Frame, ParseError> {
    let mut s = line.trim_end_matches(['\r', '\n']).trim();
    if s.is_empty() || s.starts_with('#') {
        return Err(ParseError::Empty);
    }

    // ---- 可选时间戳 "(...)" ----
    let mut ts = None;
    if s.starts_with('(') {
        let end = s.find(')').ok_or_else(|| ParseError::BadTimestamp(s.to_string()))?;
        let raw = &s[1..end];
        ts = Some(
            raw.trim()
                .parse::<f64>()
                .map_err(|_| ParseError::BadTimestamp(raw.to_string()))?,
        );
        s = s[end + 1..].trim_start();
    }

    let mut it = s.split_whitespace();
    let iface = it.next().ok_or_else(|| ParseError::BadId(s.to_string()))?.to_string();
    let rest: Vec<&str> = it.collect();
    if rest.is_empty() {
        return Err(ParseError::MissingLenMarker(s.to_string()));
    }

    // ---- 紧凑格式：ID#DATA / ID##flagsDATA / ID#R ----
    if let Some(hash) = rest[0].find('#') {
        let id_str = &rest[0][..hash];
        let (id, ext) = parse_id(id_str)?;
        let after = &rest[0][hash + 1..];
        let mut fd = false;
        let mut fd_flags = 0u8;
        let mut payload = after;
        if let Some(stripped) = after.strip_prefix('#') {
            fd = true;
            // can-utils：## 之后紧跟 1 位十六进制 flags，再跟数据
            if stripped.is_empty() {
                return Err(ParseError::BadData(rest[0].to_string()));
            }
            let (fb, tail) = stripped.split_at(1);
            fd_flags = u8::from_str_radix(fb, 16).map_err(|_| ParseError::BadData(rest[0].to_string()))?;
            payload = tail;
        }
        let rtr = payload.eq_ignore_ascii_case("r");
        let data = if rtr || payload.is_empty() {
            Vec::new()
        } else {
            parse_hex_bytes(payload)?
        };
        return Ok(Frame { ts, iface, id, ext, fd, fd_flags, rtr, data });
    }

    // ---- 默认格式：ID [N] b0 b1 ... / ID [N] remote request ----
    let id_str = rest[0];
    let (id, ext) = parse_id(id_str)?;
    let marker = rest
        .get(1)
        .ok_or_else(|| ParseError::MissingLenMarker(s.to_string()))?;
    let inner = marker
        .strip_prefix('[')
        .and_then(|m| m.strip_suffix(']'))
        .ok_or_else(|| ParseError::MissingLenMarker(s.to_string()))?;
    let declared = inner
        .parse::<usize>()
        .map_err(|_| ParseError::MissingLenMarker(s.to_string()))?;
    let body = rest[2..].join(" ");
    let rtr = body.starts_with("remote request") || body.is_empty();
    let data = if rtr { Vec::new() } else { parse_hex_bytes(&body)? };
    if !rtr && declared != data.len() {
        return Err(ParseError::LenMismatch { declared, actual: data.len() });
    }
    Ok(Frame { ts, iface, id, ext, fd: false, fd_flags: 0, rtr, data })
}

/// 单个 ID 的统计
#[derive(Debug, Clone, PartialEq)]
pub struct IdStat {
    pub id: u32,
    pub ext: bool,
    pub fd: bool,
    pub count: usize,
    pub bytes: usize,
    pub mean_period_ms: f64,
    /// 周期抖动：帧间隔标准差（ms）
    pub jitter_std_ms: f64,
    pub min_period_ms: f64,
    pub max_period_ms: f64,
    /// 时间戳倒退次数（采集端时基异常或日志乱序时出现）
    pub out_of_order: usize,
}

/// 整份日志的统计
#[derive(Debug, Clone, PartialEq)]
pub struct Summary {
    pub frames: usize,
    pub bytes: usize,
    pub duration_s: f64,
    pub fps: f64,
    /// 按帧数降序排列
    pub ids: Vec<IdStat>,
    /// 估算总线负载（ppm）
    pub bus_load_ppm: u64,
    pub bitrate: u32,
    pub parse_errors: usize,
}

/// 单帧在总线上的近似位数（不含位填充）
///
/// 经典帧：11 位 = 47 + 8N，29 位 = 67 + 8N（SOF..EOF/ACK 固定开销）
/// CAN FD：含 CRC 固定开销的近似，11 位 = 67 + 8N，29 位 = 87 + 8N
/// 真实帧还要叠加位填充（最坏约 +20%），本公式用于相对比较，见 README「已知偏差」。
pub fn approx_frame_bits(f: &Frame) -> u32 {
    let n = f.data.len() as u32 * 8;
    match (f.fd, f.ext) {
        (false, false) => 47 + n,
        (false, true) => 67 + n,
        (true, false) => 67 + n,
        (true, true) => 87 + n,
    }
}

/// 统计整份日志
pub fn analyze(frames: &[Frame], bitrate: u32, parse_errors: usize) -> Summary {
    let mut total_bits: u64 = 0;
    let mut total_bytes = 0usize;
    for f in frames {
        total_bits += u64::from(approx_frame_bits(f));
        total_bytes += f.data.len();
    }

    let mut ts_min = f64::INFINITY;
    let mut ts_max = f64::NEG_INFINITY;
    for f in frames.iter().filter_map(|f| f.ts) {
        ts_min = ts_min.min(f);
        ts_max = ts_max.max(f);
    }
    let duration_s = if ts_min.is_finite() && ts_max > ts_min { ts_max - ts_min } else { 0.0 };

    // 按 (id, ext) 分组，保持首次出现顺序
    let mut order: Vec<(u32, bool)> = Vec::new();
    let mut groups: HashMap<(u32, bool), Vec<&Frame>> = HashMap::new();
    for f in frames {
        let key = (f.id, f.ext);
        if !groups.contains_key(&key) {
            order.push(key);
        }
        groups.entry(key).or_default().push(f);
    }

    let mut ids: Vec<IdStat> = order
        .into_iter()
        .map(|key| {
            let g = &groups[&key];
            let mut periods = Vec::with_capacity(g.len());
            let mut out_of_order = 0usize;
            for w in g.windows(2) {
                if let (Some(a), Some(b)) = (w[0].ts, w[1].ts) {
                    let d = (b - a) * 1000.0;
                    if d < 0.0 {
                        out_of_order += 1;
                    }
                    periods.push(d);
                }
            }
            let mean = if periods.is_empty() {
                0.0
            } else {
                periods.iter().sum::<f64>() / periods.len() as f64
            };
            let jitter = if periods.len() < 2 {
                0.0
            } else {
                let var = periods.iter().map(|p| (p - mean).powi(2)).sum::<f64>() / periods.len() as f64;
                var.sqrt()
            };
            let minp = periods.iter().cloned().fold(f64::INFINITY, f64::min);
            let maxp = periods.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
            IdStat {
                id: key.0,
                ext: key.1,
                fd: g.iter().any(|f| f.fd),
                count: g.len(),
                bytes: g.iter().map(|f| f.data.len()).sum(),
                mean_period_ms: mean,
                jitter_std_ms: jitter,
                min_period_ms: if minp.is_finite() { minp } else { 0.0 },
                max_period_ms: if maxp.is_finite() { maxp } else { 0.0 },
                out_of_order,
            }
        })
        .collect();
    ids.sort_by(|a, b| b.count.cmp(&a.count).then(a.id.cmp(&b.id)));

    let fps = if duration_s > 0.0 { frames.len() as f64 / duration_s } else { 0.0 };
    let bus_load_ppm = if duration_s > 0.0 && bitrate > 0 {
        ((total_bits as f64 / duration_s) / f64::from(bitrate) * 1_000_000.0) as u64
    } else {
        0
    };

    Summary { frames: frames.len(), bytes: total_bytes, duration_s, fps, ids, bus_load_ppm, bitrate, parse_errors }
}

/// 输出 CSV（表头 + 每帧一行）
pub fn to_csv(frames: &[Frame]) -> String {
    let mut out = String::from("index,ts,iface,id_hex,id_dec,ext,fd,fd_flags,rtr,dlc,data_hex\n");
    for (i, f) in frames.iter().enumerate() {
        let ts = match f.ts {
            Some(t) => format!("{t:.6}"),
            None => String::new(),
        };
        let data_hex: String = f.data.iter().map(|b| format!("{b:02X}")).collect();
        out.push_str(&format!(
            "{i},{ts},{},{:03X},{},{},{},{},{},{},{}\n",
            f.iface,
            f.id,
            f.id,
            u8::from(f.ext),
            u8::from(f.fd),
            f.fd_flags,
            u8::from(f.rtr),
            f.data.len(),
            data_hex
        ));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_compact_classic() {
        let f = parse_line("(1998638.680000) can0 300#05000000A55AC33C").unwrap();
        assert_eq!(f.iface, "can0");
        assert_eq!(f.id, 0x300);
        assert!(!f.ext);
        assert!(!f.fd);
        assert_eq!(f.data, vec![0x05, 0x00, 0x00, 0x00, 0xA5, 0x5A, 0xC3, 0x3C]);
        assert!((f.ts.unwrap() - 1998638.68).abs() < 1e-6);
    }

    #[test]
    fn parse_compact_no_timestamp() {
        let f = parse_line("can1 18DAF110#0322F190").unwrap();
        assert_eq!(f.iface, "can1");
        assert_eq!(f.id, 0x18DAF110);
        assert!(f.ext);
        assert_eq!(f.data, vec![0x03, 0x22, 0xF1, 0x90]);
    }

    #[test]
    fn parse_compact_fd_with_flags() {
        let f = parse_line("(1.000000) can0 123##1AABBCC").unwrap();
        assert!(f.fd);
        assert_eq!(f.fd_flags, 1);
        assert_eq!(f.id, 0x123);
        assert_eq!(f.data, vec![0xAA, 0xBB, 0xCC]);
    }

    #[test]
    fn parse_compact_remote() {
        let f = parse_line("(2.0) can0 321#R").unwrap();
        assert!(f.rtr);
        assert_eq!(f.data.len(), 0);
        assert_eq!(f.id, 0x321);
    }

    #[test]
    fn parse_spaced_default() {
        let f = parse_line("  can0  300   [8]  05 00 00 00 A5 5A C3 3C").unwrap();
        assert_eq!(f.id, 0x300);
        assert_eq!(f.data.len(), 8);
        assert!(f.ts.is_none());
    }

    #[test]
    fn parse_spaced_remote() {
        let f = parse_line("  can0  123   [0]  remote request").unwrap();
        assert!(f.rtr);
        assert!(f.data.is_empty());
    }

    #[test]
    fn parse_comment_and_empty() {
        assert_eq!(parse_line("# hello"), Err(ParseError::Empty));
        assert_eq!(parse_line("   "), Err(ParseError::Empty));
    }

    #[test]
    fn reject_bad_data() {
        assert!(matches!(parse_line("can0 123#AAB"), Err(ParseError::BadData(_))));
        assert!(matches!(parse_line("can0 123#ZZ"), Err(ParseError::BadData(_))));
    }

    #[test]
    fn reject_bad_id() {
        assert!(matches!(parse_line("can0 FFF#00"), Err(ParseError::BadId(_))));
        assert!(matches!(parse_line("can0 123456789#00"), Err(ParseError::BadId(_))));
    }

    #[test]
    fn detect_dlc_mismatch() {
        let e = parse_line("can0 300 [8] 01 02").unwrap_err();
        assert_eq!(e, ParseError::LenMismatch { declared: 8, actual: 2 });
    }

    #[test]
    fn analyze_period_and_jitter() {
        let mut frames = Vec::new();
        for i in 0..10 {
            let jitter = if i % 2 == 0 { -0.0005 } else { 0.0005 };
            frames.push(Frame {
                ts: Some(1.0 + i as f64 * 0.010 + jitter),
                iface: "vcan0".into(),
                id: 0x300,
                ext: false,
                fd: false,
                fd_flags: 0,
                rtr: false,
                data: vec![0; 8],
            });
        }
        let s = analyze(&frames, 500_000, 0);
        assert_eq!(s.frames, 10);
        assert_eq!(s.ids.len(), 1);
        assert!((s.ids[0].mean_period_ms - 10.0).abs() < 0.2);
        assert!(s.ids[0].jitter_std_ms > 0.5 && s.ids[0].jitter_std_ms < 1.5);
    }

    #[test]
    fn analyze_out_of_order_counts() {
        let mk = |ts: f64| Frame {
            ts: Some(ts),
            iface: "can0".into(),
            id: 0x100,
            ext: false,
            fd: false,
            fd_flags: 0,
            rtr: false,
            data: vec![0],
        };
        let frames = vec![mk(1.0), mk(1.1), mk(1.05), mk(1.2)];
        let s = analyze(&frames, 500_000, 0);
        assert_eq!(s.ids[0].out_of_order, 1);
    }

    #[test]
    fn csv_roundtrip_header() {
        let f = parse_line("(1.5) can0 18DAF110#0322F190").unwrap();
        let csv = to_csv(&[f]);
        let mut lines = csv.lines();
        assert!(lines.next().unwrap().starts_with("index,ts,iface,id_hex"));
        let row = lines.next().unwrap();
        assert!(row.contains("18DAF110") && row.contains("0322F190"));
    }

    #[test]
    fn bus_load_estimate() {
        let mut frames = Vec::new();
        for i in 0..100 {
            frames.push(Frame {
                ts: Some(i as f64 * 0.01),
                iface: "can0".into(),
                id: 0x200,
                ext: false,
                fd: false,
                fd_flags: 0,
                rtr: false,
                data: vec![0; 8],
            });
        }
        let s = analyze(&frames, 500_000, 0);
        assert!(s.bus_load_ppm > 20_000 && s.bus_load_ppm < 25_000, "{}", s.bus_load_ppm);
    }
}
