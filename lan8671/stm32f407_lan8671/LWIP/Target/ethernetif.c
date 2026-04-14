/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : ethernetif.c
  * Description        : This file provides code for the configuration
  *                      of the ethernetif.c MiddleWare.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "lwip/opt.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "lwip/ethip6.h"
#include "ethernetif.h"
/* USER CODE BEGIN Include for User BSP */

#include "eth_custom_phy_interface.h"

/* USER CODE END Include for User BSP */
#include <string.h>

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* Private define ------------------------------------------------------------*/

/* Network interface name */
#define IFNAME0 's'
#define IFNAME1 't'

/* ETH Setting  */
#define ETH_DMA_TRANSMIT_TIMEOUT               ( 20U )
#define ETH_DMA_TRANSMIT_RETRY_TIMEOUT         ( 20U )
#define ETH_TX_BUFFER_MAX             ((ETH_TX_DESC_CNT) * 2U)

/* USER CODE BEGIN 1 */

#define ETH_BRINGUP_MAX_ATTEMPTS              5U
#define ETH_BRINGUP_RETRY_DELAY_MS            100U
#define ETH_BRINGUP_PHY_RESET_ASSERT_MS       10U
#define ETH_BRINGUP_PHY_POST_RESET_DELAY_MS   300U
#define ETH_DIAG_ATTR                         __attribute__((used, externally_visible))

/* USER CODE END 1 */

/* Private variables ---------------------------------------------------------*/
/*
@Note: This interface is implemented to operate in zero-copy mode only:
        - Rx Buffers will be allocated from LwIP stack Rx memory pool,
          then passed to ETH HAL driver.
        - Tx Buffers will be allocated from LwIP stack memory heap,
          then passed to ETH HAL driver.

@Notes:
  1.a. ETH DMA Rx descriptors must be contiguous, the default count is 4,
       to customize it please redefine ETH_RX_DESC_CNT in ETH GUI (Rx Descriptor Length)
       so that updated value will be generated in stm32xxxx_hal_conf.h
  1.b. ETH DMA Tx descriptors must be contiguous, the default count is 4,
       to customize it please redefine ETH_TX_DESC_CNT in ETH GUI (Tx Descriptor Length)
       so that updated value will be generated in stm32xxxx_hal_conf.h

  2.a. Rx Buffers number must be between ETH_RX_DESC_CNT and 2*ETH_RX_DESC_CNT
  2.b. Rx Buffers must have the same size: ETH_RX_BUF_SIZE, this value must
       passed to ETH DMA in the init field (heth.Init.RxBuffLen)
  2.c  The RX Ruffers addresses and sizes must be properly defined to be aligned
       to L1-CACHE line size (32 bytes).
*/

/* Data Type Definitions */
typedef enum
{
  RX_ALLOC_OK       = 0x00,
  RX_ALLOC_ERROR    = 0x01
} RxAllocStatusTypeDef;

typedef struct
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[(ETH_RX_BUF_SIZE + 31) & ~31] __ALIGNED(32);
} RxBuff_t;

/* Memory Pool Declaration */
#define ETH_RX_BUFFER_CNT             12U
LWIP_MEMPOOL_DECLARE(RX_POOL, ETH_RX_BUFFER_CNT, sizeof(RxBuff_t), "Zero-copy RX PBUF pool");

/* Variable Definitions */
static volatile uint8_t RxAllocStatus;

ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

/* USER CODE BEGIN 2 */

static user_phy_Object_t USER_PHY;

ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_stage;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_attempt;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_hal_status;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_error_code;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_dma_error_code;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_mac_error_code;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_dmabmr;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_refclk_before;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_refclk_after;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_refclk_level_before;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_refclk_level_after;
ETH_DIAG_ATTR volatile uint32_t g_eth_bringup_irq_level;
ETH_DIAG_ATTR volatile uint32_t g_eth_tx_retry_count;
ETH_DIAG_ATTR volatile uint32_t g_eth_tx_error_count;
ETH_DIAG_ATTR volatile uint32_t g_eth_tx_last_retry_count;
ETH_DIAG_ATTR volatile uint32_t g_eth_tx_max_retry_count;
ETH_DIAG_ATTR volatile uint32_t g_eth_tx_last_error_code;

/* USER CODE END 2 */

/* Global Ethernet handle */
ETH_HandleTypeDef heth;
ETH_TxPacketConfig TxConfig;

/* Private function prototypes -----------------------------------------------*/

/* USER CODE BEGIN 3 */

/* USER CODE END 3 */

/* Private functions ---------------------------------------------------------*/
void pbuf_free_custom(struct pbuf *p);

/* USER CODE BEGIN 4 */

static int32_t ETH_PHY_IO_ReadReg(uint32_t devAddr, uint32_t regAddr, uint32_t *pRegVal)
{
  uint32_t value = 0U;

  if (HAL_ETH_ReadPHYRegister(&heth, devAddr, regAddr, &value) != HAL_OK) {
    return -1;
  }

  *pRegVal = value & 0xFFFFU;
  return 0;
}

static int32_t ETH_PHY_IO_WriteReg(uint32_t devAddr, uint32_t regAddr, uint32_t regVal)
{
  if (HAL_ETH_WritePHYRegister(&heth, devAddr, regAddr, regVal & 0xFFFFU) != HAL_OK) {
    return -1;
  }

  return 0;
}

static int32_t ETH_PHY_IO_GetTick(void)
{
  return (int32_t) HAL_GetTick();
}

static uint32_t ETH_SamplePinTransitions(GPIO_TypeDef *port, uint16_t pin)
{
  uint32_t previous = port->IDR & pin;
  uint32_t transitions = 0U;

  for (uint32_t index = 0U; index < 2048U; ++index) {
    uint32_t sample = port->IDR & pin;

    if (sample != previous) {
      ++transitions;
      previous = sample;
    }
  }

  return transitions;
}

static void ETH_UpdateBringupPinDiagnostics(volatile uint32_t *transitionCount,
                                            volatile uint32_t *level)
{
  *level = ((GPIOA->IDR & GPIO_PIN_1) != 0U) ? 1U : 0U;
  *transitionCount = ETH_SamplePinTransitions(GPIOA, GPIO_PIN_1);
  g_eth_bringup_irq_level =
      (HAL_GPIO_ReadPin(LAN8671_IRQ_N_GPIO_Port, LAN8671_IRQ_N_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static void ETH_PHY_HardReset(void)
{
  HAL_GPIO_WritePin(LAN8671_RESET_N_GPIO_Port, LAN8671_RESET_N_Pin, GPIO_PIN_RESET);
  HAL_Delay(ETH_BRINGUP_PHY_RESET_ASSERT_MS);
  HAL_GPIO_WritePin(LAN8671_RESET_N_GPIO_Port, LAN8671_RESET_N_Pin, GPIO_PIN_SET);
  HAL_Delay(ETH_BRINGUP_PHY_POST_RESET_DELAY_MS);
}

/* USER CODE END 4 */

/*******************************************************************************
                       LL Driver Interface ( LwIP stack --> ETH)
*******************************************************************************/
/**
 * @brief In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 *
 * @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
static void low_level_init(struct netif *netif)
{
  HAL_StatusTypeDef hal_eth_init_status = HAL_OK;
  uint32_t attempt;
  user_phy_IOCtx_t ioctx;
  /* Start ETH HAL Init */

  uint8_t MACAddr[6];
  heth.Instance = ETH;
  MACAddr[0] = 0x02;
  MACAddr[1] = 0x00;
  MACAddr[2] = 0x00;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x67;
  MACAddr[5] = 0x11;
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = 1536;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  g_eth_bringup_stage = 0x3000U;
  g_eth_bringup_attempt = 0U;
  g_eth_bringup_hal_status = (uint32_t) HAL_OK;
  g_eth_bringup_error_code = 0U;
  g_eth_bringup_dma_error_code = 0U;
  g_eth_bringup_mac_error_code = 0U;
  g_eth_bringup_dmabmr = 0U;
  g_eth_bringup_refclk_before = 0U;
  g_eth_bringup_refclk_after = 0U;
  g_eth_bringup_refclk_level_before = 0U;
  g_eth_bringup_refclk_level_after = 0U;
  g_eth_bringup_irq_level = 0U;

  for (attempt = 1U; attempt <= ETH_BRINGUP_MAX_ATTEMPTS; ++attempt) {
    g_eth_bringup_stage = 0x3000U + attempt;
    g_eth_bringup_attempt = attempt;

    if (attempt > 1U) {
      g_eth_bringup_stage = 0x3100U + attempt;
      ETH_PHY_HardReset();
    }

    ETH_UpdateBringupPinDiagnostics(&g_eth_bringup_refclk_before,
                                    &g_eth_bringup_refclk_level_before);

    heth.gState = HAL_ETH_STATE_RESET;
    heth.ErrorCode = HAL_ETH_ERROR_NONE;
    heth.DMAErrorCode = 0U;
    heth.MACErrorCode = 0U;

    g_eth_bringup_stage = 0x3200U + attempt;
    hal_eth_init_status = HAL_ETH_Init(&heth);

    ETH_UpdateBringupPinDiagnostics(&g_eth_bringup_refclk_after,
                                    &g_eth_bringup_refclk_level_after);

    g_eth_bringup_hal_status = (uint32_t) hal_eth_init_status;
    g_eth_bringup_error_code = heth.ErrorCode;
    g_eth_bringup_dma_error_code = heth.DMAErrorCode;
    g_eth_bringup_mac_error_code = heth.MACErrorCode;
    g_eth_bringup_dmabmr = heth.Instance->DMABMR;

    if (hal_eth_init_status == HAL_OK) {
      g_eth_bringup_stage = 0x3300U + attempt;
      break;
    }

    HAL_Delay(ETH_BRINGUP_RETRY_DELAY_MS);
  }

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;

  /* End ETH HAL Init */

  /* Initialize the RX POOL */
  LWIP_MEMPOOL_INIT(RX_POOL);

#if LWIP_ARP || LWIP_ETHERNET

  /* set MAC hardware address length */
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* set MAC hardware address */
  netif->hwaddr[0] =  heth.Init.MACAddr[0];
  netif->hwaddr[1] =  heth.Init.MACAddr[1];
  netif->hwaddr[2] =  heth.Init.MACAddr[2];
  netif->hwaddr[3] =  heth.Init.MACAddr[3];
  netif->hwaddr[4] =  heth.Init.MACAddr[4];
  netif->hwaddr[5] =  heth.Init.MACAddr[5];

  /* maximum transfer unit */
  netif->mtu = ETH_MAX_PAYLOAD;

  /* Accept broadcast address and ARP traffic */
  /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
  #if LWIP_ARP
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  #else
    netif->flags |= NETIF_FLAG_BROADCAST;
  #endif /* LWIP_ARP */

/* USER CODE BEGIN low_level_init Code 1 for User BSP */

/* USER CODE END low_level_init Code 1 for User BSP */

  if (hal_eth_init_status == HAL_OK)
  {
    ioctx.Init = NULL;
    ioctx.DeInit = NULL;
    ioctx.ReadReg = ETH_PHY_IO_ReadReg;
    ioctx.WriteReg = ETH_PHY_IO_WriteReg;
    ioctx.GetTick = ETH_PHY_IO_GetTick;

    if ((USER_PHY_RegisterBusIO(&USER_PHY, &ioctx) != USER_PHY_STATUS_OK)
        || (USER_PHY_Init(&USER_PHY) != USER_PHY_STATUS_OK)) {
      Error_Handler();
    }

    /* Get link state */
    ethernet_link_check_state(netif);
  }
  else
  {
    g_eth_bringup_stage = 0x3EE0U;
    Error_Handler();
  }
#endif /* LWIP_ARP || LWIP_ETHERNET */

/* USER CODE BEGIN LOW_LEVEL_INIT */

/* USER CODE END LOW_LEVEL_INIT */
}

/**
 * @brief This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *         an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become available since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  uint32_t i = 0U;
  uint32_t retry_count = 0U;
  uint32_t retry_start_tick;
  struct pbuf *q = NULL;
  ETH_BufferTypeDef Txbuffer[ETH_TX_DESC_CNT] = {0};

  if (!netif_is_link_up(netif)) {
    return ERR_IF;
  }

  memset(Txbuffer, 0 , ETH_TX_DESC_CNT*sizeof(ETH_BufferTypeDef));

  for(q = p; q != NULL; q = q->next)
  {
    if(i >= ETH_TX_DESC_CNT)
      return ERR_IF;

    Txbuffer[i].buffer = q->payload;
    Txbuffer[i].len = q->len;

    if(i>0)
    {
      Txbuffer[i-1].next = &Txbuffer[i];
    }

    if(q->next == NULL)
    {
      Txbuffer[i].next = NULL;
    }

    i++;
  }

  TxConfig.Length = p->tot_len;
  TxConfig.TxBuffer = Txbuffer;
  TxConfig.pData = p;

  retry_start_tick = HAL_GetTick();

  do
  {
    heth.ErrorCode = HAL_ETH_ERROR_NONE;

    if (HAL_ETH_Transmit(&heth, &TxConfig, ETH_DMA_TRANSMIT_TIMEOUT) == HAL_OK)
    {
      g_eth_tx_retry_count += retry_count;
      g_eth_tx_last_retry_count = retry_count;

      if (retry_count > g_eth_tx_max_retry_count)
      {
        g_eth_tx_max_retry_count = retry_count;
      }

      g_eth_tx_last_error_code = HAL_ETH_ERROR_NONE;
      return ERR_OK;
    }

    retry_count++;
  } while ((HAL_GetTick() - retry_start_tick) < ETH_DMA_TRANSMIT_RETRY_TIMEOUT);

  g_eth_tx_retry_count += retry_count;
  g_eth_tx_last_retry_count = retry_count;

  if (retry_count > g_eth_tx_max_retry_count)
  {
    g_eth_tx_max_retry_count = retry_count;
  }

  g_eth_tx_error_count++;
  g_eth_tx_last_error_code = heth.ErrorCode;

  return ERR_IF;
}

/**
 * @brief Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return a pbuf filled with the received packet (including MAC header)
 *         NULL on memory error
   */
static struct pbuf * low_level_input(struct netif *netif)
{
  struct pbuf *p = NULL;

  if (netif_is_link_up(netif) && (RxAllocStatus == RX_ALLOC_OK))
  {
    HAL_ETH_ReadData(&heth, (void **)&p);
  }

  return p;
}

/**
 * @brief This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface. Then the type of the received packet is determined and
 * the appropriate input function is called.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
void ethernetif_input(struct netif *netif)
{
  struct pbuf *p = NULL;

  do
  {
    p = low_level_input( netif );
    if (p != NULL)
    {
      if (netif->input( p, netif) != ERR_OK )
      {
        pbuf_free(p);
      }
    }
  } while(p!=NULL);
}

#if !LWIP_ARP
/**
 * This function has to be completed by user in case of ARP OFF.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if ...
 */
static err_t low_level_output_arp_off(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr)
{
  err_t errval;
  errval = ERR_OK;

/* USER CODE BEGIN 5 */

/* USER CODE END 5 */

  return errval;

}
#endif /* LWIP_ARP */

/**
 * @brief Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

  /*
   * Initialize the snmp variables and counters inside the struct netif.
   * The last argument should be replaced with your link speed, in units
   * of bits per second.
   */
  // MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  /* We directly use etharp_output() here to save a function call.
   * You can instead declare your own function an call etharp_output()
   * from it if you have to do some checks before sending (e.g. if link
   * is available...) */

#if LWIP_IPV4
#if LWIP_ARP || LWIP_ETHERNET
#if LWIP_ARP
  netif->output = etharp_output;
#else
  /* The user should write its own code in low_level_output_arp_off function */
  netif->output = low_level_output_arp_off;
#endif /* LWIP_ARP */
#endif /* LWIP_ARP || LWIP_ETHERNET */
#endif /* LWIP_IPV4 */

#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */

  netif->linkoutput = low_level_output;

  /* initialize the hardware */
  low_level_init(netif);

  return ERR_OK;
}

/**
  * @brief  Custom Rx pbuf free callback
  * @param  pbuf: pbuf to be freed
  * @retval None
  */
void pbuf_free_custom(struct pbuf *p)
{
  struct pbuf_custom* custom_pbuf = (struct pbuf_custom*)p;
  LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);

  /* If the Rx Buffer Pool was exhausted, signal the ethernetif_input task to
   * call HAL_ETH_GetRxDataBuffer to rebuild the Rx descriptors. */

  if (RxAllocStatus == RX_ALLOC_ERROR)
  {
    RxAllocStatus = RX_ALLOC_OK;
  }
}

/* USER CODE BEGIN 6 */

/**
* @brief  Returns the current time in milliseconds
*         when LWIP_TIMERS == 1 and NO_SYS == 1
* @param  None
* @retval Current Time value
*/
u32_t sys_now(void)
{
  return HAL_GetTick();
}

/* USER CODE END 6 */

/* USER CODE BEGIN PHI IO Functions for User BSP */

/* USER CODE END PHI IO Functions for User BSP */

/**
  * @brief  Check the ETH link state then update ETH driver and netif link accordingly.
  * @retval None
  */
void ethernet_link_check_state(struct netif *netif)
{
  ETH_MACConfigTypeDef MACConf = {0};
  int32_t PHYLinkState;
  uint32_t linkchanged = 0U;
  uint32_t speed = 0U;
  uint32_t duplex = 0U;

  PHYLinkState = USER_PHY_GetLinkState(&USER_PHY);
  if (PHYLinkState < 0) {
    return;
  }

  if (netif_is_link_up(netif) && (PHYLinkState == USER_PHY_STATUS_LINK_DOWN))
  {
    HAL_ETH_Stop(&heth);
    netif_set_down(netif);
    netif_set_link_down(netif);
  }
  else if (!netif_is_link_up(netif) && (PHYLinkState > USER_PHY_STATUS_LINK_DOWN))
  {
    switch (PHYLinkState)
    {
      case USER_PHY_STATUS_10MBITS_HALFDUPLEX:
      case USER_PHY_STATUS_10MBITS_FULLDUPLEX:
        duplex = ETH_HALFDUPLEX_MODE;
        speed = ETH_SPEED_10M;
        linkchanged = 1U;
        break;

      default:
        break;
    }

    if (linkchanged != 0U)
    {
      HAL_ETH_GetMACConfig(&heth, &MACConf);
      MACConf.DuplexMode = duplex;
      MACConf.Speed = speed;
      HAL_ETH_SetMACConfig(&heth, &MACConf);
      HAL_ETH_Start(&heth);
      netif_set_up(netif);
      netif_set_link_up(netif);
    }
  }
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
/* USER CODE BEGIN HAL ETH RxAllocateCallback */
  struct pbuf_custom *p = LWIP_MEMPOOL_ALLOC(RX_POOL);
  if (p)
  {
    /* Get the buff from the struct pbuf address. */
    *buff = (uint8_t *)p + offsetof(RxBuff_t, buff);
    p->custom_free_function = pbuf_free_custom;
    /* Initialize the struct pbuf.
    * This must be performed whenever a buffer's allocated because it may be
    * changed by lwIP or the app, e.g., pbuf_free decrements ref. */
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, p, *buff, ETH_RX_BUF_SIZE);
  }
  else
  {
    RxAllocStatus = RX_ALLOC_ERROR;
    *buff = NULL;
  }
/* USER CODE END HAL ETH RxAllocateCallback */
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
/* USER CODE BEGIN HAL ETH RxLinkCallback */

  struct pbuf **ppStart = (struct pbuf **)pStart;
  struct pbuf **ppEnd = (struct pbuf **)pEnd;
  struct pbuf *p = NULL;

  /* Get the struct pbuf from the buff address. */
  p = (struct pbuf *)(buff - offsetof(RxBuff_t, buff));
  p->next = NULL;
  p->tot_len = 0;
  p->len = Length;

  /* Chain the buffer. */
  if (!*ppStart)
  {
    /* The first buffer of the packet. */
    *ppStart = p;
  }
  else
  {
    /* Chain the buffer to the end of the packet. */
    (*ppEnd)->next = p;
  }
  *ppEnd  = p;

  /* Update the total length of all the buffers of the chain. Each pbuf in the chain should have its tot_len
   * set to its own length, plus the length of all the following pbufs in the chain. */
  for (p = *ppStart; p != NULL; p = p->next)
  {
    p->tot_len += Length;
  }

/* USER CODE END HAL ETH RxLinkCallback */
}

void HAL_ETH_TxFreeCallback(uint32_t * buff)
{
/* USER CODE BEGIN HAL ETH TxFreeCallback */

  pbuf_free((struct pbuf *)buff);

/* USER CODE END HAL ETH TxFreeCallback */
}

/* USER CODE BEGIN 8 */

/* USER CODE END 8 */
