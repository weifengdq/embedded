;/*!
;    \file    startup_gd32a50x.s
;    \brief   start up file
;
;    \version 2025-08-09, V1.7.0, firmware for GD32A50x
;*/
;
;/*
; * Copyright (c) 2009-2018 Arm Limited. All rights reserved.
; * Copyright (c) 2024 GigaDevice Semiconductor Inc.
; *
; * SPDX-License-Identifier: Apache-2.0
; *
; * Licensed under the Apache License, Version 2.0 (the License); you may
; * not use this file except in compliance with the License.
; * You may obtain a copy of the License at
; *
; * www.apache.org/licenses/LICENSE-2.0
; *
; * Unless required by applicable law or agreed to in writing, software
; * distributed under the License is distributed on an AS IS BASIS, WITHOUT
; * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; * See the License for the specific language governing permissions and
; * limitations under the License.
; */
;
;/* This file refers the CMSIS standard, some adjustments are made according to GigaDevice chips */

        MODULE  ?cstartup

        ;; Forward declaration of sections.
        SECTION CSTACK:DATA:NOROOT(3)

        SECTION .intvec:CODE:NOROOT(2)

        EXTERN  __iar_program_start
        EXTERN  SystemInit
        PUBLIC  __vector_table

        DATA
__vector_table
        DCD     sfe(CSTACK)                         ; top of stack
                DCD     Reset_Handler                     ; Reset Handler
                DCD     NMI_Handler                       ; NMI Handler
                DCD     HardFault_Handler                 ; Hard Fault Handler
                DCD     MemManage_Handler                 ; MPU Fault Handler
                DCD     BusFault_Handler                  ; Bus Fault Handler
                DCD     UsageFault_Handler                ; Usage Fault Handler
                DCD     0                                 ; Reserved
                DCD     0                                 ; Reserved
                DCD     0                                 ; Reserved
                DCD     0                                 ; Reserved
                DCD     SVC_Handler                       ; SVCall Handler
                DCD     DebugMon_Handler                  ; Debug Monitor Handler
                DCD     0                                 ; Reserved
                DCD     PendSV_Handler                    ; PendSV Handler
                DCD     SysTick_Handler                   ; SysTick Handler

;               /* external interrupts handler */
                DCD     WWDGT_IRQHandler                  ; 16:Window Watchdog Timer
                DCD     LVD_IRQHandler                    ; 17:LVD through EXTI Line detect
                DCD     0                                 ; 18:Reserved
                DCD     RTC_IRQHandler                    ; 19:RTC
                DCD     FMC_IRQHandler                    ; 20:FMC
                DCD     RCU_IRQHandler                    ; 21:RCU
                DCD     EXTI0_IRQHandler                  ; 22:EXTI Line 0
                DCD     EXTI1_IRQHandler                  ; 23:EXTI Line 1
                DCD     EXTI2_IRQHandler                  ; 24:EXTI Line 2
                DCD     EXTI3_IRQHandler                  ; 25:EXTI Line 3
                DCD     EXTI4_IRQHandler                  ; 26:EXTI Line 4
                DCD     DMA0_Channel0_IRQHandler          ; 27:DMA0 Channel0
                DCD     DMA0_Channel1_IRQHandler          ; 28:DMA0 Channel1
                DCD     DMA0_Channel2_IRQHandler          ; 29:DMA0 Channel2
                DCD     DMA0_Channel3_IRQHandler          ; 30:DMA0 Channel3
                DCD     DMA0_Channel4_IRQHandler          ; 31:DMA0 Channel4
                DCD     DMA0_Channel5_IRQHandler          ; 32:DMA0 Channel5
                DCD     DMA0_Channel6_IRQHandler          ; 33:DMA0 Channel6
                DCD     ADC0_1_IRQHandler                 ; 34:ADC0 and ADC1
                DCD     CAN0_Message_IRQHandler           ; 35:CAN0 Interrupt for message buffer
                DCD     CAN0_Busoff_IRQHandler            ; 36:CAN0 Interrupt for bus off/bus off done
                DCD     CAN0_Error_IRQHandler             ; 37:CAN0 Interrupt for error
                DCD     CAN0_FastError_IRQHandler         ; 38:CAN0 Interrupt for error in fast transmission
                DCD     CAN0_TEC_IRQHandler               ; 39:CAN0 Interrupt for transmit warning
                DCD     CAN0_REC_IRQHandler               ; 40:CAN0 Interrupt for receive warning
                DCD     CAN0_WKUP_IRQHandler              ; 41:CAN0 wakeup through EXTI Line detection interrupt
                DCD     TIMER0_BRK_UP_TRG_CMT_IRQHandler  ; 42:TIMER0 Break Update Trigger and Commutation
                DCD     TIMER0_Channel_IRQHandler         ; 43:TIMER0 Channel Capture Compare
                DCD     TIMER1_IRQHandler                 ; 44:TIMER1
                DCD     TIMER19_BRK_UP_TRG_CMT_IRQHandler ; 45:TIMER19 Break Update Trigger and Commutation
                DCD     TIMER19_Channel_IRQHandler        ; 46:TIMER19 Channel Capture Compare
                DCD     I2C0_EV_IRQHandler                ; 47:I2C0 Event
                DCD     I2C0_ER_IRQHandler                ; 48:I2C0 Error
                DCD     I2C1_EV_IRQHandler                ; 49:I2C1 Event
                DCD     I2C1_ER_IRQHandler                ; 50:I2C1 Error
                DCD     SPI0_IRQHandler                   ; 51:SPI0
                DCD     SPI1_IRQHandler                   ; 52:SPI1
                DCD     USART0_IRQHandler                 ; 53:USART0
                DCD     USART1_IRQHandler                 ; 54:USART1
                DCD     USART2_IRQHandler                 ; 55:USART2
                DCD     EXTI10_15_IRQHandler              ; 56:EXTI Line10-15
                DCD     EXTI5_9_IRQHandler                ; 57:EXTI Line5-9
                DCD     TAMPER_IRQHandler                 ; 58:BKP Tamper
                DCD     TIMER20_BRK_UP_TRG_CMT_IRQHandler ; 59:TIMER20 Break Update Trigger and Commutation
                DCD     TIMER20_Channel_IRQHandler        ; 60:TIMER20 Channel Capture Compare
                DCD     TIMER7_BRK_UP_TRG_CMT_IRQHandler  ; 61:TIMER7 Break Update Trigger and Commutation
                DCD     TIMER7_Channel_IRQHandler         ; 62:TIMER7 Channel Capture Compare
                DCD     DMAMUX_IRQHandler                 ; 63:DMANUX
                DCD     SRAMC_ECCSE_IRQHandler            ; 64:Syscfg interrupt(sramc eccse)
                DCD     CMP_IRQHandler                    ; 65:CMP through EXTI Line
                DCD     0                                 ; 66:Reserved
                DCD     OVD_IRQHandler                    ; 67:OVD
                DCD     0                                 ; 68:Reserved
                DCD     0                                 ; 69:Reserved
                DCD     TIMER5_DAC_IRQHandler            ; 70:TIMER5 and DAC
                DCD     TIMER6_IRQHandler                 ; 71:TIMER6
                DCD     DMA1_Channel0_IRQHandler          ; 72:DMA1 Channel0
                DCD     DMA1_Channel1_IRQHandler          ; 73:DMA1 Channel1
                DCD     DMA1_Channel2_IRQHandler          ; 74:DMA1 Channel2
                DCD     DMA1_Channel3_IRQHandler          ; 75:DMA1 Channel3
                DCD     DMA1_Channel4_IRQHandler          ; 76:DMA1 Channel4
                DCD     0                                 ; 77:Reserved
                DCD     CAN1_WKUP_IRQHandler              ; 78:CAN1 wakeup through EXTI Line detection interrupt
                DCD     CAN1_Message_IRQHandler           ; 79:CAN1 Interrupt for message buffer
                DCD     CAN1_Busoff_IRQHandler            ; 80:CAN1 Interrupt for bus off/bus off done
                DCD     CAN1_Error_IRQHandler             ; 81:CAN1 Interrupt for error
                DCD     CAN1_FastError_IRQHandler         ; 82:CAN1 Interrupt for error in fast transmission
                DCD     CAN1_TEC_IRQHandler               ; 83:CAN1 Interrupt for transmit warning
                DCD     CAN1_REC_IRQHandler               ; 84:CAN1 Interrupt for receive warning
                DCD     FPU_IRQHandler                    ; 85:FPU
                DCD     MFCOM_IRQHandler                  ; 86:MFCOM

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Default interrupt handlers.
;;
        THUMB

        PUBWEAK Reset_Handler
        SECTION .text:CODE:NOROOT:REORDER(2)
Reset_Handler

                LDR     r0, =0x1FFFF7E0
                LDR     r2, [r0]
                LDR     r0, = 0xFFFF0000
                AND     r2, r2, r0
                LSR     r2, r2, #16
                LSL     r2, r2, #10
                LDR     r1, =0x20000000
                MOV     r0, #0x00
SRAM_INIT       STM     r1!, {r0}
                SUBS    r2, r2, #4
                CMP     r2, #0x00
                BNE     SRAM_INIT                

        LDR     R0, =SystemInit
        BLX     R0
        LDR     R0, =__iar_program_start
        BX      R0

        PUBWEAK NMI_Handler
        SECTION .text:CODE:NOROOT:REORDER(1)
NMI_Handler
        B NMI_Handler

        PUBWEAK HardFault_Handler
        SECTION .text:CODE:NOROOT:REORDER(1)
HardFault_Handler
        B HardFault_Handler      

        PUBWEAK MemManage_Handler
        SECTION .text:CODE:NOROOT:REORDER(1)
MemManage_Handler
        B MemManage_Handler

        PUBWEAK BusFault_Handler
        SECTION .text:CODE:NOROOT:REORDER(1)
BusFault_Handler
        B BusFault_Handler

        PUBWEAK UsageFault_Handler
        SECTION .text:CODE:NOROOT:REORDER(1)
UsageFault_Handler
        B UsageFault_Handler

        PUBWEAK SVC_Handler
        SECTION .text:CODE:NOROOT:REORDER(1)
SVC_Handler
        B SVC_Handler
        
        PUBWEAK DebugMon_Handler
        SECTION .text:CODE:NOROOT:REORDER(1)
DebugMon_Handler
        B DebugMon_Handler

        PUBWEAK PendSV_Handler
        SECTION .text:CODE:NOROOT:REORDER(1)
PendSV_Handler
        B PendSV_Handler

        PUBWEAK SysTick_Handler
        SECTION .text:CODE:NOROOT:REORDER(1)
SysTick_Handler
        B SysTick_Handler

        PUBWEAK WWDGT_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
WWDGT_IRQHandler
        B WWDGT_IRQHandler

        PUBWEAK LVD_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
LVD_IRQHandler
        B LVD_IRQHandler

        PUBWEAK RTC_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
RTC_IRQHandler
        B RTC_IRQHandler
        
        PUBWEAK FMC_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
FMC_IRQHandler
        B FMC_IRQHandler

        PUBWEAK RCU_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
RCU_IRQHandler
        B RCU_IRQHandler

        PUBWEAK EXTI0_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
EXTI0_IRQHandler
        B EXTI0_IRQHandler

        PUBWEAK EXTI1_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
EXTI1_IRQHandler
        B EXTI1_IRQHandler

        PUBWEAK EXTI2_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
EXTI2_IRQHandler
        B EXTI2_IRQHandler

        PUBWEAK EXTI3_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
EXTI3_IRQHandler
        B EXTI3_IRQHandler 
 
        PUBWEAK EXTI4_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
EXTI4_IRQHandler 
        B EXTI4_IRQHandler 

        PUBWEAK DMA0_Channel0_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA0_Channel0_IRQHandler
        B DMA0_Channel0_IRQHandler

        PUBWEAK DMA0_Channel1_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA0_Channel1_IRQHandler
        B DMA0_Channel1_IRQHandler

        PUBWEAK DMA0_Channel2_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA0_Channel2_IRQHandler
        B DMA0_Channel2_IRQHandler

        PUBWEAK DMA0_Channel3_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA0_Channel3_IRQHandler
        B DMA0_Channel3_IRQHandler

        PUBWEAK DMA0_Channel4_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA0_Channel4_IRQHandler
        B DMA0_Channel4_IRQHandler

        PUBWEAK DMA0_Channel5_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA0_Channel5_IRQHandler
        B DMA0_Channel5_IRQHandler

        PUBWEAK DMA0_Channel6_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA0_Channel6_IRQHandler
        B DMA0_Channel6_IRQHandler

        PUBWEAK ADC0_1_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
ADC0_1_IRQHandler
        B ADC0_1_IRQHandler
        
        PUBWEAK CAN0_Message_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN0_Message_IRQHandler
        B CAN0_Message_IRQHandler

        PUBWEAK CAN0_Busoff_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN0_Busoff_IRQHandler
        B CAN0_Busoff_IRQHandler
        
        PUBWEAK CAN0_Error_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN0_Error_IRQHandler
        B CAN0_Error_IRQHandler

        PUBWEAK CAN0_FastError_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN0_FastError_IRQHandler
        B CAN0_FastError_IRQHandler
        
        PUBWEAK CAN0_TEC_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN0_TEC_IRQHandler
        B CAN0_TEC_IRQHandler
        
        PUBWEAK CAN0_REC_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN0_REC_IRQHandler
        B CAN0_REC_IRQHandler

        PUBWEAK CAN0_WKUP_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN0_WKUP_IRQHandler
        B CAN0_WKUP_IRQHandler
        
        PUBWEAK TIMER0_BRK_UP_TRG_CMT_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER0_BRK_UP_TRG_CMT_IRQHandler
        B TIMER0_BRK_UP_TRG_CMT_IRQHandler
        
        PUBWEAK TIMER0_Channel_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER0_Channel_IRQHandler
        B TIMER0_Channel_IRQHandler

        PUBWEAK TIMER1_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER1_IRQHandler
        B TIMER1_IRQHandler

        PUBWEAK TIMER19_BRK_UP_TRG_CMT_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER19_BRK_UP_TRG_CMT_IRQHandler
        B TIMER19_BRK_UP_TRG_CMT_IRQHandler

        PUBWEAK TIMER19_Channel_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER19_Channel_IRQHandler
        B TIMER19_Channel_IRQHandler
        
        PUBWEAK I2C0_EV_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
I2C0_EV_IRQHandler
        B I2C0_EV_IRQHandler

        PUBWEAK I2C0_ER_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
I2C0_ER_IRQHandler
        B I2C0_ER_IRQHandler

        PUBWEAK I2C1_EV_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
I2C1_EV_IRQHandler
        B I2C1_EV_IRQHandler

        PUBWEAK I2C1_ER_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
I2C1_ER_IRQHandler
        B I2C1_ER_IRQHandler
        
        PUBWEAK SPI0_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
SPI0_IRQHandler
        B SPI0_IRQHandler

        PUBWEAK SPI1_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
SPI1_IRQHandler
        B SPI1_IRQHandler        
        
        PUBWEAK USART0_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
USART0_IRQHandler
        B USART0_IRQHandler

        PUBWEAK USART1_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
USART1_IRQHandler
        B USART1_IRQHandler

        PUBWEAK USART2_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
USART2_IRQHandler
        B USART2_IRQHandler

        PUBWEAK EXTI10_15_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
EXTI10_15_IRQHandler
        B EXTI10_15_IRQHandler
        
        PUBWEAK EXTI5_9_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
EXTI5_9_IRQHandler
        B EXTI5_9_IRQHandler        
        
        PUBWEAK TAMPER_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TAMPER_IRQHandler
        B TAMPER_IRQHandler
               
        PUBWEAK TIMER20_BRK_UP_TRG_CMT_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER20_BRK_UP_TRG_CMT_IRQHandler
        B TIMER20_BRK_UP_TRG_CMT_IRQHandler        
        
        PUBWEAK TIMER20_Channel_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER20_Channel_IRQHandler
        B TIMER20_Channel_IRQHandler         
        
        PUBWEAK TIMER7_BRK_UP_TRG_CMT_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER7_BRK_UP_TRG_CMT_IRQHandler
        B TIMER7_BRK_UP_TRG_CMT_IRQHandler 

        PUBWEAK TIMER7_Channel_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER7_Channel_IRQHandler
        B TIMER7_Channel_IRQHandler 

        PUBWEAK DMAMUX_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMAMUX_IRQHandler
        B DMAMUX_IRQHandler

        PUBWEAK SRAMC_ECCSE_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
SRAMC_ECCSE_IRQHandler
        B SRAMC_ECCSE_IRQHandler

        PUBWEAK CMP_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CMP_IRQHandler
        B CMP_IRQHandler
        
        PUBWEAK OVD_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
OVD_IRQHandler
        B OVD_IRQHandler               

        PUBWEAK TIMER5_DAC_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER5_DAC_IRQHandler
        B TIMER5_DAC_IRQHandler

        PUBWEAK TIMER6_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
TIMER6_IRQHandler
        B TIMER6_IRQHandler

        PUBWEAK DMA1_Channel0_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA1_Channel0_IRQHandler
        B DMA1_Channel0_IRQHandler
        
        PUBWEAK DMA1_Channel1_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA1_Channel1_IRQHandler
        B DMA1_Channel1_IRQHandler     
        
        PUBWEAK DMA1_Channel2_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA1_Channel2_IRQHandler
        B DMA1_Channel2_IRQHandler   

        PUBWEAK DMA1_Channel3_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA1_Channel3_IRQHandler
        B DMA1_Channel3_IRQHandler 

        PUBWEAK DMA1_Channel4_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
DMA1_Channel4_IRQHandler
        B DMA1_Channel4_IRQHandler 

        PUBWEAK CAN1_WKUP_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN1_WKUP_IRQHandler
        B CAN1_WKUP_IRQHandler

        PUBWEAK CAN1_Message_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN1_Message_IRQHandler
        B CAN1_Message_IRQHandler

        PUBWEAK CAN1_Busoff_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN1_Busoff_IRQHandler
        B CAN1_Busoff_IRQHandler
        
        PUBWEAK CAN1_Error_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN1_Error_IRQHandler
        B CAN1_Error_IRQHandler

        PUBWEAK CAN1_FastError_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN1_FastError_IRQHandler
        B CAN1_FastError_IRQHandler
        
        PUBWEAK CAN1_TEC_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN1_TEC_IRQHandler
        B CAN1_TEC_IRQHandler
        
        PUBWEAK CAN1_REC_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
CAN1_REC_IRQHandler
        B CAN1_REC_IRQHandler

        PUBWEAK FPU_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
FPU_IRQHandler
        B FPU_IRQHandler

        PUBWEAK MFCOM_IRQHandler
        SECTION .text:CODE:NOROOT:REORDER(1)
MFCOM_IRQHandler
        B MFCOM_IRQHandler

        END