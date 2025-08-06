/*
 * Copyright (c) 2022-2023 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef CDC_ACM_H
#define CDC_ACM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CDC ACM initialization and control functions */
void cdc_acm_init(uint8_t busid, uint32_t reg_base);
void cdc_acm_data_send_test(uint8_t busid, const char* data, uint32_t len);
bool cdc_acm_is_dtr_enabled(void);

/* Global variables */
extern volatile bool dtr_enable;

#ifdef __cplusplus
}
#endif

#endif /* CDC_ACM_H */
