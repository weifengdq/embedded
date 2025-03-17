#include "bsp_tcan4x5x.h"
#include "TCAN4550.h"
#include "bsp_uart.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static void tcan4x5x_mcan_config(uint32_t baud, float sample_point,
                                 uint32_t data_baud, float data_sample_point);
static void tcan4x5x_mcan_write_test(void);
static bool is_busoff_or_error = false;
static uint32_t busoff_or_error_count = 0;

uint32_t bsp_tcan4x5x_read_32(uint16_t address) {
  HAL_StatusTypeDef status;
  uint8_t tx_buf[8];
  uint8_t rx_buf[8];
  uint32_t returnData;
  spi1_cs_low();
  tx_buf[0] = 0x41;
  tx_buf[1] = (address >> 8) & 0xFF;
  tx_buf[2] = address & 0xFF;
  tx_buf[3] = 0x01;
  tx_buf[4] = 0x00;
  tx_buf[5] = 0x00;
  tx_buf[6] = 0x00;
  tx_buf[7] = 0x00;

  status = HAL_SPI_Transmit(&TCAN4x5x_SPI, tx_buf, 4, TCAN4x5x_SPI_TIMEOUT);

  if (status != HAL_OK) {
    TCAN4x5x_Debug("AHB_READ_32 Error 0: %d\n", (int)status);
  }

  status = HAL_SPI_Receive(&TCAN4x5x_SPI, rx_buf, 4, TCAN4x5x_SPI_TIMEOUT);

  if (status != HAL_OK) {
    TCAN4x5x_Debug("AHB_READ_32 Error 1: %d\n", (int)status);
  }

  // spi1_cs_high();
  // HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_SET);
  returnData = ((uint32_t)rx_buf[0] << 24) | ((uint32_t)rx_buf[1] << 16) |
               ((uint32_t)rx_buf[2] << 8) | rx_buf[3];
  spi1_cs_high();
  return returnData;
}

void bsp_tcan4x5x_reset(void) {
  HAL_GPIO_WritePin(TCAN_RST_GPIO_Port, TCAN_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(TCAN_RST_GPIO_Port, TCAN_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(1);
}

void bsp_tcan4x5x_init(void) {
  TCAN4x5x_Debug(
      "\r\n====================================================\r\n");
  bsp_tcan4x5x_reset();
  // bsp_tcan4x5x_write_32(0x000C, 0xFFFFFFFF);
  // bsp_tcan4x5x_write_32(0x000C, 0xFFFFFFFF);
  // HAL_Delay(100);
  // uint32_t id0 = bsp_tcan4x5x_read_32(0x0000);
  // print("TCAN4x5x Device ID: 0x%08X\n", id0);
  // uint32_t id1 = bsp_tcan4x5x_read_32(0x0004);
  // print("TCAN4x5x Device ID: 0x%08X\n", id1);
  // char id[8];
  // memcpy(id, &id0, 4);
  // memcpy(id + 4, &id1, 4);
  // print("TCAN4x5x Device ID: %s\n", id);
  // uint32_t rev = bsp_tcan4x5x_read_32(0x0008);
  // print("TCAN4x5x Device Revision: 0x%08X\n", rev);
  // uint32_t status = bsp_tcan4x5x_read_32(0x000C);
  // print("TCAN4x5x Device Status: 0x%08X\n", status);
  // AHB_WRITE_32(REG_SPI_STATUS, 0xFFFFFFFF);
  // bsp_tcan4x5x_write_32(0x000C, 0xFFFFFFFF);
  // read reg 0x0000
  // uint32_t data = bsp_tcan4x5x_read_32(0x0000);
  // print("TCAN4x5x Device ID: 0x%08X\n", data);
  // HAL_Delay(1000);
  // data = bsp_tcan4x5x_read_32(0x0000);
  // print("TCAN4x5x Device ID: 0x%08X\n", data);

  // typedef enum { TCAN4x5x_WDT_60MS, TCAN4x5x_WDT_600MS, TCAN4x5x_WDT_3S,
  // TCAN4x5x_WDT_6S } TCAN4x5x_WDT_Timer_Enum; extern TCAN4x5x_WDT_Timer_Enum
  // TCAN4x5x_WDT_Read(void);
  TCAN4x5x_WDT_Timer_Enum wdt = TCAN4x5x_WDT_Read();
  TCAN4x5x_Debug("TCAN4x5x Watchdog Timer: %d, %s\n", wdt,
                 wdt == TCAN4x5x_WDT_60MS    ? "60ms"
                 : wdt == TCAN4x5x_WDT_600MS ? "600ms"
                 : wdt == TCAN4x5x_WDT_3S    ? "3S"
                                             : "6S");
  /*
  TCAN4x5x Watchdog Timer: 0, 60ms
  */
  //  extern bool TCAN4x5x_WDT_Disable(void);
  if (!TCAN4x5x_WDT_Disable()) {
    TCAN4x5x_Debug("Failed to disable watchdog\n");
  }

  // extern uint16_t TCAN4x5x_Device_ReadDeviceVersion(void);
  uint16_t version = TCAN4x5x_Device_ReadDeviceVersion();
  TCAN4x5x_Debug("TCAN4x5x Device Version: 0x%04X\n", version);

  // void TCAN4x5x_Device_ReadConfig(TCAN4x5x_DEV_CONFIG *devCfg);
  TCAN4x5x_DEV_CONFIG devCfg;
  TCAN4x5x_Device_ReadConfig(&devCfg);
  TCAN4x5x_Debug("TCAN4x5x Device Config: 0x%08X\n", devCfg.word);
  TCAN4x5x_Debug("    SWE_DIS: %d\n", devCfg.SWE_DIS);
  TCAN4x5x_Debug("    DEVICE_RESET: %d\n", devCfg.DEVICE_RESET);
  TCAN4x5x_Debug("    WD_EN: %d\n", devCfg.WD_EN);
  TCAN4x5x_Debug("    nWKRQ_CONFIG: %d\n", devCfg.nWKRQ_CONFIG);
  TCAN4x5x_Debug("    INH_DIS: %d\n", devCfg.INH_DIS);
  TCAN4x5x_Debug("    GPIO1_CONFIG: %d\n", devCfg.GPIO1_CONFIG);
  TCAN4x5x_Debug("    FAIL_SAFE_EN: %d\n", devCfg.FAIL_SAFE_EN);
  TCAN4x5x_Debug("    WD_ACTION: %d\n", devCfg.WD_ACTION);
  TCAN4x5x_Debug("    WD_BIT_RESET: %d\n", devCfg.WD_BIT_RESET);
  TCAN4x5x_Debug("    nWKRQ_VOLTAGE: %d\n", devCfg.nWKRQ_VOLTAGE);
  TCAN4x5x_Debug("    GPO2_CONFIG: %d\n", devCfg.GPO2_CONFIG);
  TCAN4x5x_Debug("    CLK_REF: %d\n", devCfg.CLK_REF);
  TCAN4x5x_Debug("    WAKE_CONFIG: %d\n", devCfg.WAKE_CONFIG);
  /*
  TCAN4x5x Device Config: 0xC8000468
    SWE_DIS: 0
    DEVICE_RESET: 0
    WD_EN: 1
    nWKRQ_CONFIG: 0
    INH_DIS: 0
    GPIO1_CONFIG: 0
    FAIL_SAFE_EN: 0
    WD_ACTION: 0
    WD_BIT_RESET: 0
    nWKRQ_VOLTAGE: 0
    GPO2_CONFIG: 0
    CLK_REF: 1
    WAKE_CONFIG: 3
  */
  // extern bool TCAN4x5x_Device_Configure(TCAN4x5x_DEV_CONFIG *devCfg);
  // SWE_DIS set to 1
  devCfg.SWE_DIS = 1;
  if (!TCAN4x5x_Device_Configure(&devCfg)) {
    TCAN4x5x_Debug("Failed to configure device\n");
  }
  // read again
  TCAN4x5x_Device_ReadConfig(&devCfg);
  TCAN4x5x_Debug("TCAN4x5x Device Config: 0x%08X\n", devCfg.word);
  TCAN4x5x_Debug("    SWE_DIS: %d\n", devCfg.SWE_DIS);
  TCAN4x5x_Debug("    DEVICE_RESET: %d\n", devCfg.DEVICE_RESET);
  TCAN4x5x_Debug("    WD_EN: %d\n", devCfg.WD_EN);
  TCAN4x5x_Debug("    nWKRQ_CONFIG: %d\n", devCfg.nWKRQ_CONFIG);
  TCAN4x5x_Debug("    INH_DIS: %d\n", devCfg.INH_DIS);
  TCAN4x5x_Debug("    GPIO1_CONFIG: %d\n", devCfg.GPIO1_CONFIG);
  TCAN4x5x_Debug("    FAIL_SAFE_EN: %d\n", devCfg.FAIL_SAFE_EN);
  TCAN4x5x_Debug("    WD_ACTION: %d\n", devCfg.WD_ACTION);
  TCAN4x5x_Debug("    WD_BIT_RESET: %d\n", devCfg.WD_BIT_RESET);
  TCAN4x5x_Debug("    nWKRQ_VOLTAGE: %d\n", devCfg.nWKRQ_VOLTAGE);
  TCAN4x5x_Debug("    GPO2_CONFIG: %d\n", devCfg.GPO2_CONFIG);
  TCAN4x5x_Debug("    CLK_REF: %d\n", devCfg.CLK_REF);
  TCAN4x5x_Debug("    WAKE_CONFIG: %d\n", devCfg.WAKE_CONFIG);

  // extern void TCAN4x5x_Device_ReadInterrupts(TCAN4x5x_Device_Interrupts *ir);
  TCAN4x5x_Device_Interrupts ir;
  TCAN4x5x_Device_ReadInterrupts(&ir);
  TCAN4x5x_Debug("TCAN4x5x Device Interrupts: 0x%08X\n", ir.word);
  TCAN4x5x_Debug("    VTWD: %d\n", ir.VTWD);
  TCAN4x5x_Debug("    M_CAN_INT: %d\n", ir.M_CAN_INT);
  TCAN4x5x_Debug("    SWERR: %d\n", ir.SWERR);
  TCAN4x5x_Debug("    SPIERR: %d\n", ir.SPIERR);
  TCAN4x5x_Debug("    CBF: %d\n", ir.CBF);
  TCAN4x5x_Debug("    CANERR: %d\n", ir.CANERR);
  TCAN4x5x_Debug("    WKRQ: %d\n", ir.WKRQ);
  TCAN4x5x_Debug("    GLOBALERR: %d\n", ir.GLOBALERR);
  TCAN4x5x_Debug("    CANDOM: %d\n", ir.CANDOM);
  TCAN4x5x_Debug("    RESERVED: %d\n", ir.RESERVED);
  TCAN4x5x_Debug("    CANTO: %d\n", ir.CANTO);
  TCAN4x5x_Debug("    RESERVED2: %d\n", ir.RESERVED2);
  TCAN4x5x_Debug("    FRAME_OVF: %d\n", ir.FRAME_OVF);
  TCAN4x5x_Debug("    WKERR: %d\n", ir.WKERR);
  TCAN4x5x_Debug("    LWU: %d\n", ir.LWU);
  TCAN4x5x_Debug("    CANINT: %d\n", ir.CANINT);
  TCAN4x5x_Debug("    ECCERR: %d\n", ir.ECCERR);
  TCAN4x5x_Debug("    RESERVED3: %d\n", ir.RESERVED3);
  TCAN4x5x_Debug("    WDTO: %d\n", ir.WDTO);
  TCAN4x5x_Debug("    TSD: %d\n", ir.TSD);
  TCAN4x5x_Debug("    PWRON: %d\n", ir.PWRON);
  TCAN4x5x_Debug("    UVIO: %d\n", ir.UVIO);
  TCAN4x5x_Debug("    UVSUP: %d\n", ir.UVSUP);
  TCAN4x5x_Debug("    SMS: %d\n", ir.SMS);
  TCAN4x5x_Debug("    CANBUSBAT: %d\n", ir.CANBUSBAT);
  TCAN4x5x_Debug("    CANBUSGND: %d\n", ir.CANBUSGND);
  TCAN4x5x_Debug("    CANBUSOPEN: %d\n", ir.CANBUSOPEN);
  TCAN4x5x_Debug("    CANLGND: %d\n", ir.CANLGND);
  TCAN4x5x_Debug("    CANHBAT: %d\n", ir.CANHBAT);
  TCAN4x5x_Debug("    CANHCANL: %d\n", ir.CANHCANL);
  TCAN4x5x_Debug("    CANBUSTERMOPEN: %d\n", ir.CANBUSTERMOPEN);
  TCAN4x5x_Debug("    CANBUSNORM: %d\n", ir.CANBUSNORM);
  /*
  TCAN4x5x Device Interrupts: 0x00100000
    VTWD: 0
    M_CAN_INT: 0
    SWERR: 0
    SPIERR: 0
    CBF: 0
    CANERR: 0
    WKRQ: 0
    GLOBALERR: 0
    CANDOM: 0
    RESERVED: 0
    CANTO: 0
    RESERVED2: 0
    FRAME_OVF: 0
    WKERR: 0
    LWU: 0
    CANINT: 0
    ECCERR: 0
    RESERVED3: 0
    WDTO: 0
    TSD: 0
    PWRON: 1
    UVIO: 0
    UVSUP: 0
    SMS: 0
    CANBUSBAT: 0
    CANBUSGND: 0
    CANBUSOPEN: 0
    CANLGND: 0
    CANHBAT: 0
    CANHCANL: 0
    CANBUSTERMOPEN: 0
    CANBUSNORM: 0
  */

  // extern void
  // TCAN4x5x_Device_ReadInterruptEnable(TCAN4x5x_Device_Interrupt_Enable *ie);
  TCAN4x5x_Device_Interrupt_Enable ie;
  TCAN4x5x_Device_ReadInterruptEnable(&ie);
  TCAN4x5x_Debug("TCAN4x5x Device Interrupt Enable: 0x%08X\n", ie.word);
  TCAN4x5x_Debug("    RESERVED1: %d\n", ie.RESERVED1);
  TCAN4x5x_Debug("    CANDOMEN: %d\n", ie.CANDOMEN);
  TCAN4x5x_Debug("    RESERVED2: %d\n", ie.RESERVED2);
  TCAN4x5x_Debug("    CANTOEN: %d\n", ie.CANTOEN);
  TCAN4x5x_Debug("    RESERVED3: %d\n", ie.RESERVED3);
  TCAN4x5x_Debug("    FRAME_OVFEN: %d\n", ie.FRAME_OVFEN);
  TCAN4x5x_Debug("    WKERREN: %d\n", ie.WKERREN);
  TCAN4x5x_Debug("    LWUEN: %d\n", ie.LWUEN);
  TCAN4x5x_Debug("    CANINTEN: %d\n", ie.CANINTEN);
  TCAN4x5x_Debug("    ECCERREN: %d\n", ie.ECCERREN);
  TCAN4x5x_Debug("    RESERVED4: %d\n", ie.RESERVED4);
  TCAN4x5x_Debug("    WDTOEN: %d\n", ie.WDTOEN);
  TCAN4x5x_Debug("    TSDEN: %d\n", ie.TSDEN);
  TCAN4x5x_Debug("    PWRONEN: %d\n", ie.PWRONEN);
  TCAN4x5x_Debug("    UVIOEN: %d\n", ie.UVIOEN);
  TCAN4x5x_Debug("    UVSUPEN: %d\n", ie.UVSUPEN);
  TCAN4x5x_Debug("    SMSEN: %d\n", ie.SMSEN);
  TCAN4x5x_Debug("    CANBUSBATEN: %d\n", ie.CANBUSBATEN);
  TCAN4x5x_Debug("    CANBUSGNDEN: %d\n", ie.CANBUSGNDEN);
  TCAN4x5x_Debug("    CANBUSOPENEN: %d\n", ie.CANBUSOPENEN);
  TCAN4x5x_Debug("    CANLGNDEN: %d\n", ie.CANLGNDEN);
  TCAN4x5x_Debug("    CANHBATEN: %d\n", ie.CANHBATEN);
  TCAN4x5x_Debug("    CANHCANLEN: %d\n", ie.CANHCANLEN);
  TCAN4x5x_Debug("    CANBUSTERMOPENEN: %d\n", ie.CANBUSTERMOPENEN);
  TCAN4x5x_Debug("    CANBUSNORMEN: %d\n", ie.CANBUSNORMEN);
  /*
  TCAN4x5x Device Interrupt Enable: 0xFFFFFFFF
      RESERVED1: 255
      CANDOMEN: 1
      RESERVED2: 1
      CANTOEN: 1
      RESERVED3: 1
      FRAME_OVFEN: 1
      WKERREN: 1
      LWUEN: 1
      CANINTEN: 1
      ECCERREN: 1
      RESERVED4: 1
      WDTOEN: 1
      TSDEN: 1
      PWRONEN: 1
      UVIOEN: 1
      UVSUPEN: 1
      SMSEN: 1
      CANBUSBATEN: 1
      CANBUSGNDEN: 1
      CANBUSOPENEN: 1
      CANLGNDEN: 1
      CANHBATEN: 1
      CANHCANLEN: 1
      CANBUSTERMOPENEN: 1
      CANBUSNORMEN: 1
  */

  // TCAN4x5x_Device_Mode_Enum TCAN4x5x_Device_ReadMode(void);
  TCAN4x5x_Device_Mode_Enum mode = TCAN4x5x_Device_ReadMode();
  TCAN4x5x_Debug("TCAN4x5x Device Mode: %d, %s\n", mode,
                 mode == TCAN4x5x_DEVICE_MODE_NORMAL    ? "NORMAL"
                 : mode == TCAN4x5x_DEVICE_MODE_STANDBY ? "STANDBY"
                                                        : "SLEEP");
  /*
  TCAN4x5x Device Mode: 1, STANDBY
  */
  // extern bool TCAN4x5x_Device_SetMode(TCAN4x5x_Device_Mode_Enum modeDefine);
  // Set to NORMAL
  if (!TCAN4x5x_Device_SetMode(TCAN4x5x_DEVICE_MODE_NORMAL)) {
    TCAN4x5x_Debug("Failed to set device mode\n");
  }
  // read again
  mode = TCAN4x5x_Device_ReadMode();
  TCAN4x5x_Debug("TCAN4x5x Device Mode: %d, %s\n", mode,
                 mode == TCAN4x5x_DEVICE_MODE_NORMAL    ? "NORMAL"
                 : mode == TCAN4x5x_DEVICE_MODE_STANDBY ? "STANDBY"
                                                        : "SLEEP");

  // typedef enum { TCAN4x5x_DEVICE_TEST_MODE_NORMAL,
  // TCAN4x5x_DEVICE_TEST_MODE_PHY, TCAN4x5x_DEVICE_TEST_MODE_CONTROLLER }
  // TCAN4x5x_Device_Test_Mode_Enum;
  // extern TCAN4x5x_Device_Test_Mode_Enum TCAN4x5x_Device_ReadTestMode(void);
  TCAN4x5x_Device_Test_Mode_Enum testMode = TCAN4x5x_Device_ReadTestMode();
  TCAN4x5x_Debug("TCAN4x5x Device Test Mode: %d, %s\n", testMode,
                 testMode == TCAN4x5x_DEVICE_TEST_MODE_NORMAL ? "NORMAL"
                 : testMode == TCAN4x5x_DEVICE_TEST_MODE_PHY  ? "PHY"
                                                              : "CONTROLLER");
  /*
  TCAN4x5x Device Test Mode: 0, NORMAL
  */

  // extern void TCAN4x5x_MCAN_ReadCCCRRegister(TCAN4x5x_MCAN_CCCR_Config
  // *cccrConfig);
  TCAN4x5x_MCAN_CCCR_Config cccrConfig;
  TCAN4x5x_MCAN_ReadCCCRRegister(&cccrConfig);
  TCAN4x5x_Debug("TCAN4x5x MCAN CCCR Config: 0x%08X\n", cccrConfig.word);
  TCAN4x5x_Debug("    reserved: %d\n", cccrConfig.reserved);
  TCAN4x5x_Debug("    ASM: %d\n", cccrConfig.ASM);
  TCAN4x5x_Debug("    reserved2: %d\n", cccrConfig.reserved2);
  TCAN4x5x_Debug("    CSR: %d\n", cccrConfig.CSR);
  TCAN4x5x_Debug("    MON: %d\n", cccrConfig.MON);
  TCAN4x5x_Debug("    DAR: %d\n", cccrConfig.DAR);
  TCAN4x5x_Debug("    TEST: %d\n", cccrConfig.TEST);
  TCAN4x5x_Debug("    FDOE: %d\n", cccrConfig.FDOE);
  TCAN4x5x_Debug("    BRSE: %d\n", cccrConfig.BRSE);
  TCAN4x5x_Debug("    reserved3: %d\n", cccrConfig.reserved3);
  TCAN4x5x_Debug("    PXHD: %d\n", cccrConfig.PXHD);
  TCAN4x5x_Debug("    EFBI: %d\n", cccrConfig.EFBI);
  TCAN4x5x_Debug("    TXP: %d\n", cccrConfig.TXP);
  TCAN4x5x_Debug("    NISO: %d\n", cccrConfig.NISO);
  /*
  TCAN4x5x MCAN CCCR Config: 0x00000000
      reserved: 0
      ASM: 0
      reserved2: 0
      CSR: 0
      MON: 0
      DAR: 0
      TEST: 0
      FDOE: 0
      BRSE: 0
      reserved3: 0
      PXHD: 0
      EFBI: 0
      TXP: 0
      NISO: 0
  */
  // extern bool TCAN4x5x_MCAN_EnableProtectedRegisters(void);
  // Attempts to enable CCCR.CCE and CCCR.INIT to allow writes to protected
  // registers, needed for MCAN configuration
  if (!TCAN4x5x_MCAN_EnableProtectedRegisters()) {
    TCAN4x5x_Debug("Failed to enable protected registers\n");
  }
  // read again
  TCAN4x5x_MCAN_ReadCCCRRegister(&cccrConfig);
  TCAN4x5x_Debug("TCAN4x5x MCAN CCCR Config: 0x%08X\n", cccrConfig.word);
  TCAN4x5x_Debug("    reserved(CCE << 1 | INIT): %d\n", cccrConfig.reserved);
  TCAN4x5x_Debug("    ASM: %d\n", cccrConfig.ASM);
  TCAN4x5x_Debug("    reserved2: %d\n", cccrConfig.reserved2);
  TCAN4x5x_Debug("    CSR: %d\n", cccrConfig.CSR);
  TCAN4x5x_Debug("    MON: %d\n", cccrConfig.MON);
  TCAN4x5x_Debug("    DAR: %d\n", cccrConfig.DAR);
  TCAN4x5x_Debug("    TEST: %d\n", cccrConfig.TEST);
  TCAN4x5x_Debug("    FDOE(FDF): %d\n", cccrConfig.FDOE);
  TCAN4x5x_Debug("    BRSE(BRS): %d\n", cccrConfig.BRSE);
  TCAN4x5x_Debug("    reserved3: %d\n", cccrConfig.reserved3);
  TCAN4x5x_Debug("    PXHD: %d\n", cccrConfig.PXHD);
  TCAN4x5x_Debug("    EFBI: %d\n", cccrConfig.EFBI);
  TCAN4x5x_Debug("    TXP: %d\n", cccrConfig.TXP);
  TCAN4x5x_Debug("    NISO: %d\n", cccrConfig.NISO);
  // if CSR not clear, you should set mode to NORMAL first
  // enable FDF and BRS, and disable CSR(Clock Stop Request)
  cccrConfig.FDOE = 1;
  cccrConfig.BRSE = 1;
  cccrConfig.CSR = 0;
  // extern bool TCAN4x5x_MCAN_ConfigureCCCRRegister(TCAN4x5x_MCAN_CCCR_Config
  // *cccr);
  if (!TCAN4x5x_MCAN_ConfigureCCCRRegister(&cccrConfig)) {
    TCAN4x5x_Debug("Failed to configure CCCR\n");
  }
  // read again
  TCAN4x5x_MCAN_ReadCCCRRegister(&cccrConfig);
  TCAN4x5x_Debug("TCAN4x5x MCAN CCCR Config: 0x%08X\n", cccrConfig.word);
  TCAN4x5x_Debug("    reserved(CCE << 1 | INIT): %d\n", cccrConfig.reserved);
  TCAN4x5x_Debug("    ASM: %d\n", cccrConfig.ASM);
  TCAN4x5x_Debug("    reserved2: %d\n", cccrConfig.reserved2);
  TCAN4x5x_Debug("    CSR: %d\n", cccrConfig.CSR);
  TCAN4x5x_Debug("    MON: %d\n", cccrConfig.MON);
  TCAN4x5x_Debug("    DAR: %d\n", cccrConfig.DAR);
  TCAN4x5x_Debug("    TEST: %d\n", cccrConfig.TEST);
  TCAN4x5x_Debug("    FDOE(FDF): %d\n", cccrConfig.FDOE);
  TCAN4x5x_Debug("    BRSE(BRS): %d\n", cccrConfig.BRSE);
  TCAN4x5x_Debug("    reserved3: %d\n", cccrConfig.reserved3);
  TCAN4x5x_Debug("    PXHD: %d\n", cccrConfig.PXHD);
  TCAN4x5x_Debug("    EFBI: %d\n", cccrConfig.EFBI);
  TCAN4x5x_Debug("    TXP: %d\n", cccrConfig.TXP);
  TCAN4x5x_Debug("    NISO: %d\n", cccrConfig.NISO);
  /*
  TCAN4x5x MCAN CCCR Config: 0x00000303
    reserved(CCE << 1 | INIT): 3
    ASM: 0
    reserved2: 0
    CSR: 0
    MON: 0
    DAR: 0
    TEST: 0
    FDOE(FDF): 1
    BRSE(BRS): 1
    reserved3: 0
    PXHD: 0
    EFBI: 0
    TXP: 0
    NISO: 0
  */
  // tcan4x5x_mcan_config(500000, 0.8f, 2000000, 0.8f);
  tcan4x5x_mcan_config(1000000, 0.8f, 5000000, 0.75f);
  // tcan4x5x_mcan_write_test();

  is_busoff_or_error = false;
  busoff_or_error_count = 0;
}

static void tcan4x5x_mcan_config(uint32_t baud, float sample_point,
                                 uint32_t data_baud, float data_sample_point) {
  // Disable all non-MCAN related interrupts for simplicity
  TCAN4x5x_Device_Interrupt_Enable ie = {0};
  ie.UVIOEN = 1;           // UVIO, Undervoltage on VIO
  ie.UVSUPEN = 1;          // UVSUP, Undervoltage on VSUP and VCCOUT
  ie.CANBUSBATEN = 1;      // CANBUSBAT, CAN Shorted to VBAT
  ie.CANBUSGNDEN = 1;      // CANBUSGND, CAN Shorted to GND
  ie.CANBUSOPENEN = 1;     // CANBUSOPEN, CAN Open fault
  ie.CANLGNDEN = 1;        // CANLGND, CANL GND
  ie.CANHBATEN = 1;        // CANHBAT, CANH to VBAT
  ie.CANHCANLEN = 1;       // CANHCANL, CANH and CANL shorted
  ie.CANBUSTERMOPENEN = 1; // CANBUSTERMOPEN, CAN Bus has termination point open
  ie.CANBUSNORMEN = 1;     // CANBUSNOM, CAN Bus is normal flag
  TCAN4x5x_Device_ConfigureInterruptEnable(&ie);
  // Clear PWRON because if it's not cleared within ~4 minutes, it goes to sleep
  TCAN4x5x_Device_Interrupts ir = {0};
  TCAN4x5x_Device_ReadInterrupts(&ir);
  if (ir.PWRON) {
    TCAN4x5x_Device_ClearInterrupts(&ir);
    TCAN4x5x_Debug("Cleared PWRON interrupt\n");
  }

  // 40MHz Clock
  TCAN4x5x_MCAN_CCCR_Config cccr_config = {0};
  cccr_config.FDOE = 1;
  cccr_config.BRSE = 1;
  TCAN4x5x_MCAN_Nominal_Timing_Simple timing = {
      .NominalBitRatePrescaler = 4,
      .NominalTqBeforeSamplePoint =
          (int)roundf((10000000.0f / baud) * sample_point),
      .NominalTqAfterSamplePoint =
          (int)roundf((10000000.0f / baud) * (1 - sample_point))};
  TCAN4x5x_Debug("NominalTqBeforeSamplePoint: %d\n",
                 timing.NominalTqBeforeSamplePoint);
  TCAN4x5x_Debug("NominalTqAfterSamplePoint: %d\n",
                 timing.NominalTqAfterSamplePoint);
  TCAN4x5x_MCAN_Data_Timing_Simple data_timing = {
      .DataBitRatePrescaler = 1,
      .DataTqBeforeSamplePoint =
          (int)roundf((40000000 / data_baud) * data_sample_point),
      .DataTqAfterSamplePoint =
          (int)roundf((40000000 / data_baud) * (1 - data_sample_point))};
  TCAN4x5x_Debug("DataTqBeforeSamplePoint: %d\n",
                 data_timing.DataTqBeforeSamplePoint);
  TCAN4x5x_Debug("DataTqAfterSamplePoint: %d\n",
                 data_timing.DataTqAfterSamplePoint);
  TCAN4x5x_MCAN_Global_Filter_Configuration global_filter = {0};
  global_filter.RRFE = 1;
  global_filter.RRFS = 1;
  global_filter.ANFE = TCAN4x5x_GFC_ACCEPT_INTO_RXFIFO0;
  global_filter.ANFS = TCAN4x5x_GFC_ACCEPT_INTO_RXFIFO0;
  // 2KB MRAM, 2048 / 72 = 28.44
  TCAN4x5x_MRAM_Config mram_config = {
      .SIDNumElements = 1,
      .XIDNumElements = 1,
      .Rx0NumElements = 11,
      .Rx0ElementSize = MRAM_64_Byte_Data,
      .Rx1NumElements = 0,
      .Rx1ElementSize = MRAM_64_Byte_Data,
      .RxBufNumElements = 0,
      .RxBufElementSize = MRAM_64_Byte_Data,
      .TxEventFIFONumElements = 0,
      .TxBufferNumElements = 16,
      .TxBufferElementSize = MRAM_64_Byte_Data,
  };

  TCAN4x5x_MCAN_EnableProtectedRegisters();
  TCAN4x5x_MCAN_ConfigureCCCRRegister(&cccr_config);
  TCAN4x5x_MCAN_ConfigureGlobalFilter(&global_filter);
  TCAN4x5x_MCAN_ConfigureNominalTiming_Simple(&timing);
  TCAN4x5x_MCAN_ConfigureDataTiming_Simple(&data_timing);
  TCAN4x5x_MRAM_Clear();
  TCAN4x5x_MRAM_Configure(&mram_config);
  TCAN4x5x_MCAN_DisableProtectedRegisters();

  // interrupt
  TCAN4x5x_MCAN_Interrupt_Enable mcan_ie = {0};
  mcan_ie.RF0NE = 1; // Enable RX FIFO 0 New Message interrupt
  mcan_ie.EPE = 1;   // Enable Error Passive interrupt
  mcan_ie.EWE = 1;   // Enable Warning Status interrupt
  mcan_ie.BOE = 1;   // Enable Bus Off interrupt
  TCAN4x5x_MCAN_ConfigureInterruptEnable(&mcan_ie);

  // filter
  TCAN4x5x_MCAN_SID_Filter std_filter = {0};
  std_filter.SFT = TCAN4x5x_SID_SFT_CLASSIC;
  std_filter.SFEC = TCAN4x5x_SID_SFEC_PRIORITYSTORERX0;
  std_filter.SFID1 = 0;
  std_filter.SFID2 = 0;
  TCAN4x5x_MCAN_WriteSIDFilter(0, &std_filter);
  TCAN4x5x_MCAN_XID_Filter ext_filter = {0};
  ext_filter.EFT = TCAN4x5x_XID_EFT_CLASSIC;
  ext_filter.EFEC = TCAN4x5x_XID_EFEC_PRIORITYSTORERX0;
  ext_filter.EFID1 = 0;
  ext_filter.EFID2 = 0;
  TCAN4x5x_MCAN_WriteXIDFilter(0, &ext_filter);

  /* Configure the TCAN4550 Non-CAN-related functions */
  TCAN4x5x_DEV_CONFIG devConfig = {0};
  devConfig.SWE_DIS =
      0; // Keep Sleep Wake Error Enabled (it's a disable bit, not an enable)
  devConfig.DEVICE_RESET = 0; // Not requesting a software reset
  devConfig.WD_EN = 0;        // Watchdog disabled
  devConfig.nWKRQ_CONFIG = 0; // Mirror INH function (default)
  devConfig.INH_DIS = 0;      // INH enabled (default)
  devConfig.GPIO1_GPO_CONFIG =
      TCAN4x5x_DEV_CONFIG_GPO1_MCAN_INT1; // MCAN nINT 1 (default)
  devConfig.FAIL_SAFE_EN = 0;             // Failsafe disabled (default)
  devConfig.GPIO1_CONFIG =
      TCAN4x5x_DEV_CONFIG_GPIO1_CONFIG_GPO; // GPIO set as GPO (Default)
  devConfig.WD_ACTION =
      TCAN4x5x_DEV_CONFIG_WDT_ACTION_nINT; // Watchdog set an interrupt
                                           // (default)
  devConfig.WD_BIT_RESET = 0;              // Don't reset the watchdog
  devConfig.nWKRQ_VOLTAGE = 0; // Set nWKRQ to internal voltage rail (default)
  devConfig.GPO2_CONFIG =
      TCAN4x5x_DEV_CONFIG_GPO2_NO_ACTION; // GPO2 has no behavior (default)
  devConfig.CLK_REF = 1; // Input crystal is a 40 MHz crystal (default)
  devConfig.WAKE_CONFIG =
      TCAN4x5x_DEV_CONFIG_WAKE_BOTH_EDGES; // Wake pin can be triggered by
                                           // either edge (default)
  TCAN4x5x_Device_Configure(&devConfig);

  TCAN4x5x_Device_SetMode(TCAN4x5x_DEVICE_MODE_NORMAL);
  TCAN4x5x_MCAN_ClearInterruptsAll();
}

static void tcan4x5x_mcan_write_test(void) {
  static uint8_t buffer_index = 0;
  uint32_t ret = 0;
  TCAN4x5x_MCAN_TX_Header tx_header = {
      .ID = 0x123,
      .RTR = 0,
      .XTD = 0,
      .ESI = 0,
      .DLC = 8,
      .BRS = 0,
      .FDF = 0,
      .reserved = 0,
      .EFC = 0,
      .MM = 0,
  };
  uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  for (int i = 0; i < 20; i++) {
    ret = TCAN4x5x_MCAN_WriteTXBuffer(buffer_index, &tx_header, data);
    data[0]++;
    TCAN4x5x_Debug("Write TX ret: %d\n", ret);
    if (!TCAN4x5x_MCAN_TransmitBufferContents(buffer_index)) {
      TCAN4x5x_Debug("Failed to transmit buffer contents\n");
    }
    buffer_index = (buffer_index + 1) % 16;
    HAL_Delay(1);
  }
}

static void tcan4x5x_mcan_write_test_2(void) {
  static uint32_t last_send_time = 0;
  static uint64_t counter = 0;
  uint32_t current_time = HAL_GetTick();

  if (is_busoff_or_error) {
    return;
  }

  // Send message every 1ms
  if (current_time - last_send_time >= 1) {
    // Prepare TX header with ID 0x456
    TCAN4x5x_MCAN_TX_Header tx_header = {
        .ID = 0x456,
        .RTR = 0,
        .XTD = 0,
        .ESI = 0,
        .DLC = 8, // [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]
        .BRS = 0,
        .FDF = 0,
        .reserved = 0,
        .EFC = 0,
        .MM = 0,
    };

    // Convert uint64_t counter to byte array (little-endian)
    uint8_t data[64] = {0};
    data[0] = (uint8_t)(counter);
    data[1] = (uint8_t)(counter >> 8);
    data[2] = (uint8_t)(counter >> 16);
    data[3] = (uint8_t)(counter >> 24);
    data[4] = (uint8_t)(counter >> 32);
    data[5] = (uint8_t)(counter >> 40);
    data[6] = (uint8_t)(counter >> 48);
    data[7] = (uint8_t)(counter >> 56);

    // Get next buffer index
    static uint8_t buffer_index = 0;
    buffer_index = (buffer_index + 1) % 16;

    // Write to TX buffer and transmit
    if (TCAN4x5x_MCAN_WriteTXBuffer(buffer_index, &tx_header, data)) {
      if (TCAN4x5x_MCAN_TransmitBufferContents(buffer_index)) {
        // TCAN4x5x_Debug("Sent ID 0x456, counter: %llu\n", counter);
        counter++;
        last_send_time = current_time;
      } else {
        TCAN4x5x_Debug("Failed to transmit buffer\n");
      }
    }
  }
}

static int interrupt_count = 0;
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == GPIO_PIN_0) {
    interrupt_count++;
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
  }
}

const char *dev_interrupts[] = {
    "VTWD",           "M_CAN_INT",  "SWERR",     "SPIERR",  "CBF",
    "CANERR",         "WKRQ",       "GLOBALERR", "CANDOM",  "RESERVED",
    "CANTO",          "RESERVED2",  "FRAME_OVF", "WKERR",   "LWU",
    "CANINT",         "ECCERR",     "RESERVED3", "WDTO",    "TSD",
    "PWRON",          "UVIO",       "UVSUP",     "SMS",     "CANBUSBAT",
    "CANBUSGND",      "CANBUSOPEN", "CANLGND",   "CANHBAT", "CANHCANL",
    "CANBUSTERMOPEN", "CANBUSNORM",
};

const char *mcan_interrupts[] = {
    "RF0N", "RF0W", "RF0F", "RF0L", "RF1N", "RF1W", "RF1F", "RF1L",
    "HPM",  "TC",   "TCF",  "TFE",  "TEFN", "TEFW", "TEFF", "TEFL",
    "TSW",  "MRAF", "TOO",  "DRX",  "BEC",  "BEU",  "ELO",  "EP",
    "EW",   "BO",   "WDI",  "PEA",  "PED",  "ARA",
};

void bsp_tcan4x5x_process(void) {
  // if busoff or error, wait 100ms and reinit
  if (busoff_or_error_count && HAL_GetTick() - busoff_or_error_count > 100) {
    TCAN4x5x_Debug("busoff_or_error_count: %d\n", busoff_or_error_count);
    busoff_or_error_count = 0;
    bsp_tcan4x5x_init();
  }
  if (interrupt_count) {
    TCAN4x5x_Debug("interrupt_count: %d\n", interrupt_count);
    interrupt_count--;
    TCAN4x5x_Device_Interrupts dev_ir = {0};
    TCAN4x5x_Device_ReadInterrupts(&dev_ir);
    TCAN4x5x_Debug("TCAN4x5x Device Interrupts: 0x%08X\n", dev_ir.word);
    int dev_interrupts_counter = 0;
    for (int i = 0; i < sizeof(dev_interrupts) / sizeof(dev_interrupts[0]);
         i++) {
      if (dev_ir.word & (1 << i)) {
        TCAN4x5x_Debug("  - %s\n", dev_interrupts[i]);
        dev_interrupts_counter++;
      }
    }
    if (dev_ir.UVSUP) {
      is_busoff_or_error = true;
      TCAN4x5x_Debug("UVSUP, is_busoff_or_error: %d\n", is_busoff_or_error);
      busoff_or_error_count = HAL_GetTick();
      return;
    }
    if (dev_ir.SPIERR) {
      TCAN4x5x_Debug("SPIERR\n");
      TCAN4x5x_Device_ClearSPIERR();
    }
    // if (dev_interrupts_counter) {
    //   // TCAN4x5x_Device_ClearInterrupts(&dev_ir);
    //   TCAN4x5x_MCAN_EnableProtectedRegisters();
    //   TCAN4x5x_Device_ClearInterruptsAll();
    //   TCAN4x5x_MCAN_DisableProtectedRegisters();
    // }
    TCAN4x5x_MCAN_Interrupts mcan_ir = {0};
    TCAN4x5x_MCAN_ReadInterrupts(&mcan_ir);
    TCAN4x5x_Debug("TCAN4x5x MCAN Interrupts: 0x%08X\n", mcan_ir.word);
    for (int i = 0; i < sizeof(mcan_interrupts) / sizeof(mcan_interrupts[0]);
         i++) {
      if (mcan_ir.word & (1 << i)) {
        TCAN4x5x_Debug("  - %s\n", mcan_interrupts[i]);
      }
    }
    if (mcan_ir.RF0N) {
      TCAN4x5x_MCAN_ClearInterrupts(&mcan_ir);
      TCAN4x5x_MCAN_RX_Header rx_header = {0};
      uint8_t data[64] = {0};
      uint8_t bytes = TCAN4x5x_MCAN_ReadNextFIFO(RXFIFO0, &rx_header, data);
      TCAN4x5x_Debug("RF0N, RXFIFO0: %d bytes\n", bytes);
      TCAN4x5x_Debug("  ID: 0x%03X\n", rx_header.ID);
      TCAN4x5x_Debug("  XTD: %d\n", rx_header.XTD);
      TCAN4x5x_Debug("  RTR: %d\n", rx_header.RTR);
      TCAN4x5x_Debug("  FDF: %d\n", rx_header.FDF);
      TCAN4x5x_Debug("  BRS: %d\n", rx_header.BRS);
      TCAN4x5x_Debug("  ESI: %d\n", rx_header.ESI);
      TCAN4x5x_Debug("  DLC: %d\n", rx_header.DLC);
      TCAN4x5x_Debug("  Data: ");
      for (int i = 0; i < bytes; i++) {
        TCAN4x5x_Debug("%02X ", data[i]);
      }
      TCAN4x5x_Debug("\n");
    }
    if (mcan_ir.EP) {
      TCAN4x5x_MCAN_ClearInterrupts(&mcan_ir);
      TCAN4x5x_Debug("! Error Passive\n");
    }
    if (mcan_ir.EW) {
      TCAN4x5x_MCAN_ClearInterrupts(&mcan_ir);
      TCAN4x5x_Debug("!! Warning Status\n");
      is_busoff_or_error = true;
      busoff_or_error_count = HAL_GetTick();
    }
    if (mcan_ir.BO) {
      TCAN4x5x_MCAN_ClearInterrupts(&mcan_ir);
      TCAN4x5x_Debug("!!! Bus Off\n");
      is_busoff_or_error = true;
      busoff_or_error_count = HAL_GetTick();
      return;
    }
  }

  tcan4x5x_mcan_write_test_2();
}