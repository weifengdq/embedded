#ifndef I2C_EEPROM_EUI_H
#define I2C_EEPROM_EUI_H 1

#include "Ifx_Types.h"
#include "IfxI2c_I2c.h"

#define TC4D7_EEPROM_MAC_LENGTH 6U
#define TC4D7_EEPROM_TEST_SIZE 32U

extern IfxI2c_I2c g_i2cHandle;
extern IfxI2c_I2c_Device g_i2cDevice;

extern uint8 g_macAddr[TC4D7_EEPROM_MAC_LENGTH];
extern uint8 g_eepromBackupData[TC4D7_EEPROM_TEST_SIZE];
extern uint8 g_eepromWriteData[TC4D7_EEPROM_TEST_SIZE];
extern uint8 g_eepromReadData[TC4D7_EEPROM_TEST_SIZE];

extern volatile boolean g_i2cInitDone;
extern volatile boolean g_macReadPassed;
extern volatile boolean g_readWriteTestPassed;
extern volatile boolean g_restorePassed;
extern volatile boolean g_benchmarkPassed;
extern volatile boolean g_allTestsPassed;

extern volatile uint8 g_lastEepromAddress;
extern volatile IfxI2c_I2c_Status g_lastI2cStatus;
extern volatile uint32 g_readyPollCount;
extern volatile uint32 g_writeBenchmarkBytes;
extern volatile uint32 g_readBenchmarkBytes;
extern volatile uint64 g_writeBenchmarkTimeUs;
extern volatile uint64 g_readBenchmarkTimeUs;
extern volatile uint32 g_writeBenchmarkBytesPerSecond;
extern volatile uint32 g_readBenchmarkBytesPerSecond;

void TC4D7_I2cEepromEui_run(void);

#endif