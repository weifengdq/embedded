/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * File Name          : ethernetif.h
  * Description        : This file provides initialization code for LWIP
  *                      middleWare.
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

#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__
#include "lwip/err.h"
#include "lwip/netif.h"

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

#include "eth_custom_phy_interface.h"

typedef struct
{
  uint32_t rx_alloc_status;
  uint32_t rx_alloc_error_count;
  uint32_t rx_alloc_recover_count;
  uint32_t rx_desc_idx;
  uint32_t rx_build_desc_idx;
  uint32_t rx_build_desc_cnt;
  uint32_t dma_csr;
} ethernetif_diag_t;

user_phy_Object_t *ethernetif_get_user_phy(void);
void ethernetif_get_diag(ethernetif_diag_t *diag);

/* USER CODE END 0 */

/* Exported functions ------------------------------------------------------- */
err_t ethernetif_init(struct netif *netif);

void ethernetif_input(struct netif *netif);
void ethernet_link_check_state(struct netif *netif);

void Error_Handler(void);
u32_t sys_jiffies(void);
u32_t sys_now(void);

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
#endif
