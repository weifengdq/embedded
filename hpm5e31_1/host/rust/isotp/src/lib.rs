//! ISO-TP（ISO 15765-2）组包/解包库（纯 std，无外部 crates）
//!
//! 覆盖：
//!   * 四种 PCI 帧：单帧 SF / 首帧 FF / 连续帧 CF / 流控帧 FC
//!   * 长度编码：SF 短格式（<=7 字节）与转义格式（FD 长 SF）、FF 12 位与 32 位转义长度
//!   * 序号 SN 0..15 回绕、乱序检测
//!   * 流控参数：BS（块大小）、STmin（0x00~0x7F 毫秒 / 0xF1~0xF9 百微秒）
//!   * 填充字节（padding，常见 0xCC/0xAA/0x00）
//!   * CAN FD：TX_DL 8/12/16/20/24/32/48/64 合法集合校验
//!
//! 设计说明：
//!   * 库只处理“数据段”，不关心 CAN ID 与仲裁，便于单元测试与复用；
//!   * 组包（segment）是发送侧行为；流控（flow_control）是接收侧行为；两者分开；
//!   * 解包（Reassembler）是接收侧状态机，push 每帧返回 Some(payload) 表示收完一条报文。

/// 经典 CAN 的 TX_DL
pub const CLASSIC_TX_DL: usize = 8;
/// CAN FD 允许的 TX_DL 集合（ISO 15765-2:2016 Table 4）
pub const FD_TX_DLS: [usize; 8] = [8, 12, 16, 20, 24, 32, 48, 64];

/// PCI 类型
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FrameType {
    Single,
    First,
    Consecutive,
    FlowControl,
}

/// 流控状态
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FlowStatus {
    /// 0：继续发送（Continue To Send）
    Continue,
    /// 1：等待（Wait）
    Wait,
    /// 2：溢出/中止（Overflow / Abort）
    Overflow,
}

impl FlowStatus {
    pub fn from_nibble(v: u8) -> Option<Self> {
        match v {
            0 => Some(FlowStatus::Continue),
            1 => Some(FlowStatus::Wait),
            2 => Some(FlowStatus::Overflow),
            _ => None,
        }
    }
}

/// STmin：ISO-TP 的最小间隔参数
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct StMin(pub u8);

impl StMin {
    /// 解析后的实际等待时间（微秒）
    pub fn to_micros(self) -> u32 {
        match self.0 {
            0x00..=0x7F => u32::from(self.0) * 1000,
            0xF1..=0xF9 => (u32::from(self.0) - 0xF0) * 100,
            // 0x80~0xF0 与 0xFA~0xFF 为保留值，ISO 要求按 0x7F 处理
            _ => 127_000,
        }
    }

    pub fn describe(self) -> String {
        let us = self.to_micros();
        if us % 1000 == 0 {
            format!("{} ms (raw=0x{:02X})", us / 1000, self.0)
        } else {
            format!("{} us (raw=0x{:02X})", us, self.0)
        }
    }
}

/// 首帧信息
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FirstFrameInfo {
    /// 报文总长度（字节）
    pub total_len: usize,
    /// 本帧数据起始偏移（2 = 12 位长度格式，6 = 32 位转义长度格式）
    pub data_offset: usize,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum IsoTpError {
    /// 空数据段
    Empty,
    /// 数据段太短，连 PCI 都不完整
    TooShort,
    /// 未知帧类型
    UnknownFrameType(u8),
    /// SF 声明的长度超过本帧可用字节
    SingleFrameOverflow { declared: usize, available: usize },
    /// FF 声明长度 0
    FirstFrameLengthZero,
    /// 未收到 FF 就收到 CF
    UnexpectedConsecutiveFrame,
    /// 接收过程中又收到 FF（这里按 ISO 语义重启，不报错；保留变体供上层使用）
    UnexpectedFirstFrame,
    /// 序号不连续
    SequenceMismatch { expected: u8, got: u8 },
    /// 总长度超过上限
    Overflow { limit: usize },
    /// 数据段长度非法（TX_DL 不在合法集合内）
    InvalidTxDl(usize),
    /// 载荷过长
    PayloadTooLong(usize),
    /// 流控帧截断
    FlowControlTruncated,
    /// 当前状态不接受该帧（例如接收侧收到 FC）
    NotReady,
}

impl std::fmt::Display for IsoTpError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            IsoTpError::Empty => write!(f, "空数据段"),
            IsoTpError::TooShort => write!(f, "数据段太短"),
            IsoTpError::UnknownFrameType(t) => write!(f, "未知帧类型 PCI=0x{t:02X}"),
            IsoTpError::SingleFrameOverflow { declared, available } => {
                write!(f, "SF 声明 {declared} 字节，但本帧只有 {available} 字节可用")
            }
            IsoTpError::FirstFrameLengthZero => write!(f, "FF 声明长度为 0"),
            IsoTpError::UnexpectedConsecutiveFrame => write!(f, "未收到 FF 就收到 CF"),
            IsoTpError::UnexpectedFirstFrame => write!(f, "接收过程中又收到 FF"),
            IsoTpError::SequenceMismatch { expected, got } => {
                write!(f, "CF 序号错误：期望 {expected}，实际 {got}")
            }
            IsoTpError::Overflow { limit } => write!(f, "报文长度超过上限 {limit}"),
            IsoTpError::InvalidTxDl(dl) => {
                write!(f, "非法 TX_DL={dl}（FD 只允许 8/12/16/20/24/32/48/64）")
            }
            IsoTpError::PayloadTooLong(n) => write!(f, "载荷过长：{n} 字节"),
            IsoTpError::FlowControlTruncated => write!(f, "FC 帧长度不足 3 字节"),
            IsoTpError::NotReady => write!(f, "当前状态不接受该帧"),
        }
    }
}

/// 判定一帧的 PCI 类型
pub fn frame_type(data: &[u8]) -> Result<FrameType, IsoTpError> {
    let b = *data.first().ok_or(IsoTpError::Empty)?;
    match b >> 4 {
        0x0 => Ok(FrameType::Single),
        0x1 => Ok(FrameType::First),
        0x2 => Ok(FrameType::Consecutive),
        0x3 => Ok(FrameType::FlowControl),
        _ => Err(IsoTpError::UnknownFrameType(b)),
    }
}

/// 是否是合法 TX_DL（经典 8 或 FD 集合）
pub fn is_valid_tx_dl(tx_dl: usize) -> bool {
    FD_TX_DLS.contains(&tx_dl)
}

/// 组包（发送侧）：把 payload 切成 ISO-TP 帧的数据段
///
/// * tx_dl：每帧数据段长度（经典 8；FD 8/12/16/20/24/32/48/64）
/// * pad：填充字节（None 表示不填充，长度按实际内容）
///
/// 返回 Vec 中每项是一帧的数据段（不含 CAN ID）。
pub fn segment(payload: &[u8], tx_dl: usize, pad: Option<u8>) -> Result<Vec<Vec<u8>>, IsoTpError> {
    if !is_valid_tx_dl(tx_dl) {
        return Err(IsoTpError::InvalidTxDl(tx_dl));
    }
    if payload.is_empty() {
        return Err(IsoTpError::Empty);
    }
    if payload.len() > u32::MAX as usize {
        return Err(IsoTpError::PayloadTooLong(payload.len()));
    }

    let max_sf = tx_dl - 1;
    // ---- 单帧 ----
    if payload.len() <= max_sf {
        let mut f = Vec::with_capacity(tx_dl);
        if payload.len() <= 7 {
            f.push(payload.len() as u8); // SF 短格式：PCI 低半字节即长度
            f.extend_from_slice(payload);
        } else {
            // FD 长 SF 转义格式：PCI=0x00，第二字节为长度（>7）
            f.push(0x00);
            f.push(payload.len() as u8);
            f.extend_from_slice(payload);
        }
        if let Some(p) = pad {
            f.resize(tx_dl, p);
        }
        return Ok(vec![f]);
    }

    // ---- 首帧 ----
    let mut frames: Vec<Vec<u8>> = Vec::new();
    let mut ff = Vec::with_capacity(tx_dl);
    let offset;
    if payload.len() <= 0xFFF {
        ff.push(0x10 | ((payload.len() >> 8) as u8 & 0x0F));
        ff.push((payload.len() & 0xFF) as u8);
        offset = 2;
    } else {
        // 32 位转义长度：PCI=0x10，长度低 4 位为 0，随后 4 字节大端
        ff.push(0x10);
        ff.push(0x00);
        ff.extend_from_slice(&(payload.len() as u32).to_be_bytes());
        offset = 6;
    }
    let take = tx_dl - offset;
    ff.extend_from_slice(&payload[..take]);
    if let Some(p) = pad {
        ff.resize(tx_dl, p);
    }
    frames.push(ff);
    let mut pos = take;

    // ---- 连续帧 ----
    let mut sn: u8 = 1;
    while pos < payload.len() {
        let mut cf = Vec::with_capacity(tx_dl);
        cf.push(0x20 | (sn & 0x0F));
        let n = (tx_dl - 1).min(payload.len() - pos);
        cf.extend_from_slice(&payload[pos..pos + n]);
        if let Some(p) = pad {
            cf.resize(tx_dl, p);
        }
        frames.push(cf);
        pos += n;
        sn = (sn + 1) & 0x0F;
    }
    Ok(frames)
}

/// 流控帧（接收侧）：FS=0(CTS)，BS=块大小，STmin=最小间隔
pub fn flow_control(bs: u8, st_min: StMin, tx_dl: usize, pad: Option<u8>) -> Result<Vec<u8>, IsoTpError> {
    if !is_valid_tx_dl(tx_dl) || tx_dl < 3 {
        return Err(IsoTpError::InvalidTxDl(tx_dl));
    }
    let mut f = vec![0x30, bs, st_min.0];
    if let Some(p) = pad {
        f.resize(tx_dl, p);
    }
    Ok(f)
}

/// 解析 FC 帧：(状态, BS, STmin)
pub fn parse_flow_control(data: &[u8]) -> Result<(FlowStatus, u8, StMin), IsoTpError> {
    if data.len() < 3 {
        return Err(IsoTpError::FlowControlTruncated);
    }
    if frame_type(data)? != FrameType::FlowControl {
        return Err(IsoTpError::UnknownFrameType(data[0]));
    }
    let fs = FlowStatus::from_nibble(data[0] & 0x0F)
        .ok_or(IsoTpError::UnknownFrameType(data[0]))?;
    Ok((fs, data[1], StMin(data[2])))
}

/// 解析 FF 帧头
pub fn parse_first_frame(data: &[u8]) -> Result<FirstFrameInfo, IsoTpError> {
    if frame_type(data)? != FrameType::First {
        return Err(IsoTpError::UnknownFrameType(data[0]));
    }
    if data.len() < 2 {
        return Err(IsoTpError::TooShort);
    }
    let len12 = (usize::from(data[0] & 0x0F) << 8) | usize::from(data[1]);
    if len12 == 0 {
        if data.len() < 6 {
            return Err(IsoTpError::TooShort);
        }
        let total = u32::from_be_bytes([data[2], data[3], data[4], data[5]]) as usize;
        if total == 0 {
            return Err(IsoTpError::FirstFrameLengthZero);
        }
        Ok(FirstFrameInfo { total_len: total, data_offset: 6 })
    } else {
        Ok(FirstFrameInfo { total_len: len12, data_offset: 2 })
    }
}

/// 接收侧重组状态机
#[derive(Debug)]
pub struct Reassembler {
    /// 单条报文长度上限（防止异常长度导致内存暴涨）
    pub max_len: usize,
    state: State,
}

#[derive(Debug)]
enum State {
    Idle,
    Receiving { total: usize, next_sn: u8, buf: Vec<u8> },
}

impl Default for Reassembler {
    fn default() -> Self {
        Self::new(1 << 20)
    }
}

impl Reassembler {
    pub fn new(max_len: usize) -> Self {
        Reassembler { max_len, state: State::Idle }
    }

    pub fn is_receiving(&self) -> bool {
        matches!(self.state, State::Receiving { .. })
    }

    pub fn reset(&mut self) {
        self.state = State::Idle;
    }

    /// 送入一帧数据段；收满一条报文时返回 Some(payload)
    ///
    /// 注意：FF 到达时若正在接收，按 ISO 语义丢弃旧报文并重新开始。
    pub fn push(&mut self, data: &[u8]) -> Result<Option<Vec<u8>>, IsoTpError> {
        if data.is_empty() {
            return Err(IsoTpError::Empty);
        }
        match frame_type(data)? {
            FrameType::Single => {
                let (len, off) = single_frame_len(data)?;
                let available = data.len().saturating_sub(off);
                if len > available {
                    return Err(IsoTpError::SingleFrameOverflow { declared: len, available });
                }
                if len > self.max_len {
                    return Err(IsoTpError::Overflow { limit: self.max_len });
                }
                self.state = State::Idle;
                Ok(Some(data[off..off + len].to_vec()))
            }
            FrameType::First => {
                let info = parse_first_frame(data)?;
                if info.total_len > self.max_len {
                    return Err(IsoTpError::Overflow { limit: self.max_len });
                }
                let avail = data.len().saturating_sub(info.data_offset);
                let mut buf = Vec::with_capacity(info.total_len);
                let n = info.total_len.min(avail);
                buf.extend_from_slice(&data[info.data_offset..info.data_offset + n]);
                if buf.len() >= info.total_len {
                    self.state = State::Idle;
                    return Ok(Some(buf));
                }
                self.state = State::Receiving { total: info.total_len, next_sn: 1, buf };
                Ok(None)
            }
            FrameType::Consecutive => {
                let (total, next_sn, mut buf) = match std::mem::replace(&mut self.state, State::Idle) {
                    State::Receiving { total, next_sn, buf } => (total, next_sn, buf),
                    State::Idle => return Err(IsoTpError::UnexpectedConsecutiveFrame),
                };
                let sn = data[0] & 0x0F;
                if sn != next_sn {
                    return Err(IsoTpError::SequenceMismatch { expected: next_sn, got: sn });
                }
                let need = total - buf.len();
                let avail = data.len() - 1;
                let n = need.min(avail);
                buf.extend_from_slice(&data[1..1 + n]);
                if buf.len() >= total {
                    Ok(Some(buf))
                } else {
                    self.state = State::Receiving { total, next_sn: (sn + 1) & 0x0F, buf };
                    Ok(None)
                }
            }
            FrameType::FlowControl => Err(IsoTpError::NotReady),
        }
    }
}

/// SF 的长度与数据偏移：短格式（<=7）偏移 1；转义格式（PCI=0x00）偏移 2
fn single_frame_len(data: &[u8]) -> Result<(usize, usize), IsoTpError> {
    let nib = data[0] & 0x0F;
    if nib != 0 {
        Ok((usize::from(nib), 1))
    } else {
        if data.len() < 2 {
            return Err(IsoTpError::TooShort);
        }
        Ok((usize::from(data[1]), 2))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ---------------- 组包 ----------------

    #[test]
    fn segment_single_short() {
        let f = segment(&[0x10, 0x03], 8, None).unwrap();
        assert_eq!(f.len(), 1);
        assert_eq!(f[0][0], 0x02);
        assert_eq!(&f[0][1..3], &[0x10, 0x03]);
    }

    #[test]
    fn segment_single_with_padding() {
        let f = segment(&[0x22, 0xF1, 0x90], 8, Some(0xCC)).unwrap();
        assert_eq!(f[0].len(), 8);
        assert_eq!(&f[0][..4], &[0x03, 0x22, 0xF1, 0x90]);
        assert_eq!(&f[0][4..], &[0xCC, 0xCC, 0xCC, 0xCC]);
    }

    #[test]
    fn segment_multi_classic() {
        let payload: Vec<u8> = (0..20).collect();
        let f = segment(&payload, 8, Some(0xAA)).unwrap();
        assert_eq!(f.len(), 3);
        assert_eq!(f[0][0], 0x10);
        assert_eq!(((usize::from(f[0][0] & 0x0F)) << 8) | usize::from(f[0][1]), 20);
        assert_eq!(f[1][0], 0x21);
        assert_eq!(f[2][0], 0x22);
        for fr in &f {
            assert_eq!(fr.len(), 8);
        }
    }

    #[test]
    fn segment_sn_wraps_after_fifteen() {
        let payload = vec![0x5A; 200];
        let f = segment(&payload, 8, None).unwrap();
        assert!(f.len() > 16);
        assert_eq!(f[1][0] & 0x0F, 1);
        assert_eq!(f[15][0] & 0x0F, 15);
        assert_eq!(f[16][0] & 0x0F, 0);
        assert_eq!(f[17][0] & 0x0F, 1);
    }

    #[test]
    fn segment_ff_32bit_escape() {
        let payload = vec![0x11; 5000];
        let f = segment(&payload, 64, None).unwrap();
        assert_eq!(f[0][0], 0x10);
        assert_eq!(f[0][1], 0x00);
        assert_eq!(u32::from_be_bytes([f[0][2], f[0][3], f[0][4], f[0][5]]), 5000);
        assert_eq!(f[0].len(), 64);
    }

    #[test]
    fn segment_fd_long_single_frame_escape() {
        let payload: Vec<u8> = (0..30).collect();
        let f = segment(&payload, 64, Some(0xCC)).unwrap();
        assert_eq!(f.len(), 1);
        assert_eq!(f[0][0], 0x00);
        assert_eq!(f[0][1], 30);
        assert_eq!(&f[0][2..32], &payload[..]);
        assert_eq!(f[0].len(), 64);
    }

    #[test]
    fn segment_fd_tx_dl_12() {
        let payload: Vec<u8> = (0..40).collect();
        let f = segment(&payload, 12, None).unwrap();
        assert_eq!(f[0].len(), 12);
        assert_eq!(f[0][0], 0x10);
        assert_eq!(f[1][0] & 0xF0, 0x20);
    }

    #[test]
    fn segment_rejects_invalid_tx_dl() {
        assert_eq!(segment(&[1, 2, 3], 9, None), Err(IsoTpError::InvalidTxDl(9)));
        assert_eq!(segment(&[1, 2, 3], 7, None), Err(IsoTpError::InvalidTxDl(7)));
    }

    #[test]
    fn segment_rejects_empty() {
        assert_eq!(segment(&[], 8, None), Err(IsoTpError::Empty));
    }

    // ---------------- 流控 ----------------

    #[test]
    fn flow_control_build_and_parse() {
        let fc = flow_control(8, StMin(0x32), 8, Some(0xCC)).unwrap();
        assert_eq!(&fc[..3], &[0x30, 8, 0x32]);
        assert_eq!(fc.len(), 8);
        let (st, bs, sm) = parse_flow_control(&fc).unwrap();
        assert_eq!(st, FlowStatus::Continue);
        assert_eq!(bs, 8);
        assert_eq!(sm.to_micros(), 50_000);
    }

    #[test]
    fn flow_control_overflow_status() {
        let ov = vec![0x32, 0, 0, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC];
        assert_eq!(parse_flow_control(&ov).unwrap().0, FlowStatus::Overflow);
        let w = vec![0x31, 0, 0x00];
        assert_eq!(parse_flow_control(&w).unwrap().0, FlowStatus::Wait);
    }

    #[test]
    fn stmin_encoding() {
        assert_eq!(StMin(0x00).to_micros(), 0);
        assert_eq!(StMin(0x01).to_micros(), 1_000);
        assert_eq!(StMin(0x7F).to_micros(), 127_000);
        assert_eq!(StMin(0xF1).to_micros(), 100);
        assert_eq!(StMin(0xF9).to_micros(), 900);
        assert_eq!(StMin(0x80).to_micros(), 127_000);
        assert_eq!(StMin(0x32).describe(), "50 ms (raw=0x32)");
        assert_eq!(StMin(0xF3).describe(), "300 us (raw=0xF3)");
    }

    // ---------------- 解包 ----------------

    #[test]
    fn reassemble_single_frame() {
        let mut r = Reassembler::default();
        let got = r.push(&[0x03, 0x22, 0xF1, 0x90, 0xCC, 0xCC, 0xCC, 0xCC]).unwrap();
        assert_eq!(got, Some(vec![0x22, 0xF1, 0x90]));
        assert!(!r.is_receiving());
    }

    #[test]
    fn reassemble_multi_frame() {
        let payload: Vec<u8> = (0..20).collect();
        let frames = segment(&payload, 8, Some(0xAA)).unwrap();
        let mut r = Reassembler::default();
        let mut out = None;
        for f in &frames {
            out = r.push(f).unwrap();
        }
        assert_eq!(out, Some(payload));
    }

    #[test]
    fn reassemble_fd_60_bytes() {
        let payload: Vec<u8> = (0..60).collect();
        let frames = segment(&payload, 64, Some(0xCC)).unwrap();
        let mut r = Reassembler::default();
        let mut out = None;
        for f in &frames {
            out = r.push(f).unwrap();
        }
        assert_eq!(out, Some(payload));
    }

    #[test]
    fn reassemble_5000_bytes_32bit_len() {
        let payload: Vec<u8> = (0..5000).map(|i| (i % 251) as u8).collect();
        let frames = segment(&payload, 64, None).unwrap();
        let mut r = Reassembler::new(64 * 1024);
        let mut out = None;
        for f in &frames {
            out = r.push(f).unwrap();
        }
        assert_eq!(out.as_ref().map(|v| v.len()), Some(5000));
        assert_eq!(out.unwrap(), payload);
    }

    #[test]
    fn reassemble_detects_sequence_error() {
        let payload: Vec<u8> = (0..20).collect();
        let mut frames = segment(&payload, 8, None).unwrap();
        frames[2][0] = 0x25;
        let mut r = Reassembler::default();
        let mut err = None;
        for f in &frames {
            match r.push(f) {
                Ok(_) => {}
                Err(e) => {
                    err = Some(e);
                    break;
                }
            }
        }
        assert_eq!(err, Some(IsoTpError::SequenceMismatch { expected: 2, got: 5 }));
    }

    #[test]
    fn reassemble_rejects_cf_without_ff() {
        let mut r = Reassembler::default();
        assert_eq!(r.push(&[0x21, 1, 2, 3]), Err(IsoTpError::UnexpectedConsecutiveFrame));
    }

    #[test]
    fn reassemble_overflow_limit() {
        let mut r = Reassembler::new(16);
        let mut ff = vec![0x10, 100];
        ff.extend_from_slice(&[0u8; 6]);
        assert_eq!(r.push(&ff), Err(IsoTpError::Overflow { limit: 16 }));
    }

    #[test]
    fn reassemble_single_frame_declared_too_long() {
        let mut r = Reassembler::default();
        let e = r.push(&[0x07, 0x01, 0x02]).unwrap_err();
        assert_eq!(e, IsoTpError::SingleFrameOverflow { declared: 7, available: 2 });
    }

    #[test]
    fn reassemble_ff_restarts_on_new_ff() {
        let mut r = Reassembler::default();
        assert_eq!(r.push(&[0x10, 20, 0, 1, 2, 3, 4, 5]).unwrap(), None);
        let payload: Vec<u8> = (0..20).collect();
        let frames = segment(&payload, 8, None).unwrap();
        let mut out = None;
        for f in &frames {
            out = r.push(f).unwrap();
        }
        assert_eq!(out, Some(payload));
    }

    // ---------------- 端到端 ----------------

    #[test]
    fn roundtrip_many_sizes_classic() {
        for n in 1..=40usize {
            let payload: Vec<u8> = (0..n).map(|i| (i * 7 % 256) as u8).collect();
            let frames = segment(&payload, 8, Some(0xCC)).unwrap();
            let mut r = Reassembler::default();
            let mut out = None;
            for f in &frames {
                out = r.push(f).unwrap();
            }
            assert_eq!(out, Some(payload), "n={n}");
        }
    }

    #[test]
    fn roundtrip_many_sizes_fd64() {
        for n in [1usize, 7, 8, 30, 62, 63, 64, 65, 200, 1000] {
            let payload: Vec<u8> = (0..n).map(|i| (i % 253) as u8).collect();
            let frames = segment(&payload, 64, None).unwrap();
            let mut r = Reassembler::new(4096);
            let mut out = None;
            for f in &frames {
                out = r.push(f).unwrap();
            }
            assert_eq!(out, Some(payload), "n={n}");
        }
    }
}
