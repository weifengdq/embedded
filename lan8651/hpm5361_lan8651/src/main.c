#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_config.h"
#include "board.h"
#include "ethernetif_lan8651.h"
#include "hpm_dma_mgr.h"
#include "hpm_gpio_drv.h"
#include "hpm_iomux.h"
#include "hpm_spi.h"
#include "lan8651.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "netif/ethernet.h"

ATTR_PLACE_AT_NONCACHEABLE static lan8651_t s_lan8651;
static struct netif s_netif;
static struct udp_pcb *s_udp_pcb;
static void *s_lwiperf_handle;
static volatile uint32_t s_ms_tick;

static void app_timer_tick(void)
{
    s_ms_tick++;
}

static void app_spi_pad_tune(void)
{
    uint32_t slow_pad_ctl = IOC_PAD_PAD_CTL_SR_SET(0) | IOC_PAD_PAD_CTL_SPD_SET(0);

    HPM_IOC->PAD[IOC_PAD_PA26].PAD_CTL = slow_pad_ctl;
    HPM_IOC->PAD[IOC_PAD_PA27].PAD_CTL = slow_pad_ctl;
    HPM_IOC->PAD[IOC_PAD_PA28].PAD_CTL = slow_pad_ctl;
    HPM_IOC->PAD[IOC_PAD_PA29].PAD_CTL = slow_pad_ctl;
}

static void app_gpio_init(void)
{
    HPM_IOC->PAD[APP_LAN8651_RESET_PAD].FUNC_CTL = APP_LAN8651_RESET_FUNC;
    HPM_IOC->PAD[APP_LAN8651_IRQ_PAD].FUNC_CTL = APP_LAN8651_IRQ_FUNC;

    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_GET_PORT_INDEX(APP_LAN8651_RESET_PAD),
        GPIO_GET_PIN_INDEX(APP_LAN8651_RESET_PAD), 1U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_GET_PORT_INDEX(APP_LAN8651_IRQ_PAD),
        GPIO_GET_PIN_INDEX(APP_LAN8651_IRQ_PAD));
}

static hpm_stat_t app_spi_init(void)
{
    spi_initialize_config_t init_config = { 0 };
    hpm_stat_t stat;

    board_init_spi_clock(BOARD_APP_SPI_BASE);
    board_init_spi_pins_with_gpio_as_cs(BOARD_APP_SPI_BASE);
    app_spi_pad_tune();
    dma_mgr_init();

    hpm_spi_get_default_init_config(&init_config);
    init_config.mode = spi_master_mode;
    init_config.io_mode = spi_single_io_mode;
    init_config.clk_polarity = spi_sclk_low_idle;
    init_config.clk_phase = spi_sclk_sampling_odd_clk_edges;
    init_config.direction = spi_msb_first;
    init_config.data_len = 8;
    init_config.data_merge = false;

    stat = hpm_spi_initialize(BOARD_APP_SPI_BASE, &init_config);
    if (stat != status_success) {
        return stat;
    }

    stat = hpm_spi_set_sclk_frequency(BOARD_APP_SPI_BASE, APP_LAN8651_SPI_INIT_FREQ);
    if (stat != status_success) {
        return stat;
    }

    stat = hpm_spi_dma_mgr_install_callback(BOARD_APP_SPI_BASE, lan8651_spi_tx_dma_complete, lan8651_spi_rx_dma_complete);
    if (stat != status_success) {
        return stat;
    }

    return status_success;
}

static void app_udp_echo_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
    const ip_addr_t *addr, u16_t port)
{
    (void)arg;

    if (p == NULL) {
        return;
    }

    udp_sendto(pcb, p, addr, port);
    pbuf_free(p);
}

static bool app_udp_echo_init(void)
{
    s_udp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (s_udp_pcb == NULL) {
        return false;
    }
    if (udp_bind(s_udp_pcb, IP_ADDR_ANY, APP_UDP_ECHO_PORT) != ERR_OK) {
        udp_remove(s_udp_pcb);
        s_udp_pcb = NULL;
        return false;
    }
    udp_recv(s_udp_pcb, app_udp_echo_recv, NULL);
    return true;
}

static void app_lwiperf_report(void *arg, enum lwiperf_report_type report_type,
    const ip_addr_t *local_addr, u16_t local_port, const ip_addr_t *remote_addr, u16_t remote_port,
    u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
    (void)arg;
    printf("lwiperf: type=%d local=%s:%u remote=%s:%u bytes=%lu time=%lums bw=%lukbit/s\n",
        (int)report_type,
        ipaddr_ntoa(local_addr),
        local_port,
        ipaddr_ntoa(remote_addr),
        remote_port,
        (unsigned long)bytes_transferred,
        (unsigned long)ms_duration,
        (unsigned long)bandwidth_kbitpsec);
}

static bool app_lwiperf_init(void)
{
    ip_addr_t local_addr;

    ip_addr_copy(local_addr, *netif_ip_addr4(&s_netif));
    s_lwiperf_handle = lwiperf_start_tcp_server(&local_addr, APP_IPERF_PORT, app_lwiperf_report, NULL);
    return s_lwiperf_handle != NULL;
}

static void app_netif_status_cb(struct netif *netif)
{
    char ip_buf[16];
    char mask_buf[16];
    char gw_buf[16];

    printf("netif: up=%d ip=%s mask=%s gw=%s\n",
        netif_is_up(netif) ? 1 : 0,
        ip4addr_ntoa_r(netif_ip4_addr(netif), ip_buf, sizeof(ip_buf)),
        ip4addr_ntoa_r(netif_ip4_netmask(netif), mask_buf, sizeof(mask_buf)),
        ip4addr_ntoa_r(netif_ip4_gw(netif), gw_buf, sizeof(gw_buf)));
}

static void app_netif_link_cb(struct netif *netif)
{
    printf("netif: link=%s\n", netif_is_link_up(netif) ? "up" : "down");
}

static bool app_lwip_init(void)
{
    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
    err_t err;

    IP4_ADDR(&ipaddr, APP_IP_ADDR0, APP_IP_ADDR1, APP_IP_ADDR2, APP_IP_ADDR3);
    IP4_ADDR(&netmask, APP_NETMASK_ADDR0, APP_NETMASK_ADDR1, APP_NETMASK_ADDR2, APP_NETMASK_ADDR3);
    IP4_ADDR(&gateway, APP_GW_ADDR0, APP_GW_ADDR1, APP_GW_ADDR2, APP_GW_ADDR3);

    lwip_init();

    if (netif_add(&s_netif, &ipaddr, &netmask, &gateway, &s_lan8651, lan8651_ethernetif_init, ethernet_input) == NULL) {
        return false;
    }

    netif_set_default(&s_netif);
    netif_set_status_callback(&s_netif, app_netif_status_cb);
    netif_set_link_callback(&s_netif, app_netif_link_cb);
    netif_set_up(&s_netif);
    netif_set_link_up(&s_netif);

    err = etharp_request(&s_netif, netif_ip4_addr(&s_netif));
    printf("netif: gratuitous_arp=%s\n", err == ERR_OK ? "sent" : "failed");

    return true;
}

u32_t sys_now(void)
{
    return s_ms_tick;
}

int main(void)
{
    static const uint8_t mac_addr[6] = {
        APP_LAN8651_MAC0,
        APP_LAN8651_MAC1,
        APP_LAN8651_MAC2,
        APP_LAN8651_MAC3,
        APP_LAN8651_MAC4,
        APP_LAN8651_MAC5,
    };
    uint32_t last_log_ms = 0;
    uint32_t last_led_ms = 0;

    board_init();
    board_init_led_pins();
    board_init_console();
    app_gpio_init();
    board_timer_create(APP_SYS_TICK_PERIOD_MS, app_timer_tick);

    printf("%s\n", APP_PROJECT_NAME);
    printf("LAN8651 SPI1 DMA bring-up\n");

    if (app_spi_init() != status_success) {
        printf("SPI init failed\n");
        while (1) {
        }
    }

    if (lan8651_init(&s_lan8651, BOARD_APP_SPI_BASE, HPM_GPIO0, BOARD_SPI_CS_PIN,
            APP_LAN8651_RESET_PAD, APP_LAN8651_IRQ_PAD, mac_addr) != kLan8651Status_Ok) {
        printf("LAN8651 init failed\n");
        while (1) {
        }
    }

    if (lan8651_set_spi_clock(&s_lan8651, APP_LAN8651_SPI_RUNTIME_FREQ) != kLan8651Status_Ok) {
        printf("LAN8651 runtime SPI clock switch failed\n");
        while (1) {
        }
    }

    if (lan8651_start(&s_lan8651) != kLan8651Status_Ok) {
        printf("LAN8651 start failed\n");
        while (1) {
        }
    }

    if (!app_lwip_init()) {
        printf("lwIP init failed\n");
        while (1) {
        }
    }

    if (!app_udp_echo_init()) {
        printf("UDP echo init failed\n");
        while (1) {
        }
    }

    if (!app_lwiperf_init()) {
        printf("lwiperf init failed\n");
        while (1) {
        }
    }

    printf("IP ready: %u.%u.%u.%u udp=%u iperf=%u plca_node=%u\n",
        APP_IP_ADDR0, APP_IP_ADDR1, APP_IP_ADDR2, APP_IP_ADDR3,
        APP_UDP_ECHO_PORT, APP_IPERF_PORT, APP_LAN8651_PLCA_NODE_ID);

    while (1) {
        lan8651_ethernetif_input(&s_netif, APP_LAN8651_RX_POLL_BUDGET);
        sys_check_timeouts();

        if ((s_ms_tick - last_led_ms) >= APP_STATUS_LED_PERIOD_MS) {
            board_led_toggle();
            last_led_ms = s_ms_tick;
        }

        if (APP_LAN8651_ENABLE_DIAG_LOG != 0U && (s_ms_tick - last_log_ms) >= APP_LAN8651_DIAG_LOG_PERIOD_MS) {
            uint32_t oa_bufsts = 0;
            uint32_t plca_sts = 0;
            uint32_t bmcr = 0;
            uint32_t bmsr = 0;
            uint32_t phy_id1 = 0;
            uint32_t phy_id2 = 0;
            uint32_t mac_ncr = 0;
            uint32_t mac_ncfgr = 0;
            uint32_t mac_nsr = 0;
            uint32_t mac_tsr = 0;
            uint32_t mac_rsr = 0;

            last_log_ms = s_ms_tick;
            if (lan8651_read_reg(&s_lan8651, LAN8651_OA_BUFSTS, &oa_bufsts) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_PLCA_STS, &plca_sts) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_PHY_BMCR, &bmcr) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_PHY_BMSR, &bmsr) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_PHY_ID1, &phy_id1) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_PHY_ID2, &phy_id2) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_MAC_NCR, &mac_ncr) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_MAC_NCFGR, &mac_ncfgr) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_MAC_NSR, &mac_nsr) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_MAC_TSR, &mac_tsr) == kLan8651Status_Ok &&
                lan8651_read_reg(&s_lan8651, LAN8651_MAC_RSR, &mac_rsr) == kLan8651Status_Ok) {
                printf("diag: tick=%lu irq=%d sync=%lu pst=%lu rba=%lu txc=%lu bmsr=0x%04lX link=%lu an_done=%lu rflt=%lu id=%04lX:%04lX ncr=0x%08lX ncfgr=0x%08lX nsr=0x%02lX tsr=0x%03lX rsr=0x%02lX rec=%lu\n",
                    (unsigned long)s_ms_tick,
                    lan8651_irq_asserted(&s_lan8651) ? 1 : 0,
                    (unsigned long)((lan8651_link_up(&s_lan8651) ? 1U : 0U)),
                    (unsigned long)((plca_sts & LAN8651_PLCA_STS_PST) ? 1U : 0U),
                    (unsigned long)(oa_bufsts & LAN8651_OA_BUFSTS_RBA_MASK),
                    (unsigned long)((oa_bufsts & LAN8651_OA_BUFSTS_TXC_MASK) >> LAN8651_OA_BUFSTS_TXC_SHIFT),
                    (unsigned long)(bmsr & 0xFFFFU),
                    (unsigned long)((bmsr & LAN8651_PHY_BMSR_LINK_STATUS) ? 1U : 0U),
                    (unsigned long)((bmsr & LAN8651_PHY_BMSR_AN_COMPLETE) ? 1U : 0U),
                    (unsigned long)((bmsr & LAN8651_PHY_BMSR_REMOTE_FAULT) ? 1U : 0U),
                    (unsigned long)(phy_id1 & 0xFFFFU),
                    (unsigned long)(phy_id2 & 0xFFFFU),
                    (unsigned long)mac_ncr,
                    (unsigned long)mac_ncfgr,
                    (unsigned long)(mac_nsr & 0xFFU),
                    (unsigned long)(mac_tsr & 0x1FFU),
                    (unsigned long)(mac_rsr & 0x03U),
                    (unsigned long)((mac_rsr & 0x02U) ? 1U : 0U));
            } else {
                printf("tick=%lu irq=%d diag_read_failed\n",
                    (unsigned long)s_ms_tick,
                    lan8651_irq_asserted(&s_lan8651) ? 1 : 0);
            }
        }
    }
}