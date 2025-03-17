#ifndef BSP_TCAN4X5X_H
#define BSP_TCAN4X5X_H

#include "bsp_uart.h"
#include "gpio.h"
#include "main.h"
#include "spi.h"
#include <stdint.h>


#define TCAN4x5x_SPI hspi1
#define TCAN4x5x_SPI_TIMEOUT 1000
#define TCAN4x5x_Debug print

inline void spi1_cs_high() {
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_SET);
}

inline void spi1_cs_low() {
  HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_RESET);
}

// inline void bsp_tcan4x5x_init(void) {
//   // MX_SPI1_Init();
// }

inline void bsp_tcan4x5x_write_32(uint16_t address, uint32_t data) {
  spi1_cs_low();
  HAL_StatusTypeDef status;
  uint8_t dataBuffer[256];
  dataBuffer[0] = 0x61;
  dataBuffer[1] = (address >> 8) & 0xFF;
  dataBuffer[2] = address & 0xFF;
  dataBuffer[3] = 0x01;
  dataBuffer[4] = (data >> 24) & 0xFF;
  dataBuffer[5] = (data >> 16) & 0xFF;
  dataBuffer[6] = (data >> 8) & 0xFF;
  dataBuffer[7] = data & 0xFF;
  status = HAL_SPI_Transmit(&TCAN4x5x_SPI, dataBuffer, 8, TCAN4x5x_SPI_TIMEOUT);
  if (status != HAL_OK) {
    TCAN4x5x_Debug("AHB_WRITE_32 Error: %d\n", (int)status);
  }
  spi1_cs_high();
}

uint32_t bsp_tcan4x5x_read_32(uint16_t address);

void bsp_tcan4x5x_init(void);
void bsp_tcan4x5x_process(void);

#endif // BSP_TCAN4X5X_H