/*
 * xcp_test_master.c —— 板内最小 XCP 主机（CAN1），无上位机自证
 *
 * 只实现本样例需要的命令：CONNECT/DISCONNECT/GET_STATUS/GET_COMM_MODE_INFO/GET_ID/
 * GET_SEED/UNLOCK/SET_MTA/UPLOAD/SHORT_UPLOAD/DOWNLOAD/MODIFY_BITS/
 * GET_DAQ_PROCESSOR_INFO/GET_DAQ_RESOLUTION_INFO/FREE_DAQ/ALLOC_DAQ/ALLOC_ODT/
 * ALLOC_ODT_ENTRY/SET_DAQ_PTR/WRITE_DAQ/SET_DAQ_LIST_MODE/START_STOP_DAQ_LIST/
 * START_STOP_SYNCH/GET_DAQ_CLOCK。
 *
 * 帧格式与从站一致：CMD 0x555 / RES+DAQ 0x556，经典帧 DLC=8。
 */
#include "xcp_test_master.h"

#include "bsp.h"
#include "board.h"
#include "clock.h"
#include "ext/board_can.h"
#include "hpm_csr_drv.h"
#include "hpm_mcan_drv.h"
#include "xcp.h"

#include <string.h>

#define TM_CMD_ID       (0x555u)
#define TM_RES_ID       (0x556u)
#define TM_TIMEOUT_US   (3000u)
#define TM_PID_RES      (0xFFu)
#define TM_PID_ERR      (0xFEu)

/* 与 main.c 的 DAQ 打包计划保持一致（见 main.c 顶部注释） */
#define TM_LIST_DQ0     (0u)    /* EVT0 1 kHz，带时间戳，3 个 ODT */
#define TM_LIST_DQ1     (1u)    /* EVT1 100 Hz，无时间戳，1 个 ODT */

/* 见 xcp_port_can.c 的说明：HPM5E31 的 MCAN 消息 RAM 在 AHB SRAM，需要先注册缓冲区 */
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_m_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static MCAN_Type *s_base;
static uint32_t s_cpu_mhz = 480u;
static uint32_t s_tx_frames;
static uint32_t s_rx_frames;
static uint32_t s_timeouts;
static uint32_t s_err_resp;
static uint32_t s_fail;

/* ------------------------------------------------------------------ */
/* 底层：一帧的收发                                                    */
/* ------------------------------------------------------------------ */

static uint32_t us_since(uint64_t t0)
{
    return (uint32_t) ((hpm_csr_get_core_mcycle() - t0) / (uint64_t) s_cpu_mhz);
}

static int tm_rx(uint8_t *buf, uint8_t *len, uint32_t timeout_us)
{
    uint64_t t0 = hpm_csr_get_core_mcycle();

    do {
        mcan_rx_message_t rx;
        /* 板内自证：主站等待响应期间驱动从站协议栈（真实双机场景从站独立运行） */
        xcp_poll();
        while (mcan_read_rxfifo(s_base, 0u, &rx) == status_success) {
            s_rx_frames++;
            if ((rx.use_ext_id != 0u) || (rx.std_id != TM_RES_ID)) {
                continue;
            }
            uint8_t n = (uint8_t) rx.dlc;
            if (n > 8u) {
                n = 8u;
            }
            memcpy(buf, rx.data_8, n);
            *len = n;
            return 1;
        }
    } while (us_since(t0) < timeout_us);
    return 0;
}

/* 发一条命令并等响应。返回 0 = 肯定响应，1 = 错误响应（*err 为错误码），-1 = 超时/发送失败 */
static int tm_req(const uint8_t *cmd, uint8_t clen, uint8_t *res, uint8_t *rlen, uint8_t *err)
{
    mcan_tx_frame_t tx;

    memset(&tx, 0, sizeof(tx));
    tx.std_id = TM_CMD_ID;
    tx.dlc = 8u;
    memcpy(tx.data_8, cmd, clen);
    if (mcan_transmit_blocking(s_base, &tx) != status_success) {
        s_timeouts++;
        return -1;
    }
    s_tx_frames++;

    /* DAQ 正在跑时总线上会夹着 DAQ 帧，靠 PID 区分，最多等 8 帧 */
    for (int i = 0; i < 8; i++) {
        if (!tm_rx(res, rlen, TM_TIMEOUT_US)) {
            s_timeouts++;
            return -1;
        }
        if (res[0] == TM_PID_RES) {
            return 0;
        }
        if (res[0] == TM_PID_ERR) {
            s_err_resp++;
            if (err != NULL) {
                *err = res[1];
            }
            printf("     [ERR] cmd=0x%02X err=0x%02X\r\n", (unsigned) cmd[0], (unsigned) res[1]);
            return 1;
        }
        if (res[0] == 0xFDu) {      /* 事件帧，忽略 */
            continue;
        }
        /* 其它 PID = DAQ 帧，忽略 */
    }
    s_timeouts++;
    return -1;
}

/* 只传命令码的简单命令 */
static int tm_cmd1(uint8_t c0)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = c0;
    return tm_req(c, 1u, r, &rl, &err);
}

/* 两步式：命令 + 3 字节以内参数 */
static int tm_cmd3(uint8_t c0, uint8_t c1, uint8_t c2)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = c0;
    c[1] = c1;
    c[2] = c2;
    return tm_req(c, 3u, r, &rl, &err);
}

/* SET_MTA + SHORT_UPLOAD 读一个变量 */
static int tm_read(uint32_t addr, uint8_t n, uint8_t *out)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_SHORT_UPLOAD;
    c[1] = 0u;
    c[2] = n;
    c[3] = 0u;
    c[4] = (uint8_t) (addr & 0xFFu);
    c[5] = (uint8_t) ((addr >> 8) & 0xFFu);
    c[6] = (uint8_t) ((addr >> 16) & 0xFFu);
    c[7] = (uint8_t) ((addr >> 24) & 0xFFu);
    int rc = tm_req(c, 8u, r, &rl, &err);
    if (rc == 0 && out != NULL) {
        memcpy(out, &r[1], n);
    }
    return rc;
}

/* SET_MTA + DOWNLOAD 写一个 4 字节量 */
static int tm_write_u32(uint32_t addr, uint32_t v)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_SET_MTA;
    c[1] = 0u;
    c[2] = 0u;
    c[3] = (uint8_t) (addr & 0xFFu);
    c[4] = (uint8_t) ((addr >> 8) & 0xFFu);
    c[5] = (uint8_t) ((addr >> 16) & 0xFFu);
    c[6] = (uint8_t) ((addr >> 24) & 0xFFu);
    int rc = tm_req(c, 7u, r, &rl, &err);
    if (rc != 0) {
        return rc;
    }
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_DOWNLOAD;
    c[1] = 4u;
    c[2] = (uint8_t) (v & 0xFFu);
    c[3] = (uint8_t) ((v >> 8) & 0xFFu);
    c[4] = (uint8_t) ((v >> 16) & 0xFFu);
    c[5] = (uint8_t) ((v >> 24) & 0xFFu);
    return tm_req(c, 6u, r, &rl, &err);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t) (p[0] | ((uint16_t) p[1] << 8));
}

/* ------------------------------------------------------------------ */
/* 初始化                                                              */
/* ------------------------------------------------------------------ */

void tm_init(MCAN_Type *base)
{
    mcan_config_t cfg;

    s_base = base;
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000u;
    board_can_transceivers_enable();

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    {
        mcan_msg_buf_attr_t attr = {(uint32_t) (uintptr_t) s_m_msg_buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4u};
        mcan_set_msg_buf_attr(base, &attr);
    }
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = false;
    cfg.use_lowlevel_timing_setting = true;
    cfg.can_timing.prescaler = 1u;
    cfg.can_timing.num_seg1 = 139u;
    cfg.can_timing.num_seg2 = 20u;
    cfg.can_timing.num_sjw = 4u;
    board_init_can(base);
    uint32_t clk = board_init_can_clock(base);
    mcan_deinit(base);
    (void) mcan_init(base, &cfg, clk);
    s_tx_frames = 0u;
    s_rx_frames = 0u;
    s_timeouts = 0u;
    s_err_resp = 0u;
    s_fail = 0u;
}

void tm_stat(void)
{
    printf("master(CAN1): tx=%lu rx=%lu timeouts=%lu err_resp=%lu\r\n",
           (unsigned long) s_tx_frames, (unsigned long) s_rx_frames,
           (unsigned long) s_timeouts, (unsigned long) s_err_resp);
}

/* ------------------------------------------------------------------ */
/* DAQ 配置                                                            */
/* ------------------------------------------------------------------ */

static int tm_daq_write_entry(uint8_t list, uint8_t odt, uint8_t entry, uint8_t size, uint32_t addr)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_SET_DAQ_PTR;
    c[1] = 0u;
    c[2] = list;
    c[3] = 0u;
    c[4] = odt;
    c[5] = entry;
    if (tm_req(c, 6u, r, &rl, &err) != 0) {
        return -1;
    }
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_WRITE_DAQ;
    c[1] = 0xFFu;                   /* 字节对齐 */
    c[2] = size;
    c[3] = 0u;
    c[4] = (uint8_t) (addr & 0xFFu);
    c[5] = (uint8_t) ((addr >> 8) & 0xFFu);
    c[6] = (uint8_t) ((addr >> 16) & 0xFFu);
    c[7] = (uint8_t) ((addr >> 24) & 0xFFu);
    return (tm_req(c, 8u, r, &rl, &err) == 0) ? 0 : -1;
}

static int tm_daq_alloc(uint8_t list, uint8_t odts, uint8_t entries_per_odt)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_ALLOC_ODT;
    c[1] = 0u;
    c[2] = list;
    c[3] = 0u;
    c[4] = odts;
    if (tm_req(c, 5u, r, &rl, &err) != 0) {
        return -1;
    }
    for (uint8_t o = 0; o < odts; o++) {
        memset(c, 0, sizeof(c));
        c[0] = XCP_CMD_ALLOC_ODT_ENTRY;
        c[1] = 0u;
        c[2] = list;
        c[3] = 0u;
        c[4] = o;
        c[5] = entries_per_odt;
        if (tm_req(c, 6u, r, &rl, &err) != 0) {
            return -1;
        }
    }
    return 0;
}

static int tm_daq_mode(uint8_t list, uint8_t mode, uint8_t evt, uint8_t prescaler)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_SET_DAQ_LIST_MODE;
    c[1] = mode;
    c[2] = list;
    c[3] = 0u;
    c[4] = evt;
    c[5] = 0u;
    c[6] = prescaler;
    c[7] = 0u;
    return (tm_req(c, 8u, r, &rl, &err) == 0) ? 0 : -1;
}

static int tm_daq_select(uint8_t list, uint8_t *first_pid)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;

    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_START_STOP_DAQ_LIST;
    c[1] = 2u;                      /* select */
    c[2] = list;
    if (tm_req(c, 4u, r, &rl, &err) != 0) {
        return -1;
    }
    if (first_pid != NULL) {
        *first_pid = r[1];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 主要测试项                                                          */
/* ------------------------------------------------------------------ */

static void check(bool ok, const char *what)
{
    printf("  [%s] %s\r\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        s_fail++;
    }
}

static uint32_t test_connect(void)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;

    printf("-- 1) CONNECT / GET_STATUS / GET_ID --\r\n");
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_CONNECT;
    c[1] = 0u;
    int rc = tm_req(c, 2u, r, &rl, &err);
    if (rc != 0) {
        check(false, "CONNECT");
        return 1u;
    }
    uint8_t resource = r[1];
    uint8_t comm_basic = r[2];
    uint8_t max_cto = r[3];
    uint16_t max_dto = rd16(&r[4]);
    printf("     CONNECT: resource=0x%02X commBasic=0x%02X MAX_CTO=%u MAX_DTO=%u proto=%u.%u trans=%u.%u\r\n",
           (unsigned) resource, (unsigned) comm_basic, (unsigned) max_cto, (unsigned) max_dto,
           (unsigned) (r[6] >> 4), (unsigned) (r[6] & 0x0F), (unsigned) (r[7] >> 4), (unsigned) (r[7] & 0x0F));
    check((resource & XCP_RESOURCE_DAQ) != 0u, "CONNECT 报告 DAQ 资源");
    check(max_cto == 8u && max_dto == 8u, "MAX_CTO/MAX_DTO = 8");

    if (tm_cmd1(XCP_CMD_GET_STATUS) == 0) {
        uint8_t s[8];
        uint8_t sl = 0u;
        uint8_t e2 = 0u;
        uint8_t cc[8];
        memset(cc, 0, sizeof(cc));
        cc[0] = XCP_CMD_GET_STATUS;
        if (tm_req(cc, 1u, s, &sl, &e2) == 0) {
            printf("     GET_STATUS: session=0x%04X protection=0x%02X state=%u\r\n",
                   (unsigned) rd16(&s[1]), (unsigned) s[3], (unsigned) s[4]);
            check(true, "GET_STATUS");
        } else {
            check(false, "GET_STATUS");
        }
    }

    /* GET_ID(0) 取长度，然后 UPLOAD 取字符串 */
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_GET_ID;
    c[1] = 0u;
    if (tm_req(c, 2u, r, &rl, &err) == 0) {
        uint32_t n = rd32(&r[3]);
        char name[64];
        uint32_t got = 0u;
        if (n > 32u) {
            n = 32u;
        }
        while (got < n) {
            uint8_t chunk = (uint8_t) ((n - got) > 7u ? 7u : (n - got));
            memset(c, 0, sizeof(c));
            c[0] = XCP_CMD_UPLOAD;
            c[1] = chunk;
            if (tm_req(c, 2u, r, &rl, &err) != 0) {
                break;
            }
            memcpy(&name[got], &r[1], chunk);
            got += chunk;
        }
        name[n] = 0;
        printf("     GET_ID/UPLOAD: len=%lu id=\"%s\"\r\n", (unsigned long) n, name);
        check((n > 0u) && (got == n), "GET_ID + UPLOAD 取标识串");
    } else {
        check(false, "GET_ID");
    }

    if (tm_cmd1(XCP_CMD_GET_COMM_MODE_INFO) == 0) {
        check(true, "GET_COMM_MODE_INFO");
    } else {
        check(false, "GET_COMM_MODE_INFO");
    }
    return 0u;
}

static uint32_t test_cal(void)
{
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;
    uint32_t addr = (uint32_t) (uintptr_t) &g_sw_flag;

    printf("-- 2) 内存读写：SHORT_UPLOAD / DOWNLOAD / MODIFY_BITS --\r\n");
    printf("     g_sw_flag @0x%08lX = 0x%08lX\r\n", (unsigned long) addr, (unsigned long) g_sw_flag);

    g_sw_flag = 0x12345678u;
    uint8_t buf[4];
    if ((tm_read(addr, 4u, buf) == 0) && (rd32(buf) == 0x12345678u)) {
        check(true, "SHORT_UPLOAD 读回 0x12345678");
    } else {
        check(false, "SHORT_UPLOAD 读回 0x12345678");
    }

    if ((tm_write_u32(addr, 0xCAFEB0B0u) == 0) && (g_sw_flag == 0xCAFEB0B0u)) {
        printf("     DOWNLOAD 后 g_sw_flag=0x%08lX（写后立即生效）\r\n", (unsigned long) g_sw_flag);
        check(true, "DOWNLOAD 写入生效");
    } else {
        check(false, "DOWNLOAD 写入生效");
    }
    if ((tm_read(addr, 4u, buf) == 0) && (rd32(buf) == 0xCAFEB0B0u)) {
        check(true, "DOWNLOAD 后 SHORT_UPLOAD 回读一致");
    } else {
        check(false, "DOWNLOAD 后 SHORT_UPLOAD 回读一致");
    }

    /* MODIFY_BITS：把 bit8..11 置成 0xA（AND=0xF0FF、XOR=0x0A00、SHIFT=8）→ 期望 0xCAFEBAB0 */
    if (tm_read(addr, 4u, buf) == 0) {
        uint32_t before = rd32(buf);
        /* MODIFY_BITS 作用在当前 MTA 上：先 SET_MTA 指回 g_sw_flag */
        memset(c, 0, sizeof(c));
        c[0] = XCP_CMD_SET_MTA;
        c[3] = (uint8_t) (addr & 0xFFu);
        c[4] = (uint8_t) ((addr >> 8) & 0xFFu);
        c[5] = (uint8_t) ((addr >> 16) & 0xFFu);
        c[6] = (uint8_t) ((addr >> 24) & 0xFFu);
        (void) tm_req(c, 7u, r, &rl, &err);
        memset(c, 0, sizeof(c));
        c[0] = XCP_CMD_MODIFY_BITS;
        c[1] = 8u;                  /* 字段起始位 */
        c[2] = 0xF0u;               /* AND mask = 0xFFF0：清 bit8..11 */
        c[3] = 0xFFu;
        c[4] = 0x0Au;               /* XOR mask = 0x000A：置 0xA */
        c[5] = 0x00u;
        if (tm_req(c, 6u, r, &rl, &err) == 0) {
            uint32_t after = g_sw_flag;
            printf("     MODIFY_BITS: 0x%08lX -> 0x%08lX（bit8..11 置 0xA）\r\n",
                   (unsigned long) before, (unsigned long) after);
            check(after == 0xCAFEBAB0u, "MODIFY_BITS 结果 = 0xCAFEBAB0");
        } else {
            check(false, "MODIFY_BITS");
        }
    }

    /* SET_MTA + UPLOAD 走一遍（与 SHORT_UPLOAD 互证） */
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_SET_MTA;
    c[3] = (uint8_t) (addr & 0xFFu);
    c[4] = (uint8_t) ((addr >> 8) & 0xFFu);
    c[5] = (uint8_t) ((addr >> 16) & 0xFFu);
    c[6] = (uint8_t) ((addr >> 24) & 0xFFu);
    if (tm_req(c, 7u, r, &rl, &err) == 0) {
        memset(c, 0, sizeof(c));
        c[0] = XCP_CMD_UPLOAD;
        c[1] = 4u;
        if ((tm_req(c, 2u, r, &rl, &err) == 0) && (rd32(&r[1]) == g_sw_flag)) {
            check(true, "SET_MTA + UPLOAD 与变量一致");
        } else {
            check(false, "SET_MTA + UPLOAD 与变量一致");
        }
    } else {
        check(false, "SET_MTA");
    }

    /* 越界写必须被拒（内存区域白名单） */
    if (tm_write_u32(0x10000000u, 0xFFFFFFFFu) != 0) {
        check(true, "越界地址 DOWNLOAD 被拒绝");
    } else {
        check(false, "越界地址 DOWNLOAD 被拒绝");
    }

    /* GET_SEED / UNLOCK：先把保护打开再走协议 */
    static const uint8_t seed[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    xcp_set_seed(seed, 4u);
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_GET_SEED;
    c[1] = 0u;                      /* 取 first half */
    c[2] = (uint8_t) (XCP_RESOURCE_DAQ | XCP_RESOURCE_CAL_PAG);
    if ((tm_req(c, 3u, r, &rl, &err) == 0) && (r[1] == 4u) && (memcmp(&r[2], seed, 4u) == 0)) {
        printf("     GET_SEED: len=%u seed=%02X %02X %02X %02X\r\n",
               (unsigned) r[1], (unsigned) r[2], (unsigned) r[3], (unsigned) r[4], (unsigned) r[5]);
        check(true, "GET_SEED 返回 4 字节种子");
    } else {
        check(false, "GET_SEED 返回 4 字节种子");
    }
    /* 错误 key 必须被拒 */
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_UNLOCK;
    c[1] = 4u;
    c[2] = 0u;
    c[3] = 0u;
    c[4] = 0u;
    c[5] = 0u;
    if (tm_req(c, 6u, r, &rl, &err) == 1) {
        printf("     错误 UNLOCK 被拒：ERR=0x%02X\r\n", (unsigned) err);
        check(err == XCP_ERR_ACCESS_LOCKED, "错误 key 返回 ERR_ACCESS_LOCKED");
    } else {
        check(false, "错误 key 返回 ERR_ACCESS_LOCKED");
    }
    /* 正确 key = seed ^ 0xA5 */
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_UNLOCK;
    c[1] = 4u;
    for (uint8_t i = 0; i < 4u; i++) {
        c[2u + i] = (uint8_t) (seed[i] ^ 0xA5u);
    }
    if (tm_req(c, 6u, r, &rl, &err) == 0) {
        check(true, "正确 key UNLOCK 通过");
    } else {
        check(false, "正确 key UNLOCK 通过");
    }
    xcp_set_seed(NULL, 0u);         /* 关掉保护，后面 DAQ 配置不需要解锁 */
    return 0u;
}

/* ------------------------------------------------------------------ */
/* DAQ 采集                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t frames;
    uint32_t n_ts;
    uint32_t ts_first;
    uint32_t ts_last;
    uint32_t dt_min;
    uint32_t dt_max;
    uint64_t dt_sum;
    uint64_t dt_sum2;
    uint32_t prev_ts;
    uint32_t ticks;
    uint32_t tick_first;
    uint32_t tick_last;
    uint32_t lost_events;
    uint32_t prev_tick;
} tm_daq_stat_t;

static void stat_reset(tm_daq_stat_t *s)
{
    memset(s, 0, sizeof(*s));
    s->dt_min = 0xFFFFFFFFu;
}

static void stat_feed(tm_daq_stat_t *st, const uint8_t *f, uint8_t len, uint8_t pid_list0_odt0, uint8_t pid_list0_odt1)
{
    st->frames++;
    if (len < 2u) {
        return;
    }
    if (f[0] == pid_list0_odt0) {
        /* list0 ODT0: [PID][ts u32][adc u16][u8] */
        uint32_t ts = rd32(&f[1]);
        if (st->n_ts == 0u) {
            st->ts_first = ts;
            st->prev_ts = ts;
        } else {
            uint32_t dt = ts - st->prev_ts;
            if (dt < st->dt_min) {
                st->dt_min = dt;
            }
            if (dt > st->dt_max) {
                st->dt_max = dt;
            }
            st->dt_sum += dt;
            st->dt_sum2 += (uint64_t) dt * (uint64_t) dt;
            st->prev_ts = ts;
        }
        st->ts_last = ts;
        st->n_ts++;
    } else if (f[0] == pid_list0_odt1) {
        /* list0 ODT1: [PID][tick u32][cpu_load u16] */
        uint32_t tick = rd32(&f[1]);
        if (st->ticks == 0u) {
            st->tick_first = tick;
            st->prev_tick = tick;
        } else {
            uint32_t d = tick - st->prev_tick;
            if (d > 1u) {
                st->lost_events += (d - 1u);
            }
            st->prev_tick = tick;
        }
        st->tick_last = tick;
        st->ticks++;
    }
}

uint32_t tm_daq_run(uint32_t ms)
{
    static tm_daq_stat_t st;
    uint8_t buf[8];
    uint8_t len = 0u;
    uint8_t c[8];
    uint8_t r[8];
    uint8_t rl = 0u;
    uint8_t err = 0u;
    uint8_t pid0 = 0u;
    uint8_t pid1 = 1u;
    uint64_t t0;
    uint32_t dt_us;

    printf("-- 3) DAQ 配置（动态列表）--\r\n");
    if (tm_cmd1(XCP_CMD_FREE_DAQ) != 0) {
        check(false, "FREE_DAQ");
        return 1u;
    }
    check(true, "FREE_DAQ");

    /* ALLOC_DAQ(2) */
    memset(c, 0, sizeof(c));
    c[0] = XCP_CMD_ALLOC_DAQ;
    c[1] = 0u;
    c[2] = 2u;
    c[3] = 0u;
    check(tm_req(c, 4u, r, &rl, &err) == 0, "ALLOC_DAQ(2)");

    check(tm_daq_alloc(TM_LIST_DQ0, 3u, 2u) == 0, "ALLOC_ODT/ODT_ENTRY list0 = 3 ODT x 2 entry");
    check(tm_daq_alloc(TM_LIST_DQ1, 1u, 2u) == 0, "ALLOC_ODT/ODT_ENTRY list1 = 1 ODT x 2 entry");

    /* list0: ODT0 = adc(u16)+u8cnt(u8)，ODT1 = tick(u32)+cpu_load(u16)，ODT2 = pid_mid(i32) */
    bool ok_w = true;
    ok_w &= (tm_daq_write_entry(TM_LIST_DQ0, 0u, 0u, 2u, (uint32_t) (uintptr_t) &g_adc_raw) == 0);
    ok_w &= (tm_daq_write_entry(TM_LIST_DQ0, 0u, 1u, 1u, (uint32_t) (uintptr_t) &g_u8_cnt) == 0);
    ok_w &= (tm_daq_write_entry(TM_LIST_DQ0, 1u, 0u, 4u, (uint32_t) (uintptr_t) &g_tick_cnt) == 0);
    ok_w &= (tm_daq_write_entry(TM_LIST_DQ0, 1u, 1u, 2u, (uint32_t) (uintptr_t) &g_cpu_load_pm) == 0);
    ok_w &= (tm_daq_write_entry(TM_LIST_DQ0, 2u, 0u, 4u, (uint32_t) (uintptr_t) &g_pid_mid) == 0);
    check(ok_w, "WRITE_DAQ list0 5 个条目");

    /* list1: ODT0 = pid_mid(i32)+adc(u16)（无时间戳，7 字节装满 6） */
    ok_w = true;
    ok_w &= (tm_daq_write_entry(TM_LIST_DQ1, 0u, 0u, 4u, (uint32_t) (uintptr_t) &g_pid_mid) == 0);
    ok_w &= (tm_daq_write_entry(TM_LIST_DQ1, 0u, 1u, 2u, (uint32_t) (uintptr_t) &g_adc_raw) == 0);
    check(ok_w, "WRITE_DAQ list1 2 个条目");

    check(tm_daq_mode(TM_LIST_DQ0, (uint8_t) (XCP_DAQ_MODE_TIMESTAMP), 0u, 1u) == 0,
          "SET_DAQ_LIST_MODE list0（EVT0 1 kHz + 时间戳）");
    check(tm_daq_mode(TM_LIST_DQ1, 0u, 1u, 1u) == 0, "SET_DAQ_LIST_MODE list1（EVT1 100 Hz）");

    if (tm_daq_select(TM_LIST_DQ0, &pid0) != 0) {
        check(false, "START_STOP_DAQ_LIST(select) list0");
        return 1u;
    }
    if (tm_daq_select(TM_LIST_DQ1, &pid1) != 0) {
        check(false, "START_STOP_DAQ_LIST(select) list1");
        return 1u;
    }
    printf("     first PID: list0=%u list1=%u（PID = 绝对 ODT 号）\r\n", (unsigned) pid0, (unsigned) pid1);

    /* GET_DAQ_CLOCK 对时（用于时间戳单位校验） */
    uint32_t clk0 = 0u;
    if (tm_cmd1(XCP_CMD_GET_DAQ_CLOCK) == 0) {
        uint8_t q[8];
        uint8_t ql = 0u;
        uint8_t qe = 0u;
        memset(q, 0, sizeof(q));
        q[0] = XCP_CMD_GET_DAQ_CLOCK;
        if (tm_req(q, 1u, r, &ql, &qe) == 0) {
            clk0 = rd32(&r[1]);
            printf("     GET_DAQ_CLOCK = %lu us\r\n", (unsigned long) clk0);
        }
    }

    if (tm_cmd3(XCP_CMD_START_STOP_SYNCH, 1u, 0u) != 0) {
        check(false, "START_STOP_SYNCH(start)");
        return 1u;
    }

    printf("-- 4) DAQ 采集 %lu ms --\r\n", (unsigned long) ms);
    stat_reset(&st);
    uint8_t pid0_odt0 = pid0;
    uint8_t pid0_odt1 = (uint8_t) (pid0 + 1u);
    t0 = hpm_csr_get_core_mcycle();
    dt_us = 0u;
    while (dt_us < ms * 1000u) {
        xcp_poll();
        if (tm_rx(buf, &len, 2000u)) {
            if ((buf[0] < 0xF0u)) {
                stat_feed(&st, buf, len, pid0_odt0, pid0_odt1);
            }
        }
        dt_us = us_since(t0);
    }
    (void) tm_cmd3(XCP_CMD_START_STOP_SYNCH, 0u, 0u);

    /* 统计 */
    uint32_t dur_us = us_since(t0);
    uint32_t n_evt = st.ticks;
    double dur_s = (double) dur_us / 1e6;
    double rate0 = (double) n_evt / dur_s;
    uint32_t jit_mean = (st.n_ts > 1u) ? (uint32_t) (st.dt_sum / (uint64_t) (st.n_ts - 1u)) : 0u;
    double var = 0.0;
    if (st.n_ts > 1u) {
        double m = (double) jit_mean;
        var = (double) st.dt_sum2 / (double) (st.n_ts - 1u) - m * m;
    }
    uint32_t jit_sd = 0u;
    if (var > 0.0) {
        /* 整数牛顿迭代开方，避免浮点库依赖 */
        uint32_t x = (uint32_t) var;
        uint32_t y = x / 2u + 1u;
        for (uint32_t i = 0; i < 16u; i++) {
            y = (y + x / y) / 2u;
        }
        jit_sd = y;
    }
    uint32_t frame_rate = (uint32_t) ((double) st.frames / dur_s);
    printf("     list0 事件数=%lu 实测事件率=%.1f Hz（配置 1000 Hz）\r\n",
           (unsigned long) n_evt, rate0);
    printf("     list0 ODT 帧数=%lu（每事件 3 帧）总帧率=%lu frame/s\r\n",
           (unsigned long) (st.n_ts * 3u + st.ticks * 0u), (unsigned long) frame_rate);
    printf("     时间戳抖动：n=%lu mean=%lu us min=%lu us max=%lu us sd=%lu us\r\n",
           (unsigned long) st.n_ts, (unsigned long) jit_mean, (unsigned long) st.dt_min,
           (unsigned long) st.dt_max, (unsigned long) jit_sd);
    printf("     事件丢失：lost_events=%lu（按 g_tick_cnt 连续计数判定）\r\n",
           (unsigned long) st.lost_events);

    check(n_evt > 0u, "DAQ 收到事件采样");
    check((rate0 > 900.0) && (rate0 < 1100.0), "list0 事件率在 900~1100 Hz");
    check(st.lost_events == 0u, "list0 无丢帧");
    check((jit_mean > 900u) && (jit_mean < 1100u), "时间戳平均间隔 ≈ 1000 us");
    check(st.dt_max < 3000u, "时间戳最大间隔 < 3 ms");

    /* 查询总线/发送统计 */
    xcp_stats_t xs;
    xcp_get_stats(&xs);
    printf("     slave: cto_rx=%lu daq_frames=%lu dropped=%lu overload=%lu\r\n",
           (unsigned long) xs.cto_rx, (unsigned long) xs.daq_frames,
           (unsigned long) xs.daq_dropped, (unsigned long) xs.daq_overload);
    check(xs.daq_dropped == 0u, "从站 DAQ 队列无丢弃");
    check(xs.daq_overload == 0u, "从站 CAN 发送无 TX FIFO 满");

    /* 停掉 DAQ 后仍能正常响应命令 */
    check(tm_cmd1(XCP_CMD_GET_STATUS) == 0, "停止 DAQ 后命令通道仍正常");
    return 0u;
}

uint32_t tm_selftest(uint32_t daq_ms)
{
    s_fail = 0u;
    printf("\r\n=== XCP 板内自证（CAN1 主机 <-> CAN0 从站，0x555/0x556）===\r\n");
    uint32_t f0 = s_fail;
    test_connect();
    test_cal();
    (void) tm_daq_run(daq_ms);
    (void) tm_cmd1(XCP_CMD_DISCONNECT);
    printf("=== 自证结论：%s（FAIL=%lu）===\r\n", (s_fail == 0u) ? "PASS" : "FAIL", (unsigned long) s_fail);
    printf("XCP SELFTEST %s fail=%lu\r\n", (s_fail == 0u) ? "PASS" : "FAIL", (unsigned long) s_fail);
    (void) f0;
    return s_fail;
}
