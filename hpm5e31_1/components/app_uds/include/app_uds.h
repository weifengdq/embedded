/*
 * app_uds.h - UDS (ISO 14229-1:2013) 诊断服务端，与 ISO-TP / 具体 MCU 解耦
 *
 * 已实现服务（第 31 章表 31-2 有完整矩阵）：
 *   0x10 DiagnosticSessionControl (01/02/03)
 *   0x11 ECUReset                (01/02/03)
 *   0x14 ClearDiagnosticInformation (groupOfDTC=0xFFFFFF)
 *   0x19 ReadDTCInformation      (01/02/04/06)
 *   0x22 ReadDataByIdentifier
 *   0x27 SecurityAccess          (01/02 一级，03/04 二级；种子-密钥，3 次错误锁 10 s)
 *   0x28 CommunicationControl    (00/01/02/03)
 *   0x2E WriteDataByIdentifier
 *   0x2F InputOutputControlByIdentifier (00/01/02/03)
 *   0x31 RoutineControl          (01/02/03，RID 由调用者注册)
 *   0x34/0x36/0x37 RequestDownload/TransferData/RequestTransferExit
 *   0x3E TesterPresent           (00/80 抑制正响应)
 *   0x85 ControlDTCSetting       (01/02)
 *
 * 时间参数：P2=50 ms、P2*=5000 ms、S3=5000 ms（可在 cfg 覆盖）；
 * 长操作前由服务自己发 NRC 0x78 requestCorrectlyReceived-ResponsePending。
 */
#ifndef APP_UDS_H
#define APP_UDS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_UDS_P2_MS         50u
#define APP_UDS_P2STAR_MS     5000u
#define APP_UDS_S3_MS         5000u

#define APP_UDS_MAX_DID_DATA  32u
#define APP_UDS_MAX_RESP      1024u
#define APP_UDS_MAX_REQ       512u
#define APP_UDS_SEC_LOCKOUT_MS 10000u

/* 会话掩码位 */
#define APP_UDS_SESS_DEFAULT  0x01u
#define APP_UDS_SESS_PROG     0x02u
#define APP_UDS_SESS_EXT      0x04u
#define APP_UDS_SESS_ALL      0x07u

/* DTC 状态位（ISO 14229-1 statusOfDTC） */
#define APP_UDS_DTC_TEST_FAILED          0x01u
#define APP_UDS_DTC_TEST_FAILED_THIS_CYC 0x02u
#define APP_UDS_DTC_PENDING              0x04u
#define APP_UDS_DTC_CONFIRMED            0x08u
#define APP_UDS_DTC_NOT_COMPLETE_SINCE_CLR 0x10u
#define APP_UDS_DTC_FAILED_SINCE_CLR     0x20u
#define APP_UDS_DTC_NOT_COMPLETE_THIS_CYC 0x40u
#define APP_UDS_DTC_WARNING_IND          0x80u

typedef enum {
    APP_UDS_SESSION_DEFAULT  = 0x01,
    APP_UDS_SESSION_PROG     = 0x02,
    APP_UDS_SESSION_EXTENDED = 0x03
} app_uds_session_t;

typedef enum {
    APP_UDS_SEC_LOCKED = 0,
    APP_UDS_SEC_LEVEL1 = 1,
    APP_UDS_SEC_LEVEL2 = 2
} app_uds_sec_t;

/* NRC（负响应码） */
typedef enum {
    APP_UDS_NRC_GR       = 0x10,  /* generalReject */
    APP_UDS_NRC_SNS      = 0x11,  /* serviceNotSupported */
    APP_UDS_NRC_SFNS     = 0x12,  /* subFunctionNotSupported */
    APP_UDS_NRC_IMLOIF   = 0x13,  /* incorrectMessageLengthOrInvalidFormat */
    APP_UDS_NRC_CNC      = 0x22,  /* conditionsNotCorrect */
    APP_UDS_NRC_RSE      = 0x24,  /* requestSequenceError */
    APP_UDS_NRC_ROOR     = 0x31,  /* requestOutOfRange */
    APP_UDS_NRC_SAD      = 0x33,  /* securityAccessDenied */
    APP_UDS_NRC_IK       = 0x35,  /* invalidKey */
    APP_UDS_NRC_ENOA     = 0x36,  /* exceedNumberOfAttempts */
    APP_UDS_NRC_RTDNE    = 0x37,  /* requiredTimeDelayNotExpired */
    APP_UDS_NRC_GPF      = 0x72,  /* generalProgrammingFailure */
    APP_UDS_NRC_WBSC     = 0x73,  /* wrongBlockSequenceCounter */
    APP_UDS_NRC_RCRRP    = 0x78,  /* requestCorrectlyReceived-ResponsePending */
    APP_UDS_NRC_SFNSIAS  = 0x7E,  /* subFunctionNotSupportedInActiveSession */
    APP_UDS_NRC_SNSIAS   = 0x7F   /* serviceNotSupportedInActiveSession */
} app_uds_nrc_t;

/* DID 描述：数据在表里就地存放，0x2E 直接改这张表 */
typedef struct {
    uint16_t did;
    uint8_t  len;
    uint8_t  data[APP_UDS_MAX_DID_DATA];
    uint8_t  read_mask;      /* 允许读的会话掩码 */
    uint8_t  write_mask;     /* 允许写的会话掩码，0 = 只读 */
    uint8_t  write_sec;      /* 写所需安全等级 */
} app_uds_did_t;

/* 例程描述（RID + 三个动作）；state 由例程自己维护 */
typedef struct {
    uint16_t rid;
    uint8_t  session_mask;
    uint8_t  sec_level;
    uint8_t  state;          /* 0=未启动 1=运行中 2=已结束 */
    const char *name;
    int (*start)(void *ctx, const uint8_t *opt, uint32_t opt_len, uint8_t *out, uint32_t *out_len);
    int (*stop)(void *ctx, const uint8_t *opt, uint32_t opt_len, uint8_t *out, uint32_t *out_len);
    int (*results)(void *ctx, uint8_t *out, uint32_t *out_len);
    void *ctx;
} app_uds_routine_t;

/* DTC 内存（RAM 表，见 app_uds_dtc.h 的说明） */
#include "app_uds_dtc.h"
/* 刷写驱动（见 app_uds_flash.h） */
#include "app_uds_flash.h"

typedef int  (*app_uds_tx_fn)(void *ctx, const uint8_t *pdu, uint32_t len);
typedef void (*app_uds_reset_fn)(void *ctx);
typedef int  (*app_uds_obd_fn)(void *ctx, const uint8_t *req, uint32_t req_len,
                               uint8_t *resp, uint32_t *resp_len);

typedef struct {
    app_uds_tx_fn     tx;              /* 发送完整响应 PDU（通常是 ISO-TP send） */
    void             *tx_ctx;
    app_uds_reset_fn  reset;           /* 0x11 的执行体，NULL = 只打印 */
    void             *reset_ctx;
    app_uds_obd_fn    obd;             /* 可选：把 0x01/0x02/0x03/0x04/0x09 转发给 OBD 模块 */
    void             *obd_ctx;
    uint64_t       (*now_us)(void *ctx);   /* 单调微秒时基（与 ISO-TP 共用），NULL 用 poll 缓存值 */
    void             *time_ctx;
    uint32_t          p2_ms;
    uint32_t          p2star_ms;
    uint32_t          s3_ms;
    app_uds_did_t    *dids;            /* DID 表（调用者提供，可运行时修改） */
    uint32_t          did_count;
    app_uds_routine_t *routines;
    uint32_t          routine_count;
    app_uds_dtc_mem_t *dtc;
    const app_uds_flash_driver_t *flash;   /* NULL = 不支持 0x34/0x36/0x37 */
} app_uds_cfg_t;

typedef struct app_uds {
    app_uds_cfg_t  cfg;
    app_uds_session_t session;
    app_uds_sec_t  security;
    uint8_t        seed_pending;      /* 已发出种子等待 key 的等级 */
    uint32_t       seed;
    uint8_t        sec_attempts;
    uint64_t       sec_lock_until_us;
    uint64_t       s3_deadline_us;
    uint64_t       reset_at_us;
    bool           reset_pending;
    bool           dtc_setting_on;
    uint8_t        comm_mode;         /* 0=正常；见 0x28 子功能 */
    /* 0x34/0x36/0x37 状态 */
    bool           dl_active;
    uint32_t       dl_addr;
    uint32_t       dl_size;
    uint32_t       dl_got;
    uint8_t        dl_seq_next;
    uint32_t       dl_block_max;
    /* 统计 */
    uint32_t       stat_req;
    uint32_t       stat_resp_pos;
    uint32_t       stat_resp_nrc;
    uint32_t       stat_pending;
    uint32_t       stat_s3_timeout;
    uint32_t       last_rtt_us;
    uint64_t       last_req_us;
    uint64_t       now_cache_us;
    bool           suppress_pos;      /* 本次请求的 suppressPosRspMsgIndicationBit */
} app_uds_t;

int  app_uds_init(app_uds_t *u, const app_uds_cfg_t *cfg);
/* 处理一条完整请求 PDU；响应经 cfg.tx 发出。返回 0 */
int  app_uds_handle(app_uds_t *u, const uint8_t *req, uint32_t len);
/* 主循环周期调用：S3 超时回默认会话、延迟复位 */
void app_uds_poll(app_uds_t *u, uint64_t now_us);
/* 主动发 NRC 0x78（长操作前调用，主机侧会重置 P2* 计时） */
int  app_uds_send_pending(app_uds_t *u, uint8_t sid);

/* 安全访问算法（纯函数，PC 端 host/uds/uds_sec.py 与 Rust 工具用同一份） */
uint32_t app_uds_calc_key(uint32_t seed, uint8_t level);
/* 生成下一个种子（内部 LFSR + 计数，保证非 0 且不重复） */
uint32_t app_uds_next_seed(app_uds_t *u);

/* 默认 DID 表（F190 VIN / F195 软件版本 / F187 零件号 / F18C 序列号 / F191 硬件版本 / F010 输出控制） */
int  app_uds_default_dids(app_uds_did_t *tbl, uint32_t *count);

static inline const char *app_uds_session_name(app_uds_session_t s)
{
    switch (s) {
    case APP_UDS_SESSION_DEFAULT:  return "default(01)";
    case APP_UDS_SESSION_PROG:     return "programming(02)";
    case APP_UDS_SESSION_EXTENDED: return "extended(03)";
    default:                       return "?";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* APP_UDS_H */
