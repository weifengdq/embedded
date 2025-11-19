/*!
    \file    gd32a50x_cmp.h
    \brief   definitions for the CMP

    \version 2025-08-09, V1.7.0, firmware for GD32A50x
*/

/*
    Copyright (c) 2025, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#ifndef GD32A50X_CMP_H
#define GD32A50X_CMP_H

#include "gd32a50x.h"

/* CMP definitions */
#define CMP                                      CMP_BASE                               /*!< CMP base address */

/* registers definitions */
#define CMPX_CS                                  REG32((CMP) + 0x00000000U)             /*!< CMPx control and status register */

/* bits definitions */
/* CMPx_CS */
#define CMP_CS_CMPXEN                            BIT(0)                                 /*!< CMPx enable */
#define CMP_CS_CMPXM                             BITS(2,3)                              /*!< CMPx mode */
#define CMP_CS_CMPXMESEL                         BITS(4,6)                              /*!< CMP_IM external input selection */
#define CMP_CS_CMPXMISEL                         BITS(7,9)                              /*!< CMP_IM internal input selection */
#define CMP_CS_CMPXPSEL                          BITS(10,12)                            /*!< CMP_IP input selection */
#define CMP_CS_CMPXOSEL                          BITS(13,14)                            /*!< CMPx output selection */
#define CMP_CS_CMPXPL                            BIT(15)                                /*!< CMPx output polarity */
#define CMP_CS_CMPXHST                           BITS(16,17)                            /*!< CMPx hysteresis */
#define CMP_CS_CMPXBLK                           BITS(18,20)                            /*!< CMPx output blanking source */
#define CMP_CS_CMPXBEN                           BIT(22)                                /*!< CMPx scaler bridge enable bit */
#define CMP_CS_CMPXSEN                           BIT(23)                                /*!< CMPx voltage input scaler */
#define CMP_CS_CMPXO                             BIT(30)                                /*!< CMPx output state bit */
#define CMP_CS_CMPXLK                            BIT(31)                                /*!< CMPx lock */

/* constants definitions */
/* CMP units */
typedef enum{
    CMP0,                                                                               /*!< comparator 0 */
}cmp_enum;

/* CMP operating mode */
#define CS_CMPXM(regval)                         (BITS(2,3) & ((uint32_t)(regval) << 2U))
#define CMP_MODE_HIGHSPEED                       CS_CMPXM(0)                            /*!< CMP mode high speed */
#define CMP_MODE_MIDDLESPEED                     CS_CMPXM(1)                            /*!< CMP mode middle speed */
#define CMP_MODE_LOWSPEED                        CS_CMPXM(3)                            /*!< CMP mode low speed */

/* CMP hysteresis */
#define CS_CMPXHST(regval)                       (BITS(16,17) & ((uint32_t)(regval) << 16U))
#define CMP_HYSTERESIS_NO                        CS_CMPXHST(0)                          /*!< CMP output no hysteresis */
#define CMP_HYSTERESIS_LOW                       CS_CMPXHST(1)                          /*!< CMP output low hysteresis */
#define CMP_HYSTERESIS_MIDDLE                    CS_CMPXHST(2)                          /*!< CMP output middle hysteresis */
#define CMP_HYSTERESIS_HIGH                      CS_CMPXHST(3)                          /*!< CMP output high hysteresis */

/* CMP inverting input */
#define CS_CMPXMISEL(regval)                     (BITS(7,9) & ((uint32_t)(regval) << 7U))
#define CS_CMPXMESEL(regval)                     (BITS(4,6) & ((uint32_t)(regval) << 4U))
#define CMP_INVERTING_INPUT_1_4VREFINT           CS_CMPXMISEL(0)                        /*!< CMP inverting input 1/4 Vrefint */
#define CMP_INVERTING_INPUT_1_2VREFINT           CS_CMPXMISEL(1)                        /*!< CMP inverting input 1/2 Vrefint */
#define CMP_INVERTING_INPUT_3_4VREFINT           CS_CMPXMISEL(2)                        /*!< CMP inverting input 3/4 Vrefint */
#define CMP_INVERTING_INPUT_VREFINT              CS_CMPXMISEL(3)                        /*!< CMP inverting input Vrefint */
#define CMP_INVERTING_INPUT_PA7                  CS_CMPXMISEL(4)                        /*!< CMP inverting input PA7(DAC0_OUT0) */
#define CMP_INVERTING_INPUT_PC11                 CS_CMPXMESEL(0)|CS_CMPXMISEL(5)        /*!< CMP inverting input PC11 */
#define CMP_INVERTING_INPUT_PC10                 CS_CMPXMESEL(1)|CS_CMPXMISEL(5)        /*!< CMP inverting input PC10 */
#define CMP_INVERTING_INPUT_PB8                  CS_CMPXMESEL(2)|CS_CMPXMISEL(5)        /*!< CMP inverting input PB8 */
#define CMP_INVERTING_INPUT_PA0                  CS_CMPXMESEL(3)|CS_CMPXMISEL(5)        /*!< CMP inverting input PA0 */
#define CMP_INVERTING_INPUT_PA3                  CS_CMPXMESEL(4)|CS_CMPXMISEL(5)        /*!< CMP inverting input PA3 */
#define CMP_INVERTING_INPUT_PA4                  CS_CMPXMESEL(5)|CS_CMPXMISEL(5)        /*!< CMP inverting input PA4 */
#define CMP_INVERTING_INPUT_PA5                  CS_CMPXMESEL(6)|CS_CMPXMISEL(5)        /*!< CMP inverting input PA5 */
#define CMP_INVERTING_INPUT_PA6                  CS_CMPXMESEL(7)|CS_CMPXMISEL(5)        /*!< CMP inverting input PA6 */

/* CMP noninverting input*/
#define CS_CMPXPSEL(regval)                      (BITS(10,12) & ((uint32_t)(regval) << 10U))
#define CMP_NONINVERTING_INPUT_PC11              CS_CMPXPSEL(0)                         /*!< CMP noninverting input PC11 for for CMP0 */
#define CMP_NONINVERTING_INPUT_PC10              CS_CMPXPSEL(1)                         /*!< CMP noninverting input PC10 for for CMP0 */
#define CMP_NONINVERTING_INPUT_PB8               CS_CMPXPSEL(2)                         /*!< CMP noninverting input PB8 for for CMP0 */
#define CMP_NONINVERTING_INPUT_PA0               CS_CMPXPSEL(3)                         /*!< CMP noninverting input PA0 for for CMP0 */
#define CMP_NONINVERTING_INPUT_PA3               CS_CMPXPSEL(4)                         /*!< CMP noninverting input PA3 for for CMP0 */
#define CMP_NONINVERTING_INPUT_PA4               CS_CMPXPSEL(5)                         /*!< CMP noninverting input PA4 for for CMP0 */
#define CMP_NONINVERTING_INPUT_PA5               CS_CMPXPSEL(6)                         /*!< CMP noninverting input PA5 for for CMP0 */
#define CMP_NONINVERTING_INPUT_PA6               CS_CMPXPSEL(7)                         /*!< CMP noninverting input PA6 for for CMP0 */

/* CMP output */
#define CS_CMPXOSEL(regval)                      (BITS(13,14) & ((uint32_t)(regval) << 13U))
#define CMP_OUTPUT_NONE                          CS_CMPXOSEL(0)                         /*!< CMP output none */
#define CMP_OUTPUT_TIMER0_IC0                    CS_CMPXOSEL(1)                         /*!< CMP output TIMER0_CH0 input capture */
#define CMP_OUTPUT_TIMER7_IC0                    CS_CMPXOSEL(2)                         /*!< CMP output TIMER7_CH0 input capture */

/* CMP output polarity*/
#define CS_CMPXPL(regval)                        (BIT(15) & ((uint32_t)(regval) << 15U))
#define CMP_OUTPUT_POLARITY_NONINVERTED          CS_CMPXPL(0)                           /*!< CMP output not inverted */
#define CMP_OUTPUT_POLARITY_INVERTED             CS_CMPXPL(1)                           /*!< CMP output inverted */

/* CMP blanking suorce */
#define CS_CMPXBLK(regval)                       (BITS(18,20) & ((uint32_t)(regval) << 18U))
#define CMP_BLANKING_NONE                        CS_CMPXBLK(0)                          /*!< CMP no blanking source */
#define CMP_BLANKING_TIMER0_OC1                  CS_CMPXBLK(1)                          /*!< CMP TIMER0_CH1 output compare signal selected as blanking source */
#define CMP_BLANKING_TIMER7_OC1                  CS_CMPXBLK(2)                          /*!< CMP TIMER7_CH1 output compare signal selected as blanking source */
#define CMP_BLANKING_TIMER1_OC1                  CS_CMPXBLK(3)                          /*!< CMP TIMER1_CH1 output compare signal selected as blanking source */

/* CMP output level */
#define CMP_OUTPUTLEVEL_HIGH                     ((uint32_t)0x00000001U)                /*!< CMP output high */
#define CMP_OUTPUTLEVEL_LOW                      ((uint32_t)0x00000000U)                /*!< CMP output low */

/* function declarations */
/* initialization functions */
/* CMP deinit */
void cmp_deinit(cmp_enum cmp_periph);
/* CMP mode init */
void cmp_mode_init(cmp_enum cmp_periph, uint32_t operating_mode, uint32_t inverting_input, uint32_t output_hysteresis);
/* CMP noninverting input select */
void cmp_noninverting_input_select(cmp_enum cmp_periph, uint32_t noninverting_input);
/* CMP output init */
void cmp_output_init(cmp_enum cmp_periph, uint32_t output_selection, uint32_t output_polarity);
/* CMP output blanking function init */
void cmp_blanking_init(cmp_enum cmp_periph,uint32_t blanking_source_selection);

/* enable functions */
/* enable CMP */
void cmp_enable(cmp_enum cmp_periph);
/* disable CMP */
void cmp_disable(cmp_enum cmp_periph);
/* lock the CMP */
void cmp_lock_enable(cmp_enum cmp_periph);
/* enable the voltage scaler */
void cmp_voltage_scaler_enable(cmp_enum cmp_periph);
/* disable the voltage scaler */
void cmp_voltage_scaler_disable(cmp_enum cmp_periph);
/* enable the scaler bridge */
void cmp_scaler_bridge_enable(cmp_enum cmp_periph);
/* disable the scaler bridge */
void cmp_scaler_bridge_disable(cmp_enum cmp_periph);

/* get state related functions */
/* get output level */
uint32_t cmp_output_level_get(cmp_enum cmp_periph);

#endif /* GD32A50X_CMP_H */
