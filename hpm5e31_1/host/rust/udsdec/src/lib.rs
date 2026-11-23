//! UDS（ISO 14229）报文名称解析（纯 std）
//!
//! 用途：把 0x22/0x2E/0x27/0x19… 的服务与 NRC 解析成人能读的名字，便于日志分析。
//! 不是诊断栈，只做“解码”。

/// 服务名（SID -> 名称）
pub fn service_name(sid: u8) -> &'static str {
    match sid {
        0x10 => "DiagnosticSessionControl",
        0x11 => "ECUReset",
        0x14 => "ClearDiagnosticInformation",
        0x19 => "ReadDTCInformation",
        0x22 => "ReadDataByIdentifier",
        0x23 => "ReadMemoryByAddress",
        0x24 => "ReadScalingDataByIdentifier",
        0x27 => "SecurityAccess",
        0x28 => "CommunicationControl",
        0x29 => "Authentication",
        0x2A => "ReadDataByPeriodicIdentifier",
        0x2C => "DynamicallyDefineDataIdentifier",
        0x2E => "WriteDataByIdentifier",
        0x2F => "InputOutputControlByIdentifier",
        0x31 => "RoutineControl",
        0x34 => "RequestDownload",
        0x35 => "RequestUpload",
        0x36 => "TransferData",
        0x37 => "RequestTransferExit",
        0x38 => "RequestFileTransfer",
        0x3D => "WriteMemoryByAddress",
        0x3E => "TesterPresent",
        0x83 => "AccessTimingParameter",
        0x84 => "SecuredDataTransmission",
        0x85 => "ControlDTCSetting",
        0x86 => "ResponseOnEvent",
        0x87 => "LinkControl",
        _ => "Unknown/Reserved",
    }
}

/// NRC（否定响应码）名称
pub fn nrc_name(nrc: u8) -> &'static str {
    match nrc {
        0x10 => "generalReject",
        0x11 => "serviceNotSupported",
        0x12 => "subFunctionNotSupported",
        0x13 => "incorrectMessageLengthOrInvalidFormat",
        0x14 => "responseTooLong",
        0x21 => "busyRepeatRequest",
        0x22 => "conditionsNotCorrect",
        0x24 => "requestSequenceError",
        0x25 => "noResponseFromSubnetComponent",
        0x26 => "failurePreventsExecutionOfRequestedAction",
        0x31 => "requestOutOfRange",
        0x33 => "securityAccessDenied",
        0x34 => "authenticationRequired",
        0x35 => "invalidKey",
        0x36 => "exceedNumberOfAttempts",
        0x37 => "requiredTimeDelayNotExpired",
        0x70 => "uploadDownloadNotAccepted",
        0x71 => "transferDataSuspended",
        0x72 => "generalProgrammingFailure",
        0x73 => "wrongBlockSequenceCounter",
        0x78 => "requestCorrectlyReceived-ResponsePending",
        0x7E => "subFunctionNotSupportedInActiveSession",
        0x7F => "serviceNotSupportedInActiveSession",
        _ => "unknown NRC",
    }
}

/// DiagnosticSessionControl 的子功能名
pub fn session_name(sub: u8) -> &'static str {
    match sub & 0x7F {
        0x01 => "defaultSession",
        0x02 => "programmingSession",
        0x03 => "extendedDiagnosticSession",
        0x04 => "safetySystemDiagnosticSession",
        _ => "unknown session",
    }
}

/// ECUReset 的子功能名
pub fn reset_name(sub: u8) -> &'static str {
    match sub & 0x7F {
        0x01 => "hardReset",
        0x02 => "keyOffOnReset",
        0x03 => "softReset",
        0x04 => "enableRapidPowerShutDown",
        0x05 => "disableRapidPowerShutDown",
        _ => "unknown resetType",
    }
}

/// 顶层解码结果
#[derive(Debug, Clone, PartialEq)]
pub enum Decoded {
    /// 正响应：SID 已 +0x40
    Positive { sid: u8, service: &'static str, detail: String },
    /// 否定响应
    Negative { sid: u8, service: &'static str, nrc: u8, nrc_name: &'static str },
    /// 请求
    Request { sid: u8, service: &'static str, detail: String },
    /// 无法识别
    Unknown { raw: Vec<u8> },
}

impl std::fmt::Display for Decoded {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Decoded::Positive { sid, service, detail } => {
                write!(f, "正响应 SID=0x{sid:02X} {service}{detail}")
            }
            Decoded::Negative { sid, service, nrc, nrc_name } => write!(
                f,
                "否定响应 针对 SID=0x{sid:02X} {service}：NRC=0x{nrc:02X} {nrc_name}"
            ),
            Decoded::Request { sid, service, detail } => {
                write!(f, "请求 SID=0x{sid:02X} {service}{detail}")
            }
            Decoded::Unknown { raw } => write!(f, "无法识别: {raw:02X?}"),
        }
    }
}

fn detail_for(sid: u8, data: &[u8]) -> String {
    match sid {
        0x10 if data.len() >= 2 => format!("（session={}）", session_name(data[1])),
        0x11 if data.len() >= 2 => format!("（resetType={}）", reset_name(data[1])),
        0x22 if data.len() >= 3 => format!("（DID=0x{:02X}{:02X}）", data[1], data[2]),
        0x2E if data.len() >= 3 => format!("（DID=0x{:02X}{:02X}，{} 字节数据）", data[1], data[2], data.len() - 3),
        0x27 if data.len() >= 2 => format!("（securityAccessType=0x{:02X}）", data[1]),
        0x31 if data.len() >= 4 => format!(
            "（routineControlType=0x{:02X}，RID=0x{:02X}{:02X}）",
            data[1], data[2], data[3]
        ),
        0x3E if data.len() >= 2 => format!("（suppressPosRsp={}）", data[1] & 0x80 != 0),
        0x85 if data.len() >= 2 => format!("（DTCSettingType=0x{:02X}）", data[1]),
        0x19 if data.len() >= 2 => format!("（reportType=0x{:02X}）", data[1]),
        0x14 if data.len() >= 4 => format!("（groupOfDTC=0x{:02X}{:02X}{:02X}）", data[1], data[2], data[3]),
        _ => String::new(),
    }
}

/// 解码一段 UDS 报文（不含 ISO-TP 头，即已经是完整应用层载荷）
pub fn decode(payload: &[u8]) -> Decoded {
    if payload.is_empty() {
        return Decoded::Unknown { raw: vec![] };
    }
    let sid = payload[0];
    if sid == 0x7F {
        if payload.len() < 3 {
            return Decoded::Unknown { raw: payload.to_vec() };
        }
        let req_sid = payload[1];
        let nrc = payload[2];
        return Decoded::Negative {
            sid: req_sid,
            service: service_name(req_sid),
            nrc,
            nrc_name: nrc_name(nrc),
        };
    }
    // 正响应：请求 SID + 0x40（0x40 以下为请求）
    if sid >= 0x40 && sid < 0x80 {
        let req = sid - 0x40;
        let service = service_name(req);
        if service == "Unknown/Reserved" {
            return Decoded::Unknown { raw: payload.to_vec() };
        }
        return Decoded::Positive { sid, service, detail: detail_for(req, payload) };
    }
    let service = service_name(sid);
    if service == "Unknown/Reserved" {
        return Decoded::Unknown { raw: payload.to_vec() };
    }
    Decoded::Request { sid, service, detail: detail_for(sid, payload) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decode_request_read_did() {
        let d = decode(&[0x22, 0xF1, 0x90]);
        match d {
            Decoded::Request { sid, service, detail } => {
                assert_eq!(sid, 0x22);
                assert_eq!(service, "ReadDataByIdentifier");
                assert!(detail.contains("DID=0xF190"), "{detail}");
            }
            other => panic!("{other:?}"),
        }
    }

    #[test]
    fn decode_positive_response_with_did() {
        let d = decode(&[0x62, 0xF1, 0x90, b'H', b'P', b'M']);
        match d {
            Decoded::Positive { sid, service, detail } => {
                assert_eq!(sid, 0x62);
                assert_eq!(service, "ReadDataByIdentifier");
                assert!(detail.contains("DID=0xF190"));
            }
            other => panic!("{other:?}"),
        }
    }

    #[test]
    fn decode_negative_nrc_31() {
        let d = decode(&[0x7F, 0x22, 0x31]);
        match d {
            Decoded::Negative { sid, nrc, nrc_name, .. } => {
                assert_eq!(sid, 0x22);
                assert_eq!(nrc, 0x31);
                assert_eq!(nrc_name, "requestOutOfRange");
            }
            other => panic!("{other:?}"),
        }
    }

    #[test]
    fn decode_negative_response_pending() {
        let d = decode(&[0x7F, 0x31, 0x78]);
        assert!(format!("{d}").contains("requestCorrectlyReceived-ResponsePending"));
    }

    #[test]
    fn decode_session_control() {
        let req = decode(&[0x10, 0x03]);
        assert!(format!("{req}").contains("extendedDiagnosticSession"));
        let resp = decode(&[0x50, 0x03]);
        assert!(format!("{resp}").contains("正响应"));
    }

    #[test]
    fn decode_tester_present_suppress() {
        let d = decode(&[0x3E, 0x80]);
        assert!(format!("{d}").contains("suppressPosRsp=true"));
    }

    #[test]
    fn decode_security_access() {
        let d = decode(&[0x27, 0x01, 0xAA, 0xBB]);
        assert!(format!("{d}").contains("securityAccessType=0x01"));
    }

    #[test]
    fn decode_unknown_sid() {
        let d = decode(&[0x00, 0x11]);
        assert!(matches!(d, Decoded::Unknown { .. }));
    }

    #[test]
    fn decode_empty() {
        assert!(matches!(decode(&[]), Decoded::Unknown { .. }));
    }

    #[test]
    fn nrc_table_spot_checks() {
        assert_eq!(nrc_name(0x11), "serviceNotSupported");
        assert_eq!(nrc_name(0x7F), "serviceNotSupportedInActiveSession");
        assert_eq!(nrc_name(0xEE), "unknown NRC");
    }
}
