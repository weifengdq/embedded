/*!
    \file    lin.c
    \brief   lin driver
    
    \version 2022-10-21, V1.0.0, demo for GD32A50x
*/

/*
    Copyright (c) 2022, GigaDevice Semiconductor Inc.

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

#include "gd32a50x.h"
#include "lin.h"
#include "main.h"

static void lin_tx_out(lin_typedef_enum lin_num);
static void lin_rx_in(lin_typedef_enum lin_num);
static void lin_gpio_init(lin_typedef_enum lin_num);
static void lin_nvic_config(lin_typedef_enum lin_num);
static void lin_periph_config(lin_typedef_enum lin_num);
static void lin_break_send(lin_typedef_enum lin_num);
static void lin_sendbyte(lin_typedef_enum lin_num, uint8_t ch);
static ErrStatus check_send(lin_typedef_enum lin_num, lin_phase phase, uint8_t data);

/*!
    \brief      TX pin of lin node configuration
    \param[in]  lin_num: lin node to be configured
      \arg       LIN1
    \param[out] none
    \retval     none
*/
static void lin_tx_out(lin_typedef_enum lin_num)
{
    gpio_af_set(LIN_PORT_TX[lin_num], LIN_AF_TX[lin_num], LIN_PIN_TX[lin_num]);
    gpio_mode_set(LIN_PORT_TX[lin_num], GPIO_MODE_AF, GPIO_PUPD_PULLUP, LIN_PIN_TX[lin_num]);
    gpio_output_options_set(LIN_PORT_TX[lin_num], GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, LIN_PIN_TX[lin_num]);
}

/*!
    \brief      RX pin of lin node configuration
    \param[in]  lin_num: lin node to be configured
      \arg       LIN1
    \param[out] none
    \retval     none
*/
static void lin_rx_in(lin_typedef_enum lin_num)
{
    gpio_af_set(LIN_PORT_RX[lin_num], LIN_AF_RX[lin_num], LIN_PIN_RX[lin_num]);
    gpio_mode_set(LIN_PORT_RX[lin_num], GPIO_MODE_AF, GPIO_PUPD_PULLUP, LIN_PIN_RX[lin_num]);
    gpio_output_options_set(LIN_PORT_RX[lin_num], GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, LIN_PIN_RX[lin_num]);
}

/*!
    \brief      GPIO configuration of lin node 
    \param[in]  lin_num: lin node to be configured
      \arg       LIN1
    \param[out] none
    \retval     none
*/
static void lin_gpio_init(lin_typedef_enum lin_num)
{
    rcu_periph_clock_enable(LIN_CLK_TX[lin_num]);
    rcu_periph_clock_enable(LIN_CLK_RX[lin_num]);
    
    /* initialize GPIO port */
    lin_tx_out(lin_num);
    lin_rx_in(lin_num);
}

/*!
    \brief      NVIC configuration of lin node 
    \param[in]  lin_num: lin node to be configured
      \arg       LIN1
    \param[out] none
    \retval     none
*/
static void lin_nvic_config(lin_typedef_enum lin_num)
{
     nvic_irq_enable(LIN_IRQn[lin_num], 0U, 0U);
}

/*!
    \brief      peripheral configuration of lin node 
    \param[in]  lin_num: lin node to be configured
      \arg       LIN1
    \param[out] none
    \retval     none
*/
static void lin_periph_config(lin_typedef_enum lin_num)
{
    /* enable USART clock */
    rcu_periph_clock_enable(LIN_CLK_PERIPH[lin_num]);
    
    /* configure USART */
    usart_deinit(LIN_PERIPH[lin_num]);
    usart_baudrate_set(LIN_PERIPH[lin_num], LIN_BAUDRATE[lin_num]);
    usart_word_length_set(LIN_PERIPH[lin_num], USART_WL_8BIT);
    usart_stop_bit_set(LIN_PERIPH[lin_num], USART_STB_1BIT);
    usart_parity_config(LIN_PERIPH[lin_num], USART_PM_NONE);
    usart_receive_config(LIN_PERIPH[lin_num], USART_RECEIVE_ENABLE);
    usart_transmit_config(LIN_PERIPH[lin_num], USART_TRANSMIT_ENABLE);
    usart_hardware_flow_rts_config(LIN_PERIPH[lin_num], USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(LIN_PERIPH[lin_num], USART_CTS_DISABLE);
    
    usart_lin_mode_enable(LIN_PERIPH[lin_num]);
    usart_lin_break_detection_length_config(LIN_PERIPH[lin_num], USART_LBLEN_11B);
    usart_enable(LIN_PERIPH[lin_num]);
    
    usart_interrupt_enable(LIN_PERIPH[lin_num], USART_INT_LBD);
    usart_interrupt_enable(LIN_PERIPH[lin_num], USART_INT_RBNE);
}

/*!
    \brief      send break frame
    \param[in]  lin_num: lin node that to transfer
      \arg       LIN1
    \param[out] none
    \retval     none
*/
static void lin_break_send(lin_typedef_enum lin_num)
{
    usart_command_enable(LIN_PERIPH[lin_num], USART_CMD_SBKCMD);
}

/*!
    \brief      PID calculation
    \param[in]  id: id, bit 0-5 of PID
      \arg       0 - 63
    \param[out] none
    \retval     PID
*/
uint8_t lin_pid_calculate(uint8_t id)
{
    uint8_t pid, p0, p1;
    
    pid = id;
    p0 = (((pid >> 0) ^ (pid >> 1) ^ (pid >> 2) ^ (pid >> 4)) & 0x01) << 6;
    p1 = (~((pid >> 1) ^ (pid >> 3) ^ (pid >> 4) ^ (pid >> 5) & 0x01)) << 7;
    pid |= (p0 | p1);
    
    return pid;
}

/*!
    \brief      checksum calculation
    \param[in]  t_msg: lin message
                  pid: save PID
                  length: message data length
                  data: message data
                  checksum: message checksum
                  error: transmission/reception state
                  task_type: task type of the corresponding id
    \param[out] none
    \retval     PID
*/
uint8_t lin_checksum_calculate(lin_msg *t_msg)
{
    uint8_t i = 0;    
    uint32_t checksum = 0;
    if((FRAME_MASTER_REQ != (t_msg->pid & BITS(0,5))) && (FRAME_SLAVE_RESP != (t_msg->pid & BITS(0,5)))){
        checksum = t_msg->pid;
    }
    
    for(i = 0; i < t_msg->length; i++){
        checksum += t_msg->data[i];
        if(0!= (checksum & 0xFF00)){
            checksum = (checksum & 0xFF) + 1;
        }
    }
    checksum ^= 0xFF;
    
    return checksum;
}

/*!
    \brief      send byte
    \param[in]  lin_num: lin node that to transfer
      \arg       LIN1
    \param[in]  ch: character to be transfer
    \param[out] none
    \retval     none
*/
static void lin_sendbyte(lin_typedef_enum lin_num, uint8_t ch)
{
    usart_data_transmit(LIN_PERIPH[lin_num], ch);
    while(RESET == usart_flag_get(LIN_PERIPH[lin_num], USART_FLAG_TBE));
}

/*!
    \brief      check the readback character with the sent character
    \param[in]  lin_num: lin node that to transfer
      \arg       LIN1
    \param[in]  phase: the phase of the sent character belongs to
      \arg       PHASE_IDLE
      \arg       PHASE_BREAK
      \arg       PHASE_SYNC
      \arg       PHASE_PID
      \arg       PHASE_DATA0
      \arg       PHASE_DATA1
      \arg       PHASE_DATA2
      \arg       PHASE_DATA3
      \arg       PHASE_DATA4
      \arg       PHASE_DATA5
      \arg       PHASE_DATA6
      \arg       PHASE_DATA7
      \arg       PHASE_CHECKSUM
    \param[in]  data: the sent character to be compared
    \param[out] none
    \retval     none
*/
static ErrStatus check_send(lin_typedef_enum lin_num, lin_phase phase, uint8_t data)
{       
    wait_timeout = 15;
    
    /* wait the interrupt received the readback character */
    while((g_lin_node.lin_phase <= phase) && (g_lin_node.lin_phase != PHASE_IDLE)){
        if((0 != g_lin_node.status)||(0 == wait_timeout)){
            return ERROR;
        }
    }

    /* check the readback character with the sent character */
    switch(phase){
        case PHASE_IDLE:
        case PHASE_BREAK:
            break;
        case PHASE_SYNC:
            break;
        case PHASE_PID:
            /* already checked in the interrupt handler */
            if(0 != g_lin_node.status){
                return ERROR;
            }
            break;
        case PHASE_DATA0:
        case PHASE_DATA1:
        case PHASE_DATA2:
        case PHASE_DATA3:
        case PHASE_DATA4:
        case PHASE_DATA5:
        case PHASE_DATA6:
        case PHASE_DATA7:
            /* check the readback data with the sent data */
            if(data != g_lin_node.msg_readback.data[phase-PHASE_DATA0]){
                return ERROR;
            }
            break;
        case PHASE_CHECKSUM:
            /* already checked in the interrupt handler */
            if(0 != g_lin_node.status){
                return ERROR;
            }
            break;
    }
    return SUCCESS;
}

/*!
    \brief      lin node intialization
    \param[in]  lin_num: lin node that to init
      \arg       LIN1
    \param[out] none
    \retval     none
*/
void lin_init(lin_typedef_enum lin_num)
{
    lin_gpio_init(lin_num);
    lin_nvic_config(lin_num);
    lin_periph_config(lin_num);
}

/*!
    \brief      transmit the frame header
    \param[in]  lin_num: lin node that to transfer
      \arg       LIN1
    \param[in]  t_msg: lin message
                  pid: save PID
                  length: message data length
                  data: message data
                  checksum: message checksum
                  error: transmission/reception state
                  task_type: task type of the corresponding id
    \param[out] none
    \retval     ErrStatus: ERROR or SUCCESS
*/
ErrStatus lin_header_transmit(lin_typedef_enum lin_num, lin_msg *t_msg)
{
    t_msg->error = NO_ERR;
    /* send break frame */
    lin_break_send(lin_num);
    if(ERROR == check_send(lin_num, PHASE_BREAK, 0x00)){
        return ERROR;
    }
    /* send SYNC */
    lin_sendbyte(lin_num, 0x55);
    if(ERROR == check_send(lin_num, PHASE_SYNC, 0x55)){
        t_msg->error = SYNC_ERR;
        return ERROR;
    }
    /* send PID */
    lin_sendbyte(lin_num, t_msg->pid);
    if(ERROR == check_send(lin_num, PHASE_PID, t_msg->pid)){
        t_msg->error = PID_ERR;
        return ERROR;
    }
    return SUCCESS;
}

/*!
    \brief      transmit the frame data
    \param[in]  lin_num: lin node that to transfer
      \arg       LIN1
    \param[in]  t_msg: lin message
                  pid: save PID
                  length: message data length
                  data: message data
                  checksum: message checksum
                  error: transmission/reception state
                  task_type: task type of the corresponding id
    \param[out] none
    \retval     ErrStatus: ERROR or SUCCESS
*/
ErrStatus lin_data_transmit(lin_typedef_enum lin_num, lin_msg *t_msg)
{
    uint8_t i = 0;
    
    t_msg->error = DATA_OK;
    
    for(i = 0; i < t_msg->length; i++){
        /* send data */
        lin_sendbyte(lin_num, t_msg->data[i]);
        if(0 == check_send(lin_num, (lin_phase)(PHASE_DATA0+i), t_msg->data[i])){
            t_msg->error = READ_BACK_ERR;
            return ERROR;
        }
    }
    return SUCCESS;
}

/*!
    \brief      transmit the frame checksum
    \param[in]  lin_num: lin node that to transfer
      \arg       LIN1
    \param[in]  t_msg: lin message
                  pid: save PID
                  length: message data length
                  data: message data
                  checksum: message checksum
                  error: transmission/reception state
                  task_type: task type of the corresponding id
    \param[out] none
    \retval     ErrStatus: ERROR or SUCCESS
*/
ErrStatus lin_checksum_transmit(lin_typedef_enum lin_num, lin_msg *t_msg)
{
    uint8_t checksum = 0;

    t_msg->error = CHECKSUM_OK;
    /* calculate the checksum */
    checksum = lin_checksum_calculate(t_msg);
    
    /* send checksum */
    lin_sendbyte(lin_num, checksum);
    if(0 == check_send(lin_num, PHASE_CHECKSUM, checksum)){
        t_msg->error = READ_BACK_ERR;
        return ERROR;
    }
    return SUCCESS;
}
