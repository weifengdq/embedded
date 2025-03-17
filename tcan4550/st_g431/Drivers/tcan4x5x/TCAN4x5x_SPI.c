/*
 * TCAN4x5x_SPI.c
 * Description: This file is responsible for abstracting the lower-level
 * microcontroller SPI read and write functions
 *
 *
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

#include "TCAN4x5x_SPI.h"

#include "bsp_uart.h"
#include "main.h"
#include "spi.h"


// HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, const uint8_t
// *pData, uint16_t Size, uint32_t Timeout); HAL_StatusTypeDef
// HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size,
// uint32_t Timeout); HAL_StatusTypeDef
// HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, const uint8_t *pTxData,
// uint8_t *pRxData,
//                                           uint16_t Size, uint32_t Timeout);

void AHB_WRITE_32(uint16_t address, uint32_t data) {
  HAL_StatusTypeDef status;
  uint8_t dataBuffer[256];
  // uint8_t rx_buf[256];
  // dataBuffer[0] = AHB_WRITE_OPCODE;
  dataBuffer[0] = 0x61;
  dataBuffer[1] = (address >> 8) & 0xFF;
  dataBuffer[2] = address & 0xFF;
  dataBuffer[3] = 0x01;
  dataBuffer[4] = (data >> 24) & 0xFF;
  dataBuffer[5] = (data >> 16) & 0xFF;
  dataBuffer[6] = (data >> 8) & 0xFF;
  dataBuffer[7] = data & 0xFF;
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_RESET);
  
  status = HAL_SPI_Transmit(&hspi1, dataBuffer, 8, 1000);
  // status = HAL_SPI_TransmitReceive(&hspi1, dataBuffer, rx_buf, 8, 1000);
  
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_SET);
  if (status != HAL_OK) {
    print("AHB_WRITE_32 Error: %d\n", (int)status);
  }
}

uint32_t AHB_READ_32(uint16_t address) {
  HAL_StatusTypeDef status;
  uint8_t tx_buf[8];
  uint8_t rx_buf[8];
  uint32_t returnData;
  tx_buf[0] = AHB_READ_OPCODE;
  tx_buf[1] = (address >> 8) & 0xFF;
  tx_buf[2] = address & 0xFF;
  tx_buf[3] = 0x01;
  tx_buf[4] = 0x00;
  tx_buf[5] = 0x00;
  tx_buf[6] = 0x00;
  tx_buf[7] = 0x00;
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_RESET);
  
  status = HAL_SPI_Transmit(&hspi1, tx_buf, 4, 1000);
  if (status != HAL_OK) {
    print("AHB_READ_32 Error 0: %d\n", (int)status);
  }
  status = HAL_SPI_Receive(&hspi1, rx_buf, 4, 1000);
  if (status != HAL_OK) {
    print("AHB_READ_32 Error 1: %d\n", (int)status);
  }
  
  // status = HAL_SPI_TransmitReceive(&hspi1, tx_buf, rx_buf, 8, 1000);
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_SET);
  returnData = ((uint32_t)rx_buf[0] << 24) | ((uint32_t)rx_buf[1] << 16) |
               ((uint32_t)rx_buf[2] << 8) | rx_buf[3];
  return returnData;
}

static uint8_t write_burst_buf[256] = {0};
static uint8_t write_burst_index = 0;
static uint8_t write_burst_rx_buf[256] = {0};
void AHB_WRITE_BURST_START(uint16_t address, uint8_t words) {
  write_burst_buf[0] = 0x61;
  write_burst_buf[1] = (address >> 8) & 0xFF;
  write_burst_buf[2] = address & 0xFF;
  write_burst_buf[3] = words;
  write_burst_index = 4;
}

void AHB_WRITE_BURST_WRITE(uint32_t data) {
  write_burst_buf[write_burst_index++] = (data >> 24) & 0xFF;
  write_burst_buf[write_burst_index++] = (data >> 16) & 0xFF;
  write_burst_buf[write_burst_index++] = (data >> 8) & 0xFF;
  write_burst_buf[write_burst_index++] = data & 0xFF;
}

void AHB_WRITE_BURST_END(void) {
  HAL_StatusTypeDef status;
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_RESET);
  // status = HAL_SPI_Transmit(&hspi1, write_burst_buf, write_burst_index,
  // 1000);
  status = HAL_SPI_TransmitReceive(&hspi1, write_burst_buf, write_burst_rx_buf,
                                   write_burst_index, 1000);
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_SET);
  if (status != HAL_OK) {
    print("AHB_WRITE_BURST_END Error: %d\n", (int)status);
  }
}

void AHB_READ_BURST_START(uint16_t address, uint8_t words) {
  uint8_t tx_buf[4] = {0};
  tx_buf[0] = AHB_READ_OPCODE;
  tx_buf[1] = (address >> 8) & 0xFF;
  tx_buf[2] = address & 0xFF;
  tx_buf[3] = words;
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi1, tx_buf, 4,
                            1000);
  if (status != HAL_OK) {
    print("AHB_READ_BURST_START Error: %d\n", (int)status);
  }
}

uint32_t AHB_READ_BURST_READ(void) {
  HAL_StatusTypeDef status;
  uint32_t returnData;
  uint8_t rx_buf[4] = {0};
  status = HAL_SPI_Receive(&hspi1, rx_buf, 4, 1000);
  if (status != HAL_OK) {
    print("AHB_READ_BURST_READ Error: %d\n", (int)status);
  }
  returnData = ((uint32_t)rx_buf[0] << 24) | ((uint32_t)rx_buf[1] << 16) |
               ((uint32_t)rx_buf[2] << 8) | rx_buf[3];
  return returnData;
}

void AHB_READ_BURST_END(void) {
  // Do nothing
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_SET);
}

// /*
//  * @brief Single word write
//  *
//  * @param address A 16-bit address of the destination register
//  * @param data A 32-bit word of data to write to the destination register
//  */
// void
// AHB_WRITE_32(uint16_t address, uint32_t data)
// {
//     AHB_WRITE_BURST_START(address, 1);
//     AHB_WRITE_BURST_WRITE(data);
//     AHB_WRITE_BURST_END();
// }

// /*
//  * @brief Single word read
//  *
//  * @param address A 16-bit address of the source register
//  *
//  * @return Returns 32-bit word of data from source register
//  */
// uint32_t
// AHB_READ_32(uint16_t address)
// {
//     uint32_t returnData;

//     AHB_READ_BURST_START(address, 1);
//     returnData = AHB_READ_BURST_READ();
//     AHB_READ_BURST_END();

//     return returnData;
// }

// /*
//  * @brief Burst write start
//  *
//  * The SPI transaction contains 3 parts: the header (start), the payload, and
//  the end of data (end)
//  * This function is the start, where the register address and number of words
//  are transmitted
//  *
//  * @param address A 16-bit address of the destination register
//  * @param words The number of 4-byte words that will be transferred. 0 = 256
//  words
//  */
// void
// AHB_WRITE_BURST_START(uint16_t address, uint8_t words)
// {
//     //set the CS low to start the transaction
//     GPIO_setOutputLowOnPin(SPI_CS_GPIO_PORT, SPI_CS_GPIO_PIN);

//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, AHB_WRITE_OPCODE);

//     // Send the 16-bit address
//     WAIT_FOR_TRANSMIT;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, HWREG8(&address + 1));
//     WAIT_FOR_TRANSMIT;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, HWREG8(&address));

//     WAIT_FOR_TRANSMIT;
//     // Send the number of words to read
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, words);

// }

// /*
//  * @brief Burst write
//  *
//  * The SPI transaction contains 3 parts: the header (start), the payload, and
//  the end of data (end)
//  * This function writes a single word at a time
//  *
//  * @param data A 32-bit word of data to write to the destination register
//  */
// void
// AHB_WRITE_BURST_WRITE(uint32_t data)
// {
//     WAIT_FOR_TRANSMIT;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, HWREG8(&data + 3));
//     WAIT_FOR_TRANSMIT;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, HWREG8(&data + 2));
//     WAIT_FOR_TRANSMIT;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, HWREG8(&data + 1));
//     WAIT_FOR_TRANSMIT;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, HWREG8(&data));
// }

// /*
//  * @brief Burst write end
//  *
//  * The SPI transaction contains 3 parts: the header (start), the payload, and
//  the end of data (end)
//  * This function ends the burst transaction by pulling nCS high
//  */
// void
// AHB_WRITE_BURST_END(void)
// {
//     WAIT_FOR_IDLE;
//     GPIO_setOutputHighOnPin(SPI_CS_GPIO_PORT, SPI_CS_GPIO_PIN);
// }

// /*
//  * @brief Burst read start
//  *
//  * The SPI transaction contains 3 parts: the header (start), the payload, and
//  the end of data (end)
//  * This function is the start, where the register address and number of words
//  are transmitted
//  *
//  * @param address A 16-bit start address to begin the burst read
//  * @param words The number of 4-byte words that will be transferred. 0 = 256
//  words
//  */
// void
// AHB_READ_BURST_START(uint16_t address, uint8_t words)
// {
//     // Set the CS low to start the transaction
//     GPIO_setOutputLowOnPin(SPI_CS_GPIO_PORT, SPI_CS_GPIO_PIN);
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, AHB_READ_OPCODE);

//     // Send the 16-bit address
//     WAIT_FOR_TRANSMIT;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, HWREG8(&address + 1));
//     WAIT_FOR_TRANSMIT;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, HWREG8(&address));

//     // Send the number of words to read
//     WAIT_FOR_TRANSMIT;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, words);

// }

// /*
//  * @brief Burst read start
//  *
//  * The SPI transaction contains 3 parts: the header (start), the payload, and
//  the end of data (end)
//  * This function where each word of data is read from the TCAN4x5x
//  *
//  * @return A 32-bit single data word that is read at a time
//  */
// uint32_t
// AHB_READ_BURST_READ(void)
// {
//     uint8_t readData;
//     uint8_t readData1;
//     uint8_t readData2;
//     uint8_t readData3;
//     uint32_t returnData;

//     WAIT_FOR_IDLE;
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, 0x00); // pause after this
//     WAIT_FOR_IDLE;

//     readData = HWREG8(SPI_HW_ADDR + OFS_UCBxRXBUF);
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, 0x00);

//     WAIT_FOR_IDLE;
//     readData1 = HWREG8(SPI_HW_ADDR + OFS_UCBxRXBUF);
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, 0x00);

//     WAIT_FOR_IDLE;
//     readData2 = HWREG8(SPI_HW_ADDR + OFS_UCBxRXBUF);
//     EUSCI_B_SPI_transmitData(SPI_HW_ADDR, 0x00);

//     WAIT_FOR_IDLE;
//     readData3 = HWREG8(SPI_HW_ADDR + OFS_UCBxRXBUF);

//     returnData = (((uint32_t)readData) << 24) | (((uint32_t)readData1 << 16))
//     | (((uint32_t)readData2) << 8) | readData3; return returnData;
// }

// /*
//  * @brief Burst write end
//  *
//  * The SPI transaction contains 3 parts: the header (start), the payload, and
//  the end of data (end)
//  * This function ends the burst transaction by pulling nCS high
//  */
// void
// AHB_READ_BURST_END(void)
// {
//     WAIT_FOR_IDLE;
//     GPIO_setOutputHighOnPin(SPI_CS_GPIO_PORT, SPI_CS_GPIO_PIN);
// }
