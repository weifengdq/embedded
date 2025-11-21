/*!
    \file    lin.h
    \brief   the header file of the lin

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

#ifndef LIN_H
#define LIN_H

#include "gd32a50x.h"

#define FRAME_MASTER_REQ     0x3C
#define FRAME_SLAVE_RESP     0x3D

#define LIN_MASTER           1
#define LIN_SLAVE            2

typedef enum 
{
    LIN0 = 0U,
    LIN1 = 1U,
    LIN2 = 2U,    //here is only one node, user can add another, and also add the information below
}lin_typedef_enum;

#define LINn                 1

/* LIN0 */
#define LIN0_PORT_TX         GPIOA
#define LIN0_PIN_TX          GPIO_PIN_3
#define LIN0_CLK_TX          RCU_GPIOA
#define LIN0_AF_TX           GPIO_AF_5

#define LIN0_PORT_RX         GPIOA
#define LIN0_PIN_RX          GPIO_PIN_4
#define LIN0_CLK_RX          RCU_GPIOA
#define LIN0_AF_RX           GPIO_AF_5

#define LIN0_PERIPH          USART0
#define LIN0_CLK_PERIPH      RCU_USART0
#define LIN0_IRQn            USART0_IRQn
#define LIN0_IRQHandler      USART0_IRQHandler
           
#define LIN0_BAUDRATE        19200

/* LIN1 */
#define LIN1_PORT_TX         GPIOC
#define LIN1_PIN_TX          GPIO_PIN_2
#define LIN1_CLK_TX          RCU_GPIOC
#define LIN1_AF_TX           GPIO_AF_5

#define LIN1_PORT_RX         GPIOC
#define LIN1_PIN_RX          GPIO_PIN_3
#define LIN1_CLK_RX          RCU_GPIOC
#define LIN1_AF_RX           GPIO_AF_5

#define LIN1_PERIPH          USART1
#define LIN1_CLK_PERIPH      RCU_USART1
#define LIN1_IRQn            USART1_IRQn
#define LIN1_IRQHandler      USART1_IRQHandler

#define LIN1_BAUDRATE        19200

/* LIN2 */
#define LIN2_PORT_TX         GPIOE
#define LIN2_PIN_TX          GPIO_PIN_0
#define LIN2_CLK_TX          RCU_GPIOE
#define LIN2_AF_TX           GPIO_AF_5

#define LIN2_PORT_RX         GPIOE
#define LIN2_PIN_RX          GPIO_PIN_1
#define LIN2_CLK_RX          RCU_GPIOE
#define LIN2_AF_RX           GPIO_AF_5

#define LIN2_PERIPH          USART2
#define LIN2_CLK_PERIPH      RCU_USART2
#define LIN2_IRQn            USART2_IRQn
#define LIN2_IRQHandler      USART2_IRQHandler

#define LIN2_BAUDRATE        19200

static const uint32_t LIN_PORT_TX[LINn] = {
    LIN0_PORT_TX,
//    LIN1_PORT_TX,
//    LIN2_PORT_TX,
};
static const uint32_t LIN_PIN_TX[LINn] = {
    LIN0_PIN_TX,
//    LIN1_PIN_TX,
//    LIN2_PIN_TX,
};
static const rcu_periph_enum LIN_CLK_TX[LINn] = {
    LIN0_CLK_TX,
//    LIN1_CLK_TX,
//    LIN2_CLK_TX,
};
static const uint32_t LIN_AF_TX[LINn] = {
    LIN0_AF_TX,
//    LIN1_AF_TX,
//    LIN2_AF_TX,
};
static const uint32_t LIN_PORT_RX[LINn] = {
    LIN0_PORT_RX,
//    LIN1_PORT_RX,
//    LIN2_PORT_RX,
};
static const uint32_t LIN_PIN_RX[LINn] = {
    LIN0_PIN_RX,
//    LIN1_PIN_RX,
//    LIN2_PIN_RX,
};
static const rcu_periph_enum LIN_CLK_RX[LINn] = {
    LIN0_CLK_RX,
//    LIN1_CLK_RX,
//    LIN2_CLK_RX,
};
static const uint32_t LIN_AF_RX[LINn] = {
    LIN0_AF_RX,
//    LIN1_AF_RX,
//    LIN2_AF_RX,
};
static const uint32_t LIN_PERIPH[LINn] = {
    LIN0_PERIPH,
//    LIN1_PERIPH,
//    LIN2_PERIPH,
};
static const rcu_periph_enum LIN_CLK_PERIPH[LINn] = {
    LIN0_CLK_PERIPH,
//    LIN1_CLK_PERIPH,
//    LIN2_CLK_PERIPH,
};
static const uint32_t LIN_IRQn[LINn] = {
    LIN0_IRQn,
//    LIN1_IRQn,
//    LIN2_IRQn,
};
static const uint32_t LIN_BAUDRATE[LINn] = {
    LIN0_BAUDRATE,
//    LIN1_BAUDRATE,
//    LIN2_BAUDRATE,
};

/* lin phase */
typedef enum{
    PHASE_IDLE     = 0,
    PHASE_BREAK    = 1,
    PHASE_SYNC     = 2,
    PHASE_PID      = 3,
    PHASE_DATA0    = 4,
    PHASE_DATA1    = 5,
    PHASE_DATA2    = 6,
    PHASE_DATA3    = 7,
    PHASE_DATA4    = 8,
    PHASE_DATA5    = 9,
    PHASE_DATA6    = 10,
    PHASE_DATA7    = 11,
    PHASE_CHECKSUM = 12
}lin_phase;

/* lin transmission/reception state */
typedef enum{
    NO_ERR        = 0,
    SYNC_OK       = 0x11,
    PID_OK        = 0x12,
    DATA_OK       = 0x13,
    CHECKSUM_OK   = 0x14,
    SYNC_ERR      = 0x21,
    PID_ERR       = 0x22,
    CHECKSUM_ERR  = 0x23,
    READ_BACK_ERR = 0x24
}lin_error_state;

/* task type */
typedef enum{
    TASK_TYPE_RX = 1,                  //master/slave node slave reception task
    TASK_TYPE_TX = 2,                  //master/slave node slave transmission task
    TASK_TYPE_NONE = 3                 //master/slave node no task
}lin_task_type;

/* lin message structure */
typedef struct{
    uint8_t pid;                      //save PID
    uint8_t length;                   //message data length
    uint8_t data[8];                  //message data
    uint8_t checksum;                 //message checksum
    lin_error_state error;            //transmission/reception state
    lin_task_type task_type;          //task type of the corresponding id
}lin_msg;

/* lin node structure */
typedef struct{
    lin_typedef_enum lin_periph;      //peripheral of the lin node
    uint8_t work_mode;                //master or slave node selection
    lin_phase lin_phase;              //lin phase of current id transmission/reception
    uint8_t status;                   //there is error or not in current id transmission/reception
    uint8_t current_id;               //current id of 64 IDs which is in transmission/reception
    lin_msg msg_readback;             //for temp message save
    lin_msg msg_list[64];             //all messages of 64 IDs, including the message to be send, and the data successfully received
}lin_parameter;

/* function declarations */
void lin_init(lin_typedef_enum lin_num);
ErrStatus lin_header_transmit(lin_typedef_enum lin_num, lin_msg *t_msg);
ErrStatus lin_data_transmit(lin_typedef_enum lin_num, lin_msg *t_msg);
ErrStatus lin_checksum_transmit(lin_typedef_enum lin_num, lin_msg *t_msg);
uint8_t lin_pid_calculate(uint8_t id);
uint8_t lin_checksum_calculate(lin_msg *t_msg);
void LIN_IRQHandler(void);

#endif /* LIN_H */
