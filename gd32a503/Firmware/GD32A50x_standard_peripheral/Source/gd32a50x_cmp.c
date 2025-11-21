/*!
    \file    gd32a50x_cmp.c
    \brief   CMP driver

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

#include "gd32a50x_cmp.h"

/*!
    \brief      CMP deinit
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[out] none
    \retval     none
*/
void cmp_deinit(cmp_enum cmp_periph)
{
    if(CMP0 == cmp_periph){
        CMPX_CS = ((uint32_t)0x00000000U);
    }else{
    }
}

/*!
    \brief      CMP mode init
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[in]  operating_mode
      \arg        CMP_MODE_HIGHSPEED: high speed mode
      \arg        CMP_MODE_MIDDLESPEED: medium speed mode
      \arg        CMP_MODE_LOWSPEED: low speed mode
    \param[in]  inverting_input
      \arg        CMP_INVERTING_INPUT_1_4VREFINT: VREFINT *1/4 input
      \arg        CMP_INVERTING_INPUT_1_2VREFINT: VREFINT *1/2 input
      \arg        CMP_INVERTING_INPUT_3_4VREFINT: VREFINT *3/4 input
      \arg        CMP_INVERTING_INPUT_VREFINT: VREFINT input
      \arg        CMP_INVERTING_INPUT_PA7: PA7(DAC0_OUT0) input
      \arg        CMP_INVERTING_INPUT_PC11: CMP inverting input PC11
      \arg        CMP_INVERTING_INPUT_PC10: CMP inverting input PC10
      \arg        CMP_INVERTING_INPUT_PB8: CMP inverting input PB8 
      \arg        CMP_INVERTING_INPUT_PA0: CMP inverting input PA0 
      \arg        CMP_INVERTING_INPUT_PA3: CMP inverting input PA3 
      \arg        CMP_INVERTING_INPUT_PA4: CMP inverting input PA4 
      \arg        CMP_INVERTING_INPUT_PA5: CMP inverting input PA5 
      \arg        CMP_INVERTING_INPUT_PA6: CMP inverting input PA6 
    \param[in]  output_hysteresis
      \arg        CMP_HYSTERESIS_NO: output no hysteresis
      \arg        CMP_HYSTERESIS_LOW: output low hysteresis
      \arg        CMP_HYSTERESIS_MIDDLE: output middle hysteresis
      \arg        CMP_HYSTERESIS_HIGH: output high hysteresis
    \param[out] none
    \retval     none
*/
void cmp_mode_init(cmp_enum cmp_periph, uint32_t operating_mode, uint32_t inverting_input, uint32_t output_hysteresis)
{
    uint32_t temp = 0U;

    if(CMP0 == cmp_periph){
        /* initialize comparator 0 mode */
        temp = CMPX_CS;
        temp &= ~(uint32_t)(CMP_CS_CMPXM | CMP_CS_CMPXMESEL | CMP_CS_CMPXMISEL | CMP_CS_CMPXHST);
        temp |= (uint32_t)(operating_mode | inverting_input | output_hysteresis);
        CMPX_CS = temp;
    }else{
    }
}

/*!
    \brief      CMP noninverting input select
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[in]  noninverting_input
      \arg        CMP_NONINVERTING_INPUT_PC11: CMP noninverting input PC11 for for CMP0
      \arg        CMP_NONINVERTING_INPUT_PC10: CMP noninverting input PC10 for for CMP0
      \arg        CMP_NONINVERTING_INPUT_PB8: CMP noninverting input PB8 for for CMP0
      \arg        CMP_NONINVERTING_INPUT_PA0: CMP noninverting input PA0 for for CMP0
      \arg        CMP_NONINVERTING_INPUT_PA3: CMP noninverting input PA3 for for CMP0
      \arg        CMP_NONINVERTING_INPUT_PA4: CMP noninverting input PA4 for for CMP0
      \arg        CMP_NONINVERTING_INPUT_PA5: CMP noninverting input PA5 for for CMP0
      \arg        CMP_NONINVERTING_INPUT_PA6: CMP noninverting input PA6 for for CMP0      
    \param[out] none
    \retval     none
*/
void cmp_noninverting_input_select(cmp_enum cmp_periph, uint32_t noninverting_input)
{
    uint32_t temp = 0U;

    if(CMP0 == cmp_periph){
        temp = CMPX_CS;
        temp &= ~(uint32_t)CMP_CS_CMPXPSEL;
        temp |= (uint32_t)noninverting_input;
        CMPX_CS = temp;
    }else{
    }
}

/*!
    \brief      CMP output init
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[in]  output_selection
      \arg        CMP_OUTPUT_NONE: CMP output none
      \arg        CMP_OUTPUT_TIMER0_IC0: CMP output TIMER0_CH0 input capture
      \arg        CMP_OUTPUT_TIMER7_IC0: CMP output TIMER7_CH0 input capture
    \param[in]  output_polarity
      \arg        CMP_OUTPUT_POLARITY_INVERTED: output is inverted
      \arg        CMP_OUTPUT_POLARITY_NONINVERTED: output is not inverted
    \param[out] none
    \retval     none
*/
void cmp_output_init(cmp_enum cmp_periph, uint32_t output_selection, uint32_t output_polarity)
{
    uint32_t temp = 0U;

    if(CMP0 == cmp_periph){
        /* initialize comparator 0 output */
        temp = CMPX_CS;
        temp &= ~(uint32_t)CMP_CS_CMPXOSEL;
        temp |= (uint32_t)output_selection;
        /* output polarity */
        if(CMP_OUTPUT_POLARITY_INVERTED == output_polarity){
            temp |= (uint32_t)CMP_CS_CMPXPL;
        }else{
            temp &= ~(uint32_t)CMP_CS_CMPXPL;
        }
        CMPX_CS = temp;
    }else{
    }
}

/*!
    \brief      CMP output blanking function init
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[in]  blanking_source_selection 
      \arg        CMP_BLANKING_NONE: CMP no blanking source
      \arg        CMP_BLANKING_TIMER0_OC1: CMP TIMER0_CH1 output compare signal selected as blanking source
      \arg        CMP_BLANKING_TIMER1_OC1: CMP TIMER1_CH1 output compare signal selected as blanking source
      \arg        CMP_BLANKING_TIMER7_OC1: CMP TIMER7_CH1 output compare signal selected as blanking source
    \param[out] none
    \retval     none
*/
void cmp_blanking_init(cmp_enum cmp_periph, uint32_t blanking_source_selection)
{
    uint32_t temp = 0U;

    if(CMP0 == cmp_periph){
        temp = CMPX_CS;
        temp &= ~(uint32_t)CMP_CS_CMPXBLK;
        temp |= (uint32_t)blanking_source_selection;
        CMPX_CS = temp;
    }else{
    }
}


/*!
    \brief      enable CMP
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[out] none
    \retval     none
*/
void cmp_enable(cmp_enum cmp_periph)
{
    if(CMP0 == cmp_periph){
        CMPX_CS |= (uint32_t)CMP_CS_CMPXEN;
    }else{
    }
}

/*!
    \brief      disable CMP
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[out] none
    \retval     none
*/
void cmp_disable(cmp_enum cmp_periph)
{
    if(CMP0 == cmp_periph){
        CMPX_CS &= ~(uint32_t)CMP_CS_CMPXEN;
    }else{
    }
}

/*!
    \brief      lock the CMP
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[out] none
    \retval     none
*/
void cmp_lock_enable(cmp_enum cmp_periph)
{
    if(CMP0 == cmp_periph){
        /* lock CMP0 */
        CMPX_CS |= (uint32_t)CMP_CS_CMPXLK;
    }else{
    }
}

/*!
    \brief      enable the voltage scaler
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[out] none
    \retval     none
*/
void cmp_voltage_scaler_enable(cmp_enum cmp_periph)
{
    if(CMP0 == cmp_periph){
        CMPX_CS |= (uint32_t)CMP_CS_CMPXSEN;
    }else{
    }
}

/*!
    \brief      disable the voltage scaler
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[out] none
    \retval     none
*/
void cmp_voltage_scaler_disable(cmp_enum cmp_periph)
{
    if(CMP0 == cmp_periph){
        CMPX_CS &= ~(uint32_t)CMP_CS_CMPXSEN;
    }else{
    }
}
/*! 
    \brief      enable the scaler bridge
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[out] none
    \retval     none
*/
void cmp_scaler_bridge_enable(cmp_enum cmp_periph)
{
    if(CMP0 == cmp_periph){
        CMPX_CS |= (uint32_t)CMP_CS_CMPXBEN;
    }else{
    }
}

/*!
    \brief      disable the scaler bridge
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[out] none
    \retval     none
*/
void cmp_scaler_bridge_disable(cmp_enum cmp_periph)
{
    if(CMP0 == cmp_periph){
        CMPX_CS &= ~(uint32_t)CMP_CS_CMPXBEN;
    }else{
    }
}

/*!
    \brief      get output level
    \param[in]  cmp_periph
      \arg        CMP0: comparator 0
    \param[out] none
    \retval     the output level
*/
uint32_t cmp_output_level_get(cmp_enum cmp_periph)
{
    /* get output level of CMP0 */
    if((uint32_t)RESET != (CMPX_CS & CMP_CS_CMPXO)) {
        return CMP_OUTPUTLEVEL_HIGH;
    }else{
        return CMP_OUTPUTLEVEL_LOW;
    }
}
