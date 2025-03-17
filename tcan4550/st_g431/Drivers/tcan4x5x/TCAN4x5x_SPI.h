/*
 * TCAN4x5x_SPI.h
 * Version 1.1
 * Description: This file is responsible for abstracting the lower-level microcontroller SPI read and write functions
 *
 *
 * Change list:
 * 	- 1.1 (6/6/2018)
 * 		- Updated pinout for boosterback support
 *
 * Copyright (c) 2019 Texas Instruments Incorporated.  All rights reserved.
 * Software License Agreement
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * Neither the name of Texas Instruments Incorporated nor the names of
 * its contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TCAN4X5X_SPI_H_
#define TCAN4X5X_SPI_H_

#include <stdint.h>

// #include <driverlib.h>

// // Defines for GPIO port and pin for MSP430 SPI port.
// #define SPI_CS_GPIO_PORT				GPIO_PORT_P2
// #define SPI_CS_GPIO_PIN					GPIO_PIN5
// #define SPI_HW_ADDR						EUSCI_B0_BASE
// // MSP430 Specific commands to proper sequencing on the SPI bus
// #define WAIT_FOR_TRANSMIT				while (!(HWREG16(SPI_HW_ADDR + OFS_UCBxIFG) & UCTXIFG))
// #define WAIT_FOR_IDLE					while ((HWREG16(SPI_HW_ADDR + OFS_UCBxSTATW) & UCBUSY))


// //------------------------------------------------------------------------
// // AHB Access Op Codes
// //------------------------------------------------------------------------
#define AHB_WRITE_OPCODE                            0x61
#define AHB_READ_OPCODE                             0x41




//------------------------------------------------------------------------
//							Write Functions
//------------------------------------------------------------------------

void AHB_WRITE_32(uint16_t address, uint32_t data);
void AHB_WRITE_BURST_START(uint16_t address, uint8_t words);
void AHB_WRITE_BURST_WRITE(uint32_t data);
void AHB_WRITE_BURST_END(void);


//--------------------------------------------------------------------------
//							Read Functions
//--------------------------------------------------------------------------
uint32_t AHB_READ_32(uint16_t address);
void AHB_READ_BURST_START(uint16_t address, uint8_t words);
uint32_t AHB_READ_BURST_READ(void);
void AHB_READ_BURST_END(void);

inline uint32_t tan4x5x_read(uint16_t reg_addr) {
    return AHB_READ_32(reg_addr);
}

#endif
