#include "app_network.h"

#include <stdarg.h>
#include <stdio.h>

#include "lwip/apps/lwiperf.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#define APP_UDP_ECHO_PORT 10U
#define APP_IPERF_TCP_PORT 5010U

static UART_HandleTypeDef *s_log_uart;
static struct udp_pcb *s_udp_echo_pcb;
static void *s_lwiperf_session;

static void App_UdpEchoRecv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
    const ip_addr_t *addr, u16_t port);
static void App_LwiperfReport(void *arg, enum lwiperf_report_type report_type,
    const ip_addr_t *local_addr, u16_t local_port,
    const ip_addr_t *remote_addr, u16_t remote_port,
    u32_t bytes_transferred, u32_t ms_duration,
    u32_t bandwidth_kbitpsec);
static void App_UdpEchoInit(void);
static void App_LwiperfInit(void);

static const char *App_IpAddrToString(const ip_addr_t *addr, char *buffer,
    size_t bufferSize)
{
  if (ipaddr_ntoa_r(addr, buffer, (int) bufferSize) == NULL) {
    (void) snprintf(buffer, bufferSize, "invalid");
  }

  return buffer;
}

void App_Network_SetUart(UART_HandleTypeDef *huart)
{
  s_log_uart = huart;
}

int debug_printf(const char *fmt, ...)
{
  char buffer[256];
  va_list args;
  int length;

  if (s_log_uart == NULL) {
    return 0;
  }

  va_start(args, fmt);
  length = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (length < 0) {
    return length;
  }

  if (length >= (int)sizeof(buffer)) {
    length = (int)sizeof(buffer) - 1;
  }

  if (HAL_UART_Transmit(s_log_uart, (uint8_t *) buffer, (uint16_t) length,
      (uint32_t) (length + 16)) != HAL_OK) {
    return -1;
  }

  return length;
}

void App_Network_Init(void)
{
  App_UdpEchoInit();
  App_LwiperfInit();

  debug_printf("Services ready: UDP echo port %u, iperf TCP port %u\r\n",
      APP_UDP_ECHO_PORT, APP_IPERF_TCP_PORT);
}

void App_Network_OnLinkStatusChanged(struct netif *netif)
{
  if (netif_is_link_up(netif)) {
    char ip_buffer[16];
    char mask_buffer[16];
    char gw_buffer[16];

    ip4addr_ntoa_r(netif_ip4_addr(netif), ip_buffer, sizeof(ip_buffer));
    ip4addr_ntoa_r(netif_ip4_netmask(netif), mask_buffer, sizeof(mask_buffer));
    ip4addr_ntoa_r(netif_ip4_gw(netif), gw_buffer, sizeof(gw_buffer));

    debug_printf("LAN8671 link up: ip=%s mask=%s gw=%s\r\n",
        ip_buffer, mask_buffer, gw_buffer);
  } else {
    debug_printf("LAN8671 link down\r\n");
  }
}

static void App_UdpEchoRecv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
    const ip_addr_t *addr, u16_t port)
{
  char remote_buffer[48];

  LWIP_UNUSED_ARG(arg);

  if (p != NULL) {
    debug_printf("udp-echo: rx from=%s:%u len=%u\r\n",
        App_IpAddrToString(addr, remote_buffer, sizeof(remote_buffer)),
        port,
        p->tot_len);

    if (udp_sendto(pcb, p, addr, port) != ERR_OK) {
      debug_printf("udp-echo: tx failed to=%s:%u len=%u\r\n",
          remote_buffer,
          port,
          p->tot_len);
    } else {
      debug_printf("udp-echo: tx to=%s:%u len=%u\r\n",
          remote_buffer,
          port,
          p->tot_len);
    }
    pbuf_free(p);
  }
}

static void App_UdpEchoInit(void)
{
  s_udp_echo_pcb = udp_new();
  if (s_udp_echo_pcb == NULL) {
    debug_printf("udp_new() failed\r\n");
    return;
  }

  if (udp_bind(s_udp_echo_pcb, IP_ADDR_ANY, APP_UDP_ECHO_PORT) != ERR_OK) {
    debug_printf("udp_bind() failed for port %u\r\n", APP_UDP_ECHO_PORT);
    udp_remove(s_udp_echo_pcb);
    s_udp_echo_pcb = NULL;
    return;
  }

  udp_recv(s_udp_echo_pcb, App_UdpEchoRecv, NULL);
  debug_printf("udp-echo: listening on %u\r\n", APP_UDP_ECHO_PORT);
}

static const char *App_LwiperfReportTypeToString(enum lwiperf_report_type report_type)
{
  switch (report_type) {
    case LWIPERF_TCP_DONE_SERVER:
      return "server-done";
    case LWIPERF_TCP_DONE_CLIENT:
      return "client-done";
    case LWIPERF_TCP_ABORTED_LOCAL:
      return "aborted-local";
    case LWIPERF_TCP_ABORTED_LOCAL_DATAERROR:
      return "aborted-data";
    case LWIPERF_TCP_ABORTED_LOCAL_TXERROR:
      return "aborted-tx";
    case LWIPERF_TCP_ABORTED_REMOTE:
      return "aborted-remote";
    default:
      return "unknown";
  }
}

static void App_LwiperfReport(void *arg, enum lwiperf_report_type report_type,
    const ip_addr_t *local_addr, u16_t local_port,
    const ip_addr_t *remote_addr, u16_t remote_port,
    u32_t bytes_transferred, u32_t ms_duration,
    u32_t bandwidth_kbitpsec)
{
  char local_buffer[48];
  char remote_buffer[48];

  LWIP_UNUSED_ARG(arg);

  ipaddr_ntoa_r(local_addr, local_buffer, sizeof(local_buffer));
  ipaddr_ntoa_r(remote_addr, remote_buffer, sizeof(remote_buffer));

  debug_printf(
      "iperf: type=%s local=%s:%u remote=%s:%u bytes=%lu duration_ms=%lu avg_kbit=%lu\r\n",
      App_LwiperfReportTypeToString(report_type),
      local_buffer,
      local_port,
      remote_buffer,
      remote_port,
      (unsigned long) bytes_transferred,
      (unsigned long) ms_duration,
      (unsigned long) bandwidth_kbitpsec);
}

static void App_LwiperfInit(void)
{
  s_lwiperf_session = lwiperf_start_tcp_server(IP_ADDR_ANY, APP_IPERF_TCP_PORT,
      App_LwiperfReport, NULL);
  if (s_lwiperf_session == NULL) {
    debug_printf("lwiperf_start_tcp_server() failed for port %u\r\n",
        APP_IPERF_TCP_PORT);
  } else {
    debug_printf("iperf: listening on tcp/%u\r\n", APP_IPERF_TCP_PORT);
  }
}