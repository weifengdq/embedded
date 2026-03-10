#include <stdio.h>

#include "lwip/apps/lwiperf.h"
#include "lwip/ip_addr.h"

#include "IperfApp.h"

static const char *IperfApp_reportTypeToString(enum lwiperf_report_type reportType)
{
    switch (reportType)
    {
    case LWIPERF_TCP_DONE_SERVER:
        return "server done";
    case LWIPERF_TCP_DONE_CLIENT:
        return "client done";
    case LWIPERF_TCP_ABORTED_LOCAL:
        return "aborted local";
    case LWIPERF_TCP_ABORTED_LOCAL_DATAERROR:
        return "aborted data error";
    case LWIPERF_TCP_ABORTED_LOCAL_TXERROR:
        return "aborted tx error";
    case LWIPERF_TCP_ABORTED_REMOTE:
        return "aborted remote";
    default:
        return "unknown";
    }
}

static void IperfApp_report(
    void *arg,
    enum lwiperf_report_type reportType,
    const ip_addr_t *localAddr,
    u16_t localPort,
    const ip_addr_t *remoteAddr,
    u16_t remotePort,
    u32_t bytesTransferred,
    u32_t durationMs,
    u32_t bandwidthKbitPerSec)
{
    char localAddrBuffer[IPADDR_STRLEN_MAX];
    char remoteAddrBuffer[IPADDR_STRLEN_MAX];

    LWIP_UNUSED_ARG(arg);

    ipaddr_ntoa_r(localAddr, localAddrBuffer, sizeof(localAddrBuffer));
    ipaddr_ntoa_r(remoteAddr, remoteAddrBuffer, sizeof(remoteAddrBuffer));

    printf("iperf result: %s, local %s:%u, remote %s:%u, bytes=%lu, duration=%lu ms, avg=%lu kbit/s\r\n",
           IperfApp_reportTypeToString(reportType),
           localAddrBuffer,
           (unsigned)localPort,
           remoteAddrBuffer,
           (unsigned)remotePort,
           (unsigned long)bytesTransferred,
           (unsigned long)durationMs,
           (unsigned long)bandwidthKbitPerSec);
}

void IperfApp_init(void)
{
    void *session;

    session = lwiperf_start_tcp_server_default(IperfApp_report, NULL);

    if (session == NULL)
    {
        printf("Failed to start lwIP iperf TCP server.\r\n");
    }
    else
    {
        printf("lwIP iperf server started on TCP port %u.\r\n", (unsigned)LWIPERF_TCP_PORT_DEFAULT);
    }
}