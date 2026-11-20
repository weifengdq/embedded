/**
 * @file    s2500.c
 * @brief   T30HM1TS2500 (S2500) 10BASE-T1S MAC-PHY 通用驱动实现
 *
 * OPEN Alliance 10BASE-T1x MAC-PHY Serial Interface (OA-SPI):
 *   - 控制事务: 4B 命令头 + 4B 占位 + 4B 数据, CS_N 每次事务翻转
 *   - 数据事务: 4B 块头 + 64B 负载, 同时返回 64B 负载 + 4B 块尾
 *   - 所有 32-bit 字 MSB first, 头/尾 bit0 为奇校验
 *
 * 许可: MIT
 */
#include "s2500.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* 小工具                                                             */
/* ------------------------------------------------------------------ */
#define S2500_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static inline uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* OA-SPI 头部奇校验: 返回应写入 bit0 的值 */
uint32_t s2500_oaspi_parity(uint32_t v)
{
    v >>= 1;
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1u) ? 0u : 1u;   /* 补齐为奇数个 1 */
}

static void s2500_log(s2500_dev_t *dev, const char *fmt, ...)
{
    (void)dev;
    (void)fmt;
    /* 平台可通过 hal->log 提供日志, 这里不做任何事 */
}

static void s2500_delay_us(s2500_dev_t *dev, uint32_t us)
{
    if (dev->hal && dev->hal->delay_us) {
        dev->hal->delay_us(dev->hal->ctx, us);
    }
}

static uint32_t s2500_millis(s2500_dev_t *dev)
{
    if (dev->hal && dev->hal->millis) {
        return dev->hal->millis(dev->hal->ctx);
    }
    return 0;
}

static int s2500_timeout(s2500_dev_t *dev, uint32_t start, uint32_t ms)
{
    if (!dev->hal || !dev->hal->millis) {
        return 0;
    }
    return (s2500_millis(dev) - start) > ms;
}

static int s2500_xfer(s2500_dev_t *dev, uint32_t len)
{
    if (!dev->hal || !dev->hal->spi_xfer) {
        return S2500_ERR;
    }
    int rc = dev->hal->spi_xfer(dev->hal->ctx, dev->txb, dev->rxb, len);
    dev->stats.ctrl_xfer++;
    return (rc == 0) ? S2500_OK : S2500_ERR_SPI;
}

/* ------------------------------------------------------------------ */
/* 控制事务                                                           */
/* ------------------------------------------------------------------ */
static uint32_t ctrl_header(int write, uint8_t mms, uint16_t addr, uint8_t len_m1)
{
    uint32_t h = 0;
    if (write)      h |= (1u << 29);          /* WNR */
    h |= (1u << 28);                          /* AID = 1, 单寄存器 */
    h |= ((uint32_t)(mms & 0xFu) << 24);
    h |= ((uint32_t)addr << 8);
    h |= ((uint32_t)(len_m1 & 0x7Fu) << 1);
    h |= s2500_oaspi_parity(h);
    return h;
}

static int ctrl_transaction(s2500_dev_t *dev, int write, uint8_t mms,
                            uint16_t addr, uint32_t *value)
{
    int attempt;
    for (attempt = 0; attempt < 3; attempt++) {
        uint32_t hdr = ctrl_header(write, mms, addr, 0);
        memset(dev->txb, 0, S2500_CTRL_XFER_LEN);
        memset(dev->rxb, 0, S2500_CTRL_XFER_LEN);
        wr_be32(&dev->txb[0], hdr);
        if (write && value) {
            wr_be32(&dev->txb[4], *value);
        }
        if (s2500_xfer(dev, S2500_CTRL_XFER_LEN) != S2500_OK) {
            continue;
        }
        uint32_t echo = rd_be32(&dev->rxb[4]);
        if ((echo & 1u) != s2500_oaspi_parity(echo)) {
            dev->stats.ctrl_parity_err++;
            continue;
        }
        if (echo & (1u << 30)) {           /* HDRB: 芯片收到坏头 */
            dev->stats.header_bad++;
            continue;
        }
        if (!write && value) {
            *value = rd_be32(&dev->rxb[8]);
        }
        return S2500_OK;
    }
    return S2500_ERR_SPI;
}

int s2500_read_reg(s2500_dev_t *dev, uint8_t mms, uint16_t addr, uint32_t *val)
{
    if (!dev || !val) {
        return S2500_ERR_PARAM;
    }
    return ctrl_transaction(dev, 0, mms, addr, val);
}

int s2500_write_reg(s2500_dev_t *dev, uint8_t mms, uint16_t addr, uint32_t val)
{
    if (!dev) {
        return S2500_ERR_PARAM;
    }
    return ctrl_transaction(dev, 1, mms, addr, &val);
}

int s2500_update_bits(s2500_dev_t *dev, uint8_t mms, uint16_t addr,
                      uint32_t clr, uint32_t set)
{
    uint32_t v;
    if (s2500_read_reg(dev, mms, addr, &v) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    v = (v & ~clr) | set;
    return s2500_write_reg(dev, mms, addr, v);
}

int s2500_reg_field_write(s2500_dev_t *dev, uint8_t mms, uint16_t addr,
                          uint32_t mask, uint32_t shift, uint32_t value)
{
    uint32_t v;
    if (s2500_read_reg(dev, mms, addr, &v) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    v = (v & ~mask) | ((value << shift) & mask);
    return s2500_write_reg(dev, mms, addr, v);
}

/* ------------------------------------------------------------------ */
/* 数据块事务                                                         */
/* ------------------------------------------------------------------ */
static void build_data_header(s2500_dev_t *dev, int dv, int sv, int ev,
                              uint8_t ebo, uint8_t tsc, int norx)
{
    uint32_t h = (1u << 31);              /* DNC = 1 (数据) */
    if (norx)   h |= (1u << 29);
    if (dv)     h |= (1u << 21);
    if (sv)     h |= (1u << 20);
    if (ev)     h |= (1u << 14);
    h |= ((uint32_t)(ebo & 0x3Fu) << 8);
    h |= ((uint32_t)(tsc & 0x3u) << 6);
    h |= s2500_oaspi_parity(h);
    wr_be32(&dev->txb[0], h);
}

/**
 * @brief 申请 need 个 TX chunk 信用
 *
 * BUFSTS.TXC 只会增加 (芯片发完一帧就还回来), 因此本地递减是保守估计:
 * 只要本地计数够用就不必读寄存器, 每块可以省掉一次 12 字节控制事务
 * (1500 字节帧 24 块 → 省掉 23 次 BUFSTS 读, SPI 时间 -15%)。
 */
static int wait_tx_credits(s2500_dev_t *dev, uint32_t need, uint32_t timeout_ms)
{
    uint32_t start = s2500_millis(dev);

    if (dev->tx_credit >= (int32_t)need) {
        dev->tx_credit -= (int32_t)need;
        return S2500_OK;
    }

    for (;;) {
        uint32_t v = 0;
        if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_BUFSTS, &v) != S2500_OK) {
            dev->tx_credit = -1;
            return S2500_ERR_SPI;
        }
        uint32_t txc = (v & S2500_BUFSTS_TXC_MASK) >> S2500_BUFSTS_TXC_SHIFT;
        if (txc >= need) {
            dev->tx_credit = (int32_t)(txc - need);
            return S2500_OK;
        }
        dev->stats.tx_busy_waits++;
        if (timeout_ms && s2500_timeout(dev, start, timeout_ms)) {
            dev->tx_credit = -1;
            return S2500_ERR_TIMEOUT;
        }
        s2500_delay_us(dev, 20);
    }
}

uint32_t s2500_tx_chunks_available(s2500_dev_t *dev)
{
    uint32_t v = 0;
    if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_BUFSTS, &v) != S2500_OK) {
        return 0;
    }
    return (v & S2500_BUFSTS_TXC_MASK) >> S2500_BUFSTS_TXC_SHIFT;
}

uint32_t s2500_rx_chunks_available(s2500_dev_t *dev)
{
    uint32_t v = 0;
    if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_BUFSTS, &v) != S2500_OK) {
        return 0;
    }
    return (v & S2500_BUFSTS_RCA_MASK) >> S2500_BUFSTS_RCA_SHIFT;
}

int s2500_send(s2500_dev_t *dev, const uint8_t *frame, uint16_t len, uint32_t flags)
{
    uint16_t eff_len;
    uint16_t offset;
    uint8_t  tsc = 0;

    if (!dev || !frame || len == 0 || len > (S2500_MAX_FRAME - 4)) {
        return S2500_ERR_PARAM;
    }
    if (flags & S2500_XMIT_TS_A) tsc = 1;
    if (flags & S2500_XMIT_TS_B) tsc = 2;
    if (flags & S2500_XMIT_TS_C) tsc = 3;

    /* 以太网最小帧 60 字节(不含 FCS), 不足时补零 */
    eff_len = (len < S2500_MIN_FRAME) ? S2500_MIN_FRAME : len;

    for (offset = 0; offset < eff_len; offset = (uint16_t)(offset + S2500_CHUNK_SIZE)) {
        uint16_t remain = (uint16_t)(eff_len - offset);
        int is_first = (offset == 0);
        int is_last  = (remain <= S2500_CHUNK_SIZE);
        uint16_t copy_len = (remain > S2500_CHUNK_SIZE) ? S2500_CHUNK_SIZE : remain;
        uint8_t  ebo = is_last ? (uint8_t)(copy_len - 1) : 0;

        if (wait_tx_credits(dev, 1, 200) != S2500_OK) {
            dev->tx_credit = -1;
            dev->stats.tx_dropped++;
            return S2500_ERR_TIMEOUT;
        }
        memset(dev->txb, 0, S2500_DATA_XFER_LEN);
        memset(dev->rxb, 0, S2500_DATA_XFER_LEN);
        build_data_header(dev, 1, is_first, is_last, ebo,
                          is_first ? tsc : 0, 1);

        /* 负载: 超出实际帧长的部分自动为 0 (以太网填充) */
        {
            uint16_t real = (offset < len) ? (uint16_t)(len - offset) : 0;
            uint16_t n = (real < copy_len) ? real : copy_len;
            if (n) {
                memcpy(&dev->txb[4], &frame[offset], n);
            }
        }

        if (s2500_xfer(dev, S2500_DATA_XFER_LEN) != S2500_OK) {
            dev->tx_credit = -1;
            dev->stats.tx_dropped++;
            return S2500_ERR_SPI;
        }

        {
            uint32_t ftr = rd_be32(&dev->rxb[S2500_CHUNK_SIZE]);
            if ((ftr & 1u) != s2500_oaspi_parity(ftr)) {
                dev->stats.footer_parity_err++;
                return S2500_ERR_PROTOCOL;
            }
            if (ftr & (1u << 30)) {          /* HDRB */
                dev->stats.header_bad++;
                return S2500_ERR_PROTOCOL;
            }
            if (ftr & (1u << 15)) {          /* FD: 帧被丢弃 */
                dev->stats.tx_dropped++;
                return S2500_ERR_TX;
            }
        }
        dev->stats.tx_chunks++;
    }

    dev->stats.tx_frames++;
    dev->stats.tx_bytes += eff_len;
    return S2500_OK;
}

/* CRC32 (以太网 FCS, 反射多项式 0xEDB88320) */
/* CRC32 (poly 0xEDB88320) 半字节查表: 与原逐位算法结果完全一致,
 * 但 1460 字节只花 ~8k 周期 (~66us @120MHz), 原逐位版本约 ~60k 周期 (~0.5ms),
 * 这段在收完一帧到交给 lwIP 之间串行执行, 直接压在 ACK 时延上
 * (2026-11-20 实测: 下行每帧多花 ~2ms, TCP 被卡在 3.6Mbit/s)。 */
static const uint32_t s_crc32_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

static uint32_t s2500_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        crc = (crc >> 4) ^ s_crc32_nibble[crc & 0x0Fu];
        crc = (crc >> 4) ^ s_crc32_nibble[crc & 0x0Fu];
    }
    return ~crc;
}

/* 接收重组状态 (放在 dev 之外的静态区, 每个 dev 一份) */
typedef struct {
    uint8_t  buf[S2500_MAX_FRAME];
    uint16_t len;
    uint8_t  active;
} s2500_rx_ctx_t;

static s2500_rx_ctx_t s_rx_ctx[1];

int s2500_recv(s2500_dev_t *dev, uint8_t *frame, uint16_t *len, uint32_t *ts)
{
    s2500_rx_ctx_t *rx = &s_rx_ctx[0];
    int guard = 0;

    if (!dev || !frame || !len) {
        return S2500_ERR_PARAM;
    }
    *len = 0;
    if (ts) *ts = 0;

    rx->len = 0;
    rx->active = 0;

    /* 快速路径: 没有可读 chunk 时不做 68B 数据事务 */
    {
        uint32_t bst = 0;
        if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_BUFSTS, &bst) != S2500_OK) {
            return S2500_ERR_SPI;
        }
        if (((bst & S2500_BUFSTS_RCA_MASK) >> S2500_BUFSTS_RCA_SHIFT) == 0) {
            dev->stats.rx_polls++;
            return S2500_ERR_NO_FRAME;
        }
    }

    while (guard++ < 64) {
        uint32_t ftr;
        int dv, sv, ev;
        uint8_t swo, ebo, sbo;
        uint32_t copy_off, copy_len;

        memset(dev->txb, 0, S2500_DATA_XFER_LEN);
        memset(dev->rxb, 0, S2500_DATA_XFER_LEN);
        build_data_header(dev, 0, 0, 0, 0, 0, 0);
        if (s2500_xfer(dev, S2500_DATA_XFER_LEN) != S2500_OK) {
            return S2500_ERR_SPI;
        }
        ftr = rd_be32(&dev->rxb[S2500_CHUNK_SIZE]);
        if ((ftr & 1u) != s2500_oaspi_parity(ftr)) {
            dev->stats.footer_parity_err++;
            return S2500_ERR_PROTOCOL;
        }
        if (ftr & (1u << 30)) {
            dev->stats.header_bad++;
            return S2500_ERR_PROTOCOL;
        }
        if (ftr & (1u << 15)) {              /* FD: FCS 错误 / 丢弃 */
            dev->stats.rx_fcs_err++;
            rx->len = 0;                     /* 丢弃已重组的半帧 */
            continue;
        }

        dv  = (ftr >> 21) & 1;
        sv  = (ftr >> 20) & 1;
        ev  = (ftr >> 14) & 1;
        swo = (uint8_t)((ftr >> 16) & 0xF);
        ebo = (uint8_t)((ftr >> 8) & 0x3F);
        sbo = (uint8_t)(swo * 4u);

        if (!dv) {
            if (rx->len > 0) {
                /* 帧未以 EV 结束, 视为截断帧丢弃 */
                dev->stats.rx_dropped++;
                rx->len = 0;
            }
            return S2500_ERR_NO_FRAME;
        }
        dev->stats.rx_chunks++;

        /* 关键: EBO 只在 EV=1 时有效; 中间块的 EBO 可能为 0,
         * 因此非结束块必须整块拷贝 (LAN8651 参考实现也是这么做的)。 */
        if (rx->len == 0) {
            if (!sv) {
                dev->stats.rx_resync++;
                continue;                     /* 丢弃半帧 */
            }
        }
        copy_off = sbo;
        copy_len = (ev ? ((uint32_t)ebo + 1u) : (uint32_t)S2500_CHUNK_SIZE) - copy_off;
        if (copy_len > S2500_CHUNK_SIZE || copy_off + copy_len > S2500_CHUNK_SIZE) {
            dev->stats.rx_resync++;
            rx->len = 0;
            continue;
        }
        if (rx->len + copy_len > S2500_MAX_FRAME) {
            dev->stats.rx_overflow++;
            rx->len = 0;
            continue;
        }
        memcpy(&rx->buf[rx->len], &dev->rxb[copy_off], copy_len);
        rx->len = (uint16_t)(rx->len + copy_len);

        /* 同一块里结束当前帧并开始下一帧 (防御性处理) */
        if (ev && ((uint32_t)ebo + 1u) < S2500_CHUNK_SIZE) {
            /* 结束; 下一帧将在后续块中重新开始 (ZARFE=1 时不会出现在本块) */
            break;
        }
        if (ev) {
            break;
        }
    }

    if (rx->len < 14) {
        return S2500_ERR_NO_FRAME;
    }

    /* S2500 不做 FCS 剥离: 校验并去掉末尾 4 字节 */
    if (rx->len >= 64) {
        uint32_t calc = s2500_crc32(rx->buf, (uint32_t)(rx->len - 4));
        uint32_t got = (uint32_t)rx->buf[rx->len - 4] |
                       ((uint32_t)rx->buf[rx->len - 3] << 8) |
                       ((uint32_t)rx->buf[rx->len - 2] << 16) |
                       ((uint32_t)rx->buf[rx->len - 1] << 24);
        if (calc == got) {
            rx->len = (uint16_t)(rx->len - 4);
        }
    } else {
        /* 短帧不可能带合法 FCS, 保持原样 */
    }

    memcpy(frame, rx->buf, rx->len);
    *len = rx->len;
    dev->stats.rx_frames++;
    dev->stats.rx_bytes += rx->len;
    return S2500_OK;
}

/* ------------------------------------------------------------------ */
/* 复位 / 探测 / 配置                                                 */
/* ------------------------------------------------------------------ */
int s2500_soft_reset(s2500_dev_t *dev)
{
    uint32_t v, start;
    if (s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_RESET, 1) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    s2500_delay_us(dev, 2000);
    start = s2500_millis(dev);
    for (;;) {
        if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_STATUS0, &v) != S2500_OK) {
            return S2500_ERR_SPI;
        }
        if (v & S2500_STATUS0_RESETC) {
            break;
        }
        if (s2500_timeout(dev, start, 100)) {
            return S2500_ERR_TIMEOUT;
        }
        s2500_delay_us(dev, 200);
    }
    /* 清掉复位完成 */
    s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_STATUS0, S2500_STATUS0_RESETC);
    dev->stats.tx_chunks = 0;
    return S2500_OK;
}

int s2500_probe(s2500_dev_t *dev)
{
    uint32_t id = 0, phy = 0;
    if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_IDVER, &id) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    if (id == 0 || id == 0xFFFFFFFFu) {
        return S2500_ERR_PROTOCOL;
    }
    if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_PHYID, &phy) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    return S2500_OK;
}

int s2500_get_factory_mac(s2500_dev_t *dev, uint8_t mac[6])
{
    uint32_t lo = 0, hi = 0;
    if (s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_MACID0, &lo) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    if (s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_MACID1, &hi) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    /* MACID0 = 地址低 32 位, MACID1[15:0] = 地址高 16 位, 均为数值形式 */
    mac[0] = (uint8_t)(hi >> 8);
    mac[1] = (uint8_t)(hi >> 0);
    mac[2] = (uint8_t)(lo >> 24);
    mac[3] = (uint8_t)(lo >> 16);
    mac[4] = (uint8_t)(lo >> 8);
    mac[5] = (uint8_t)(lo >> 0);
    return S2500_OK;
}

int s2500_set_mac(s2500_dev_t *dev, const uint8_t mac[6])
{
    uint32_t lo = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
                  ((uint32_t)mac[4] << 8) | (uint32_t)mac[5];
    uint32_t hi = ((uint32_t)mac[0] << 8) | (uint32_t)mac[1];
    int rc;
    memcpy(dev->mac, mac, 6);
    /* 过滤器 0: 精确匹配本机 MAC */
    rc = s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MADDRFILT0L, lo);
    if (rc != S2500_OK) return rc;
    rc = s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MADDRMASK0L, 0xFFFFFFFFu);
    if (rc != S2500_OK) return rc;
    rc = s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MADDRMASK0H, 0x0000FFFFu);
    if (rc != S2500_OK) return rc;
    rc = s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MADDRFILT0H, hi | (1u << 31));
    return rc;
}

static int s2500_dio_defaults(s2500_dev_t *dev)
{
    /* DIO0 (黄) = ACT: TX/RX 活动; DIO1 (绿) = LINK: PLCA 状态 (PLCA 模式)
     * 两块板均为高电平点亮, 因此 VALx = 1。 */
    uint32_t v = 0;
    v |= S2500_DIO_SLW(0) | S2500_DIO_PEN(0) | S2500_DIO_FN(0, S2500_DIOFN_LED_TXRX) | S2500_DIO_VAL(0, 1);
    v |= S2500_DIO_SLW(1) | S2500_DIO_PEN(1) | S2500_DIO_FN(1, S2500_DIOFN_LED_PLCASTS) | S2500_DIO_VAL(1, 1);
    return s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_DIOCFG0, v);
}

int s2500_init(s2500_dev_t *dev, const s2500_hal_t *hal)
{
    uint32_t v;
    uint8_t mac[6];

    if (!dev || !hal || !hal->spi_xfer) {
        return S2500_ERR_PARAM;
    }
    memset(dev, 0, sizeof(*dev));
    dev->hal = hal;
    dev->chunk_size = S2500_CHUNK_SIZE;
    dev->tx_credit = -1;                 /* TX 信用缓存未知, 首次发送时读 BUFSTS */

    s2500_delay_us(dev, 1000);

    if (s2500_probe(dev) != S2500_OK) {
        return S2500_ERR_PROTOCOL;
    }
    /* 等待 RESETC */
    {
        uint32_t start = s2500_millis(dev);
        for (;;) {
            if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_STATUS0, &v) != S2500_OK) {
                return S2500_ERR_SPI;
            }
            if (v & S2500_STATUS0_RESETC) break;
            if (s2500_timeout(dev, start, 50)) break;
        }
    }
    s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_STATUS0, S2500_STATUS0_ERR_MASK);

    /* CONFIG0: 64B chunk, 接收帧对齐到块首, 8 个 TX credit 阈值, SYNC=1 */
    v = S2500_CONFIG0_CPS_64 | S2500_CONFIG0_ZARFE |
        S2500_CONFIG0_TXCTHRESH_8 | S2500_CONFIG0_SYNC;
    if (s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_CONFIG0, v) != S2500_OK) {
        return S2500_ERR_SPI;
    }

    /* 中断全部屏蔽, 由驱动轮询; 需要时可打开 */
    s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_IMSK0, 0x00001FBFu);
    s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_IMSK1, 0x00000003u);
    dev->irq_mask = 0x00001FBFu;

    /* 出厂 MAC */
    if (s2500_get_factory_mac(dev, mac) == S2500_OK) {
        if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0 &&
            !(mac[0] & 1u)) {
            s2500_set_mac(dev, mac);
        } else {
            memcpy(dev->mac, mac, 6);
        }
    }

    /* MAC 控制: FCS 由 MAC 生成, 先关闭收发 */
    /* 注意: 不能置 MCSF/BCSF, 否则广播 ARP 与组播会被丢弃 */
    s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0,
                    S2500_MC_FCSA | S2500_MC_ADRF);

    /* 模拟推荐值: TX_SLW = middle, 其余保持默认 (CLK_O 关闭以降低 EMI) */
    {
        uint32_t t2 = 0;
        s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_TWEAKS2, &t2);
        t2 &= ~(3u << S2500_TWEAKS2_TXSLW_SHIFT);
        t2 |= (1u << S2500_TWEAKS2_TXSLW_SHIFT);
        t2 &= ~S2500_TWEAKS2_CLKO_ENA;
        s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_TWEAKS2, t2);
    }

    /* PLCA 默认: 关闭, ID=255 */
    s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL1,
                    ((uint32_t)8 << S2500_PLCA_NCNT_SHIFT) | 255u);
    s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCATOTMR, 32u);

    /* PHY 收发使能 (CONTROL.LCTL=1), 否则链路/收发均不工作 */
    s2500_update_bits(dev, S2500_MMS_STD, S2500_REG_PHY_CTRL,
                      S2500_PHYCTRL_LOOPBACK | S2500_PHYCTRL_LRST | S2500_PHYCTRL_RESET,
                      S2500_PHYCTRL_LCTL);

    s2500_dio_defaults(dev);
    s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_STATUS0, S2500_STATUS0_ERR_MASK);
    (void)s2500_log;
    return S2500_OK;
}

int s2500_configure(s2500_dev_t *dev, const s2500_cfg_t *cfg)
{
    uint32_t c0, mc;

    if (!dev || !cfg) {
        return S2500_ERR_PARAM;
    }

    /* 配置期间先关闭 SYNC */
    if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_CONFIG0, &c0) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    c0 &= ~S2500_CONFIG0_SYNC;
    c0 &= ~(S2500_CONFIG0_TXCTE | S2500_CONFIG0_RXCTE | S2500_CONFIG0_FTSE);
    if (cfg->cut_through_tx) c0 |= S2500_CONFIG0_TXCTE;
    if (cfg->cut_through_rx) c0 |= S2500_CONFIG0_RXCTE;
    if (cfg->ts_en) c0 |= S2500_CONFIG0_FTSE;
    if (s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_CONFIG0, c0) != S2500_OK) {
        return S2500_ERR_SPI;
    }

    if (cfg->mac) {
        s2500_set_mac(dev, cfg->mac);
    }

    /* MAC 控制 */
    if (s2500_read_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0, &mc) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    mc |= S2500_MC_FCSA;
    if (cfg->promiscuous) {
        mc &= ~S2500_MC_ADRF;
    } else {
        mc |= S2500_MC_ADRF;
    }
    mc &= ~(S2500_MC_BCSF | S2500_MC_MCSF);   /* 允许广播/组播 (lwIP 需要) */
    if (s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0, mc) != S2500_OK) {
        return S2500_ERR_SPI;
    }

    /* PLCA */
    if (s2500_plca_configure(dev, cfg->plca_id, cfg->plca_ncnt,
                             cfg->plca_totmr, cfg->plca_en) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    s2500_plca_redundancy(dev, cfg->plca_cr);
    s2500_plca_precedence(dev, cfg->plca_prec);

    /* 恢复 SYNC */
    c0 |= S2500_CONFIG0_SYNC;
    if (s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_CONFIG0, c0) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    dev->plca_id = cfg->plca_id;
    dev->plca_ncnt = cfg->plca_ncnt;
    return S2500_OK;
}

int s2500_start(s2500_dev_t *dev)
{
    uint32_t mc;
    s2500_update_bits(dev, S2500_MMS_STD, S2500_REG_PHY_CTRL, 0, S2500_PHYCTRL_LCTL);
    if (s2500_read_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0, &mc) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    mc |= (S2500_MC_TXEN | S2500_MC_RXEN);
    return s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0, mc);
}

int s2500_stop(s2500_dev_t *dev)
{
    uint32_t mc;
    if (s2500_read_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0, &mc) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    mc &= ~(S2500_MC_TXEN | S2500_MC_RXEN);
    return s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0, mc);
}

/* ------------------------------------------------------------------ */
/* 自愈看门狗                                                         */
/* ------------------------------------------------------------------ */
static s2500_wd_t s_wd = {
    .enabled   = 1,
    .period_ms = (uint16_t)S2500_WD_PERIOD_MS,
};
static s2500_wd_reinit_fn s_wd_reinit;
static void *s_wd_reinit_ctx;

void s2500_wd_set_reinit_cb(s2500_wd_reinit_fn cb, void *ctx)
{
    s_wd_reinit = cb;
    s_wd_reinit_ctx = ctx;
}

s2500_wd_t *s2500_wd_state(void)
{
    return &s_wd;
}

void s2500_wd_set_enabled(int on)
{
    s_wd.enabled = on ? 1u : 0u;
}

int s2500_wd_enabled(void)
{
    return s_wd.enabled ? 1 : 0;
}

void s2500_wd_reset(void)
{
    uint8_t enabled = s_wd.enabled;
    uint16_t period = s_wd.period_ms;
    memset(&s_wd, 0, sizeof(s_wd));
    s_wd.enabled = enabled ? 1u : 0u;
    s_wd.period_ms = period ? period : (uint16_t)S2500_WD_PERIOD_MS;
}

int s2500_wd_poll(s2500_dev_t *dev)
{
    uint32_t now, ctl1 = 0, cfg0 = 0;
    uint8_t id, ncnt, reason = S2500_WD_REASON_NONE;

    if (!dev || !dev->hal || !dev->hal->millis || !s_wd.enabled) {
        return 0;
    }
    now = dev->hal->millis(dev->hal->ctx);
    if (!s_wd.started) {
        s_wd.started = 1;
        s_wd.last_check_ms = now;
        return 0;
    }
    if ((uint32_t)(now - s_wd.last_check_ms) < (uint32_t)s_wd.period_ms) {
        return 0;
    }
    s_wd.last_check_ms = now;
    s_wd.checks++;

    /* 1 次 12B 控制事务 (~6us): 控制/状态寄存器不会被正常收发改写 */
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL1, &ctl1) != S2500_OK) {
        /* TC10 Sleep 期间芯片不响应 SPI, 属正常; 总线异常也不误判复位 */
        s_wd.spi_errors++;
        return 0;
    }
    id   = (uint8_t)(ctl1 & S2500_PLCA_ID_MASK);
    ncnt = (uint8_t)((ctl1 >> S2500_PLCA_NCNT_SHIFT) & 0xFFu);

    if (id != dev->plca_id) {
        reason = S2500_WD_REASON_ID;
    } else if (dev->plca_ncnt && ncnt != dev->plca_ncnt) {
        reason = S2500_WD_REASON_NCNT;
    } else if (dev->plca_id == 255u) {
        /* PLCA 未启用时 ID 恒为 255, 无法与默认态区分, 用 CONFIG0.SYNC 兜底 */
        if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_CONFIG0, &cfg0) != S2500_OK) {
            s_wd.spi_errors++;
            return 0;
        }
        if (!(cfg0 & S2500_CONFIG0_SYNC)) {
            reason = S2500_WD_REASON_SYNC;
        }
    }

    /* 每 10 次检查核对一次 IDVER, 兜底捕获"ID 恰好相同"的异常 */
    if (reason == S2500_WD_REASON_NONE && (s_wd.checks % 10u) == 0u) {
        uint32_t idver = 0;
        if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_IDVER, &idver) != S2500_OK) {
            s_wd.spi_errors++;
        } else if (idver != S2500_WD_IDVER) {
            reason = S2500_WD_REASON_IDVER;
        }
    }
    if (reason == S2500_WD_REASON_NONE) {
        return 0;
    }

    s_wd.detections++;
    s_wd.last_reason = reason;
    s_wd.last_detect_ms = now;
    if (s_wd_reinit) {
        if (s_wd_reinit(s_wd_reinit_ctx) == 0) {
            s_wd.reinit_ok++;
            s_wd.last_recover_ms = dev->hal->millis(dev->hal->ctx);
        } else {
            s_wd.reinit_fail++;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 状态 / 诊断                                                        */
/* ------------------------------------------------------------------ */
int s2500_clear_status0(s2500_dev_t *dev, uint32_t mask)
{
    return s2500_write_reg(dev, S2500_MMS_STD, S2500_REG_STATUS0, mask);
}

int s2500_read_status(s2500_dev_t *dev, s2500_status_t *st)
{
    uint32_t phy = 0, plca0 = 0, plca1 = 0, plca_st = 0, ext = 0, sqi = 0, sqip = 0, dg = 0;

    if (!dev || !st) return S2500_ERR_PARAM;
    memset(st, 0, sizeof(*st));

    if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_PHY_STATUS, &phy) != S2500_OK) return S2500_ERR_SPI;
    st->link_up = (phy & S2500_PHYSTS_LKST) ? 1 : 0;
    dev->link_up = st->link_up;

    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0, &plca0) != S2500_OK) return S2500_ERR_SPI;
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL1, &plca1) != S2500_OK) return S2500_ERR_SPI;
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCASTATUS, &plca_st) != S2500_OK) return S2500_ERR_SPI;
    st->plca_en = (plca0 & S2500_PLCA_EN) ? 1 : 0;
    st->plca_id = (uint16_t)(plca1 & S2500_PLCA_ID_MASK);
    st->plca_ncnt = (uint16_t)((plca1 & S2500_PLCA_NCNT_MASK) >> S2500_PLCA_NCNT_SHIFT);
    st->plca_pst = (plca_st & S2500_PLCA_PST) ? 1 : 0;

    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCAEXT, &ext) == S2500_OK) {
        st->plca_crps = (ext & S2500_PLCAEXT_CRPS) ? 1 : 0;
    }
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCADIAG, &dg) == S2500_OK) {
        st->plca_diag = (uint8_t)(dg & 0x7u);
    }
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_DCQ_SQI, &sqi) == S2500_OK) {
        st->sqi = (uint8_t)(sqi & S2500_SQI_MASK);
        st->sqi_valid = (sqi & S2500_SQI_UPD) ? 1 : 0;
    }
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_DCQ_SQIP, &sqip) == S2500_OK) {
        st->sqip = (uint8_t)(sqip & S2500_SQIP_MASK);
        st->sqip_valid = (sqip & S2500_SQIP_UPD) ? 1 : 0;
    }
    return S2500_OK;
}

int s2500_read_pcs_counters(s2500_dev_t *dev, uint32_t *collisions, uint32_t *rjab)
{
    uint32_t v = 0;
    if (collisions) {
        if (s2500_read_reg(dev, S2500_MMS_PCS, S2500_REG_PCSDIAG2, &v) != S2500_OK) return S2500_ERR_SPI;
        *collisions = v;
    }
    if (rjab) {
        if (s2500_read_reg(dev, S2500_MMS_PCS, S2500_REG_PCSDIAG1, &v) != S2500_OK) return S2500_ERR_SPI;
        *rjab = v;
    }
    return S2500_OK;
}

int s2500_read_counters(s2500_dev_t *dev, uint32_t out[35])
{
    int i;
    if (!dev || !out) return S2500_ERR_PARAM;
    for (i = 0; i < 35; i++) {
        if (s2500_read_reg(dev, S2500_MMS_MAC, (uint16_t)(S2500_MST_TX_OCTETS_L + i), &out[i]) != S2500_OK) {
            return S2500_ERR_SPI;
        }
    }
    return S2500_OK;
}

int s2500_clear_counters(s2500_dev_t *dev)
{
    uint32_t mc;
    if (s2500_read_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0, &mc) != S2500_OK) return S2500_ERR_SPI;
    /* 关闭再打开 RXEN/TXEN 会清零统计 */
    s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0, mc & ~(S2500_MC_TXEN | S2500_MC_RXEN));
    return s2500_write_reg(dev, S2500_MMS_MAC, S2500_REG_MCONTROL0, mc);
}

int s2500_read_sqi(s2500_dev_t *dev, uint8_t *sqi, uint8_t *sqip)
{
    uint32_t v = 0;
    if (sqi) {
        if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_DCQ_SQI, &v) != S2500_OK) return S2500_ERR_SPI;
        *sqi = (uint8_t)(v & S2500_SQI_MASK);
    }
    if (sqip) {
        if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_DCQ_SQIP, &v) != S2500_OK) return S2500_ERR_SPI;
        *sqip = (uint8_t)(v & S2500_SQIP_MASK);
    }
    return S2500_OK;
}

int s2500_plca_diag(s2500_dev_t *dev, uint8_t *diag)
{
    uint32_t v = 0;
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCADIAG, &v) != S2500_OK) return S2500_ERR_SPI;
    *diag = (uint8_t)(v & 0x7u);
    return S2500_OK;
}

int s2500_read_irq_status(s2500_dev_t *dev, uint32_t *irqs)
{
    uint32_t v = 0;
    if (s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_PHYIRQS, &v) != S2500_OK) return S2500_ERR_SPI;
    *irqs = v;
    return S2500_OK;
}

/* ------------------------------------------------------------------ */
/* PLCA                                                               */
/* ------------------------------------------------------------------ */
int s2500_plca_configure(s2500_dev_t *dev, uint8_t id, uint8_t ncnt,
                         uint8_t totmr, int enable)
{
    int rc;
    if (ncnt == 0) ncnt = 8;
    /* 先关闭并复位 PLCA RS, 避免运行中改 ID 造成误判 */
    rc = s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0, 0);
    if (rc != S2500_OK) return rc;
    rc = s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0, S2500_PLCA_RST);
    if (rc != S2500_OK) return rc;
    s2500_delay_us(dev, 50);
    rc = s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCATOTMR, totmr);
    if (rc != S2500_OK) return rc;
    rc = s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL1,
                         ((uint32_t)(ncnt & 0xFF) << S2500_PLCA_NCNT_SHIFT) |
                         (uint32_t)(id & 0xFF));
    if (rc != S2500_OK) return rc;
    rc = s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0,
                         enable ? S2500_PLCA_EN : 0u);
    dev->plca_id = id;
    dev->plca_ncnt = ncnt;
    return rc;
}

int s2500_plca_set_id(s2500_dev_t *dev, uint8_t id)
{
    return s2500_update_bits(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL1,
                             S2500_PLCA_ID_MASK, id);
}

int s2500_plca_coordinator(s2500_dev_t *dev, int on)
{
    return s2500_plca_set_id(dev, on ? 0 : (dev->plca_id ? dev->plca_id : 1));
}

int s2500_plca_redundancy(s2500_dev_t *dev, int on)
{
    return s2500_update_bits(dev, S2500_MMS_PLCA, S2500_REG_PLCAEXT,
                             0, on ? S2500_PLCAEXT_CREN : 0);
}

int s2500_plca_precedence(s2500_dev_t *dev, int on)
{
    return s2500_update_bits(dev, S2500_MMS_PLCA, S2500_REG_PLCAEXT,
                             S2500_PLCAEXT_PRECEDENCE, on ? S2500_PLCAEXT_PRECEDENCE : 0);
}

int s2500_plca_burst(s2500_dev_t *dev, uint8_t maxbc, uint8_t btmr)
{
    return s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCABURST,
                           ((uint32_t)maxbc << 8) | btmr);
}

int s2500_plca_status(s2500_dev_t *dev, uint8_t *pst, uint8_t *crps)
{
    uint32_t v = 0, e = 0;
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCASTATUS, &v) != S2500_OK) return S2500_ERR_SPI;
    if (pst) *pst = (v & S2500_PLCA_PST) ? 1 : 0;
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCAEXT, &e) != S2500_OK) return S2500_ERR_SPI;
    if (crps) *crps = (e & S2500_PLCAEXT_CRPS) ? 1 : 0;
    return S2500_OK;
}

/* ------------------------------------------------------------------ */
/* HDD                                                                */
/* ------------------------------------------------------------------ */
int s2500_hdd_run(s2500_dev_t *dev, s2500_hdd_result_t *res, uint32_t timeout_ms)
{
    uint32_t v = 0, start;
    if (!dev || !res) return S2500_ERR_PARAM;
    memset(res, 0, sizeof(*res));

    /* 使能 HDD 模式 */
    if (s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_HDD,
                        S2500_HDD_CTRL | S2500_HDD_START) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    start = s2500_millis(dev);
    for (;;) {
        if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_HDD, &v) != S2500_OK) {
            return S2500_ERR_SPI;
        }
        if (v & S2500_HDD_VALID) break;
        if (s2500_timeout(dev, start, timeout_ms ? timeout_ms : 2000)) {
            s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_HDD, 0);
            return S2500_ERR_TIMEOUT;
        }
        s2500_delay_us(dev, 1000);
    }
    res->valid = 1;
    res->ready = (v & S2500_HDD_READY) ? 1 : 0;
    res->short_open = (uint8_t)(v & S2500_HDD_SHORT_OPEN_MASK);
    res->one_wire = (v & S2500_HDD_1W_ST) ? 1 : 0;

    /* 退出 HDD 模式 */
    s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_HDD, 0);
    return S2500_OK;
}

/* ------------------------------------------------------------------ */
/* TC14 拓扑发现                                                      */
/* ------------------------------------------------------------------ */
int s2500_td_run(s2500_dev_t *dev, const s2500_td_result_t *req,
                 s2500_td_result_t *res, uint32_t timeout_ms)
{
    uint32_t v, start;
    uint8_t dur;

    if (!dev || !req || !res) return S2500_ERR_PARAM;
    memset(res, 0, sizeof(*res));
    dur = (uint8_t)((req->dm_dur_ms >= 1 && req->dm_dur_ms <= 16) ? (req->dm_dur_ms - 1) : 7);

    v = S2500_TDCTRL_EN | ((uint32_t)(dur & 0xF) << S2500_TDCTRL_DM_DUR_SHIFT);
    if (req->auto_mode) {
        if (req->dist_mr) v |= S2500_TDCTRL_REFN;   /* 复用字段表示参考节点 */
        v |= S2500_TDCTRL_AUTO_START;
    } else if (req->delay_only) {
        v |= S2500_TDCTRL_DLYM_START;
    } else {
        v |= S2500_TDCTRL_DM_START;
    }
    if (s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_CTRL, v) != S2500_OK) {
        return S2500_ERR_SPI;
    }

    start = s2500_millis(dev);
    for (;;) {
        uint32_t st = 0;
        if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_STAT, &st) != S2500_OK) {
            return S2500_ERR_SPI;
        }
        /* 自动模式必须等距离测量结束: DLYM_DONE 只表示内部延时阶段完成,
         * 在这里提前 break 会让 dist_mr 恒为 0 (2026-11-20 修复)。
         * 仅延时模式 (dly) 才等 DLYM_DONE。 */
        {
            uint32_t done_mask;
            if (req->auto_mode) {
                done_mask = S2500_TDSTAT_DM_DONE | S2500_TDSTAT_DM_ERR |
                            S2500_TDSTAT_DLYM_ERR;
            } else if (req->delay_only) {
                done_mask = S2500_TDSTAT_DLYM_DONE | S2500_TDSTAT_DLYM_ERR;
            } else {
                done_mask = S2500_TDSTAT_DM_DONE | S2500_TDSTAT_DM_ERR;
            }
            if (st & done_mask) {
                res->auto_err = (st & S2500_TDSTAT_AUTO_ERR) ? 1 : 0;
                res->dm_err = (st & S2500_TDSTAT_DM_ERR) ? 1 : 0;
                res->dlym_err = (st & S2500_TDSTAT_DLYM_ERR) ? 1 : 0;
                break;
            }
        }
        if (s2500_timeout(dev, start, timeout_ms ? timeout_ms : 5000)) {
            s2500_td_abort(dev);
            return S2500_ERR_TIMEOUT;
        }
        s2500_delay_us(dev, 1000);
    }

    /* DM_DONE 在本芯片上可能早于距离阶段真正结束置位 (实测 dist_mr 只有几百),
     * 再等 2*DM_DUR + 10 ms 让距离阶段跑完后再读结果。 */
    s2500_delay_us(dev, ((uint32_t)(dur + 1u) * 2u + 10u) * 1000u);

    {
        uint32_t lo = 0, hi = 0;
        s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_DIST_LOW, &lo);
        s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_DIST_HIGH, &hi);
        res->dist_mr = (hi << 16) | (lo & 0xFFFF);
        s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_DLY_LOW, &lo);
        s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_DLY_HIGH, &hi);
        res->dly_mr = (hi << 16) | (lo & 0xFFFF);
        s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_MNDLY_LOW, &lo);
        s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_MNDLY_HIGH, &hi);
        res->mndly_mr = (hi << 16) | (lo & 0xFFFF);
        s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_MNDLY_DUR, &v);
        res->mndly_dur_ms = (uint8_t)((v >> 12) + 1);
    }
    res->dm_dur_ms = (uint8_t)(dur + 1);
    /* 退出拓扑发现模式 */
    s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_CTRL, 0);
    return S2500_OK;
}

int s2500_td_abort(s2500_dev_t *dev)
{
    return s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_TD_CTRL, 0);
}

/* ------------------------------------------------------------------ */
/* TC6 时间戳                                                         */
/* ------------------------------------------------------------------ */
int s2500_time_read(s2500_dev_t *dev, uint32_t *sec, uint32_t *nsec)
{
    uint32_t s = 0, n = 0;
    if (s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_PTPSEC, &s) != S2500_OK) return S2500_ERR_SPI;
    if (s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_PTPNSEC, &n) != S2500_OK) return S2500_ERR_SPI;
    if (sec) *sec = s;
    if (nsec) *nsec = n;
    return S2500_OK;
}

int s2500_time_set(s2500_dev_t *dev, uint32_t sec, uint32_t nsec)
{
    if (s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_SETSEC, sec) != S2500_OK) return S2500_ERR_SPI;
    /* SETT 位 (SETNSEC[31]) 与 ns 同时写入 */
    return s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_SETNSEC,
                           (nsec & 0x3FFFFFFFu) | (1u << 31));
}

int s2500_ttsc_read(s2500_dev_t *dev, uint8_t idx, uint64_t *ts64)
{
    uint32_t h = 0, l = 0;
    uint16_t ah = (uint16_t)(S2500_REG_TTSCAH + idx * 2u);
    uint16_t al = (uint16_t)(S2500_REG_TTSCAL + idx * 2u);
    if (idx > 2) return S2500_ERR_PARAM;
    if (s2500_read_reg(dev, S2500_MMS_STD, ah, &h) != S2500_OK) return S2500_ERR_SPI;
    if (s2500_read_reg(dev, S2500_MMS_STD, al, &l) != S2500_OK) return S2500_ERR_SPI;
    if (ts64) *ts64 = ((uint64_t)h << 32) | l;
    return S2500_OK;
}

int s2500_capture_configure(s2500_dev_t *dev, uint8_t edge, int continuous)
{
    uint32_t v;
    if (s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_CAPCTL, &v) != S2500_OK) return S2500_ERR_SPI;
    v &= ~0x7u;
    v |= (uint32_t)(edge & 0x3u);
    if (continuous) v |= (1u << 2);
    return s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_CAPCTL, v);
}

int s2500_capture_read(s2500_dev_t *dev, uint32_t *sec, uint32_t *nsec)
{
    uint32_t s = 0, n = 0;
    if (s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_CAPSEC, &s) != S2500_OK) return S2500_ERR_SPI;
    if (s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_CAPNSEC, &n) != S2500_OK) return S2500_ERR_SPI;
    if (sec) *sec = s;
    if (nsec) *nsec = n;
    return S2500_OK;
}

int s2500_compare_oneshot(s2500_dev_t *dev, uint32_t sec, uint32_t nsec, uint8_t dio_pol)
{
    if (s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_CMPSEC, sec) != S2500_OK) return S2500_ERR_SPI;
    if (s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_CMPNSEC, nsec & 0x3FFFFFFFu) != S2500_OK) return S2500_ERR_SPI;
    /* 单次事件, DIO0 输出, 极性由 VAL 位控制 */
    s2500_dio_config(dev, 0, S2500_DIOFN_CMP, dio_pol, 1, 0, 1);
    return s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_CMPCTL, 0x1u);
}

int s2500_compare_clock(s2500_dev_t *dev, uint32_t period_ns, uint8_t dio_pol)
{
    if (period_ns < 48) period_ns = 48;
    if (s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_CMPPER, period_ns) != S2500_OK) return S2500_ERR_SPI;
    s2500_dio_config(dev, 0, S2500_DIOFN_CMP, dio_pol, 1, 0, 1);
    return s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_CMPCTL, 0x0u);
}

int s2500_compare_stop(s2500_dev_t *dev)
{
    return s2500_dio_config(dev, 0, S2500_DIOFN_DISABLE, 0, 0, 0, 1);
}

/* ------------------------------------------------------------------ */
/* TC10 睡眠 / 唤醒                                                   */
/* ------------------------------------------------------------------ */
int s2500_sleep(s2500_dev_t *dev)
{
    /* LPREQ 置 1 后进入 Go-To-Sleep, 2ms 后进入 Sleep */
    return s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_WS_CTRL0, S2500_WSCTRL0_LPREQ);
}

int s2500_send_wake_tone(s2500_dev_t *dev)
{
    return s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_WS_CTRL0, S2500_WSCTRL0_LPEXIT);
}

int s2500_wake_config(s2500_dev_t *dev, uint8_t wfwd, uint8_t wdeb,
                      uint8_t lwd, uint8_t rwd, uint8_t lwth)
{
    uint32_t v = 0;
    if (wfwd) v |= S2500_WSCTRL1_WFWD;
    if (wdeb) v |= S2500_WSCTRL1_WDEB;
    if (lwd)  v |= S2500_WSCTRL1_LWD;
    if (rwd)  v |= S2500_WSCTRL1_RWD;
    if (lwth) v |= S2500_WSCTRL1_LWTH;
    return s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_WS_CTRL1, v);
}

int s2500_wake_pin_output(s2500_dev_t *dev, int on)
{
    return s2500_update_bits(dev, S2500_MMS_PLCA, S2500_REG_WS_CTRL1,
                             0, on ? S2500_WSCTRL1_WOUT : 0);
}

int s2500_read_reset_reason(s2500_dev_t *dev, uint32_t *ws_status1)
{
    uint32_t v = 0;
    if (s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_WS_STATUS1, &v) != S2500_OK) return S2500_ERR_SPI;
    if (ws_status1) *ws_status1 = v;
    return S2500_OK;
}

/* ------------------------------------------------------------------ */
/* DIO / LED / 模拟                                                   */
/* ------------------------------------------------------------------ */
int s2500_dio_config(s2500_dev_t *dev, uint8_t dio, uint8_t fn, uint8_t val,
                     uint8_t pull_en, uint8_t pull_down, uint8_t slew_fast)
{
    uint32_t v = 0;
    if (dio > 1) return S2500_ERR_PARAM;
    v |= S2500_DIO_SLW(dio) & (slew_fast ? 0xFFFFFFFFu : 0u);
    v |= pull_en ? S2500_DIO_PEN(dio) : 0u;
    v |= pull_down ? S2500_DIO_PUD(dio) : 0u;
    v |= S2500_DIO_FN(dio, fn);
    v |= S2500_DIO_VAL(dio, val);
    return s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_DIOCFG0, v);
}

int s2500_led_config(s2500_dev_t *dev, uint8_t act_fn, uint8_t link_fn)
{
    uint32_t v = 0;
    v |= S2500_DIO_SLW(0) | S2500_DIO_FN(0, act_fn) | S2500_DIO_VAL(0, 1);
    v |= S2500_DIO_SLW(1) | S2500_DIO_FN(1, link_fn) | S2500_DIO_VAL(1, 1);
    return s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_DIOCFG0, v);
}

int s2500_analog_config(s2500_dev_t *dev, uint8_t tx_amp, uint8_t edth,
                        uint8_t edthcd, uint8_t tx_slw, uint8_t clko_en,
                        uint8_t tx_cmc)
{
    uint32_t v;
    if (s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_TWEAKS2, &v) != S2500_OK) return S2500_ERR_SPI;
    v &= ~((3u << S2500_TWEAKS2_TXAMP_SHIFT) | (15u << S2500_TWEAKS2_EDTHCD_SHIFT) |
           (15u << S2500_TWEAKS2_EDTH_SHIFT) | (3u << S2500_TWEAKS2_TXSLW_SHIFT) |
           (3u << S2500_TWEAKS2_TXCMC_SHIFT) | S2500_TWEAKS2_CLKO_ENA);
    v |= ((uint32_t)(tx_amp & 3u) << S2500_TWEAKS2_TXAMP_SHIFT);
    v |= ((uint32_t)(edthcd & 15u) << S2500_TWEAKS2_EDTHCD_SHIFT);
    v |= ((uint32_t)(edth & 15u) << S2500_TWEAKS2_EDTH_SHIFT);
    v |= ((uint32_t)(tx_slw & 3u) << S2500_TWEAKS2_TXSLW_SHIFT);
    v |= ((uint32_t)(tx_cmc & 3u) << S2500_TWEAKS2_TXCMC_SHIFT);
    if (clko_en) v |= S2500_TWEAKS2_CLKO_ENA;
    return s2500_write_reg(dev, S2500_MMS_VENDOR, S2500_REG_TWEAKS2, v);
}

int s2500_loopback(s2500_dev_t *dev, uint8_t mode)
{
    int rc;
    /* 先清两种环回 */
    rc = s2500_update_bits(dev, S2500_MMS_STD, S2500_REG_PHY_CTRL, S2500_PHYCTRL_LOOPBACK, 0);
    if (rc != S2500_OK) return rc;
    rc = s2500_update_bits(dev, S2500_MMS_PCS, S2500_REG_PCSCTL, S2500_PCSCTL_LOOP, 0);
    if (rc != S2500_OK) return rc;
    rc = s2500_update_bits(dev, S2500_MMS_PMA, S2500_REG_PMACTL, S2500_PMACTL_LOOP, 0);
    if (rc != S2500_OK) return rc;

    if (mode == 1) {
        return s2500_update_bits(dev, S2500_MMS_PCS, S2500_REG_PCSCTL, 0, S2500_PCSCTL_LOOP);
    }
    if (mode == 2) {
        return s2500_update_bits(dev, S2500_MMS_STD, S2500_REG_PHY_CTRL, 0, S2500_PHYCTRL_LOOPBACK);
    }
    return S2500_OK;
}

/* ------------------------------------------------------------------ */
/* 调试: 读一个数据块并解码 footer (不经过重组逻辑)                   */
/* ------------------------------------------------------------------ */
int s2500_raw_block_dump(s2500_dev_t *dev, int verbose)
{
    uint32_t ftr;
    if (!dev) return S2500_ERR_PARAM;
    memset(dev->txb, 0, S2500_DATA_XFER_LEN);
    memset(dev->rxb, 0, S2500_DATA_XFER_LEN);
    build_data_header(dev, 0, 0, 0, 0, 0, 0);
    if (s2500_xfer(dev, S2500_DATA_XFER_LEN) != S2500_OK) return S2500_ERR_SPI;
    ftr = rd_be32(&dev->rxb[S2500_CHUNK_SIZE]);
    if (verbose && dev->hal && dev->hal->log) {
        dev->hal->log(dev->hal->ctx,
            "ftr=0x%08lX P=%u EXST=%u HDRB=%u SYNC=%u RBA=%u DV=%u SV=%u SWO=%u FD=%u EV=%u EBO=%u TXC=%u\r\n"
            "  head: %02X %02X %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            (unsigned long)ftr,
            (unsigned)(ftr & 1), (unsigned)((ftr >> 31) & 1), (unsigned)((ftr >> 30) & 1),
            (unsigned)((ftr >> 29) & 1), (unsigned)((ftr >> 24) & 0x1F),
            (unsigned)((ftr >> 21) & 1), (unsigned)((ftr >> 20) & 1),
            (unsigned)((ftr >> 16) & 0xF), (unsigned)((ftr >> 15) & 1),
            (unsigned)((ftr >> 14) & 1), (unsigned)((ftr >> 8) & 0x3F),
            (unsigned)((ftr >> 1) & 0x1F),
            dev->rxb[0], dev->rxb[1], dev->rxb[2], dev->rxb[3],
            dev->rxb[4], dev->rxb[5], dev->rxb[6], dev->rxb[7],
            dev->rxb[8], dev->rxb[9], dev->rxb[10], dev->rxb[11],
            dev->rxb[12], dev->rxb[13], dev->rxb[14], dev->rxb[15]);
    }
    return S2500_OK;
}

/* ------------------------------------------------------------------ */
/* 自检: 环回收发 (不依赖外部总线)                                    */
/* ------------------------------------------------------------------ */
int s2500_loopback_test_ex(s2500_dev_t *dev, uint8_t mode, uint32_t timeout_ms,
                           s2500_lb_result_t *res)
{
    uint8_t tx[128], rx[1600];
    uint16_t rxlen = 0;
    uint32_t start, plca0 = 0;
    int i, rc = S2500_ERR_TIMEOUT;

    if (res) {
        memset(res, 0, sizeof(*res));
        res->rc = S2500_ERR_TIMEOUT;
    }

    if (!dev) return S2500_ERR_PARAM;

    /* 组一个最小测试帧: 目的=本机, 源=本机, 类型 0x88B5 */
    memset(tx, 0, sizeof(tx));
    memcpy(&tx[0], dev->mac, 6);
    memcpy(&tx[6], dev->mac, 6);
    tx[12] = 0x88;
    tx[13] = 0xB5;
    for (i = 14; i < 100; i++) tx[i] = (uint8_t)(i * 3 + mode);

    /* 环回期间关闭 PLCA, 避免跟随者没有 BEACON 时无法发送 */
    s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0, &plca0);
    s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0, 0);

    if (s2500_loopback(dev, mode) != S2500_OK) {
        s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0, plca0);
        return S2500_ERR_SPI;
    }
    s2500_start(dev);
    s2500_delay_us(dev, 1000);

    if (s2500_send(dev, tx, 100, 0) != S2500_OK) {
        s2500_loopback(dev, 0);
        s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0, plca0);
        return S2500_ERR_TX;
    }

    start = s2500_millis(dev);
    while (!s2500_timeout(dev, start, timeout_ms ? timeout_ms : 100)) {
        uint16_t got = 0;
        if (s2500_recv(dev, rx, &got, NULL) == S2500_OK) {
            rxlen = got;
            if (res) {
                res->rxlen = got;
                memcpy(res->head, rx, got < sizeof(res->head) ? got : sizeof(res->head));
                res->rx_frames++;
            }
            if (rxlen == 100 && memcmp(rx, tx, 100) == 0) {
                rc = S2500_OK;
            } else {
                rc = S2500_ERR_PROTOCOL;
            }
            break;
        }
        s2500_delay_us(dev, 200);
    }
    s2500_loopback(dev, 0);
    s2500_write_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0, plca0);
    if (res) res->rc = rc;
    return rc;
}

int s2500_loopback_test(s2500_dev_t *dev, uint8_t mode, uint32_t timeout_ms)
{
    return s2500_loopback_test_ex(dev, mode, timeout_ms, NULL);
}

/* ------------------------------------------------------------------ */
/* 信息输出                                                          */
/* ------------------------------------------------------------------ */
/* 打印辅助: 平台未提供 log 时静默 */
#define S2500_PRINTF(dev, ...) do {     if ((dev)->hal && (dev)->hal->log) { (dev)->hal->log((dev)->hal->ctx, __VA_ARGS__); } } while (0)

void s2500_dump_info(s2500_dev_t *dev)
{
    uint32_t idver = 0, phyid = 0, cap = 0, onip = 0, onic = 0, health = 0, ws1 = 0;
    uint32_t phystat = 0, plca0 = 0, plca1 = 0, pstat = 0, ext = 0;
    s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_IDVER, &idver);
    s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_PHYID, &phyid);
    s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_STDCAP, &cap);
    s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_ONIPVER, &onip);
    s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_ONICVER, &onic);
    s2500_read_reg(dev, S2500_MMS_VENDOR, S2500_REG_NVM_HEALTH, &health);
    s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_PHY_STATUS, &phystat);
    s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL0, &plca0);
    s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCACTL1, &plca1);
    s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCASTATUS, &pstat);
    s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_PLCAEXT, &ext);
    s2500_read_reg(dev, S2500_MMS_PLCA, S2500_REG_WS_STATUS1, &ws1);

    S2500_PRINTF(dev, "S2500 IDVER=0x%08X (v%u.%u) PHYID=0x%08X STDCAP=0x%08X\r\n",
                 idver, (idver >> 4) & 0xF, idver & 0xF, phyid, cap);
    S2500_PRINTF(dev, "  ONIPVER=0x%04X ONICVER=0x%08X NVM_HEALTH=0x%08X\r\n",
                 onip, onic, health);
    S2500_PRINTF(dev, "  MAC=%02X:%02X:%02X:%02X:%02X:%02X  PHYSTS=0x%04X link=%u\r\n",
                 dev->mac[0], dev->mac[1], dev->mac[2], dev->mac[3], dev->mac[4], dev->mac[5],
                 phystat, (phystat & S2500_PHYSTS_LKST) ? 1u : 0u);
    S2500_PRINTF(dev, "  PLCA en=%u id=%u ncnt=%u pst=%u crps=%u prec=%u CREN=%u\r\n",
                 (plca0 & S2500_PLCA_EN) ? 1u : 0u,
                 (unsigned)(plca1 & 0xFFu),
                 (unsigned)((plca1 >> 8) & 0xFFu),
                 (pstat & S2500_PLCA_PST) ? 1u : 0u,
                 (ext & S2500_PLCAEXT_CRPS) ? 1u : 0u,
                 (ext & S2500_PLCAEXT_PRECEDENCE) ? 1u : 0u,
                 (ext & S2500_PLCAEXT_CREN) ? 1u : 0u);
    S2500_PRINTF(dev, "  WS_STATUS1=0x%04X (POR_VCC=%u POR_VIO=%u RST_IN=%u TSD=%u WUT=%u WPIN=%u)\r\n",
                 ws1, (ws1 >> 15) & 1u, (ws1 >> 14) & 1u, (ws1 >> 12) & 1u,
                 (ws1 >> 9) & 1u, (ws1 >> 8) & 1u, (ws1 >> 7) & 1u);
}
