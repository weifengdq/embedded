#include "I2cEepromEui.h"

#include <string.h>

#include "IfxCpu.h"
#include "IfxPort.h"
#include "IfxStm.h"

#define TC4D7_I2C_BAUDRATE 400000U
#define TC4D7_EEPROM_DEVICE_ADDRESS 0x50U
#define TC4D7_EEPROM_DEVICE_ADDRESS_8BIT (TC4D7_EEPROM_DEVICE_ADDRESS << 1)
#define TC4D7_EEPROM_SIZE_BYTES 256U
#define TC4D7_EEPROM_PAGE_SIZE 8U
#define TC4D7_EEPROM_PROTECTED_BASE 0x80U
#define TC4D7_EEPROM_MAC_OFFSET 0xFAU
#define TC4D7_EEPROM_TEST_OFFSET 0x20U
#define TC4D7_EEPROM_READY_TIMEOUT_POLLS 100000U
#define TC4D7_EEPROM_WRITE_BENCHMARK_ROUNDS 4U
#define TC4D7_EEPROM_READ_BENCHMARK_ROUNDS 64U

IfxI2c_I2c g_i2cHandle;
IfxI2c_I2c_Device g_i2cDevice;

uint8 g_macAddr[TC4D7_EEPROM_MAC_LENGTH];
uint8 g_eepromBackupData[TC4D7_EEPROM_TEST_SIZE];
uint8 g_eepromWriteData[TC4D7_EEPROM_TEST_SIZE];
uint8 g_eepromReadData[TC4D7_EEPROM_TEST_SIZE];

volatile boolean g_i2cInitDone = FALSE;
volatile boolean g_macReadPassed = FALSE;
volatile boolean g_readWriteTestPassed = FALSE;
volatile boolean g_restorePassed = FALSE;
volatile boolean g_benchmarkPassed = FALSE;
volatile boolean g_allTestsPassed = FALSE;

volatile uint8 g_lastEepromAddress = 0U;
volatile IfxI2c_I2c_Status g_lastI2cStatus = IfxI2c_I2c_Status_error;
volatile uint32 g_readyPollCount = 0U;
volatile uint32 g_writeBenchmarkBytes = 0U;
volatile uint32 g_readBenchmarkBytes = 0U;
volatile uint64 g_writeBenchmarkTimeUs = 0U;
volatile uint64 g_readBenchmarkTimeUs = 0U;
volatile uint32 g_writeBenchmarkBytesPerSecond = 0U;
volatile uint32 g_readBenchmarkBytesPerSecond = 0U;

static const IfxI2c_Pins g_i2cPins = {
    &IfxI2c0_SCL_P13_1_INOUT,
    &IfxI2c0_SDA_P13_2_INOUT,
    IfxPort_PadDriver_ttlSpeed1
};

static uint64 getTimeStamp(void)
{
    return IfxStm_get(IfxCpu_getAddress(IfxCpu_getCoreIndex()));
}

static uint64 getElapsedMicroseconds(uint64 startTicks)
{
    return IfxStm_getMicrosecondsFromTicks(getTimeStamp() - startTicks);
}

static uint32 calculateBytesPerSecond(uint32 bytesTransferred, uint64 elapsedUs)
{
    if (elapsedUs == 0U)
    {
        return 0U;
    }

    return (uint32)(((uint64)bytesTransferred * 1000000ULL) / elapsedUs);
}

static void fillPattern(uint8 *buffer, Ifx_SizeT size, uint8 seed)
{
    Ifx_SizeT index;

    for (index = 0; index < size; ++index)
    {
        buffer[index] = (uint8)(seed + (uint8)(index * 13U) + (uint8)(index >> 1U));
    }
}

static IfxI2c_I2c_Status waitUntilReady(void)
{
    static uint8 dummyByte = 0U;
    uint32 attempt;
    IfxI2c_I2c_Status status = IfxI2c_I2c_Status_error;

    for (attempt = 0; attempt < TC4D7_EEPROM_READY_TIMEOUT_POLLS; ++attempt)
    {
        status = IfxI2c_I2c_write(&g_i2cDevice, &dummyByte, 0U);

        if (status != IfxI2c_I2c_Status_nak)
        {
            g_readyPollCount += attempt + 1U;
            return status;
        }
    }

    g_readyPollCount += TC4D7_EEPROM_READY_TIMEOUT_POLLS;
    return status;
}

static IfxI2c_I2c_Status writeInternalAddress(uint8 wordAddress)
{
    uint8 txBuffer[1] = {wordAddress};
    uint32 attempt;
    IfxI2c_I2c_Status status = IfxI2c_I2c_Status_error;

    g_lastEepromAddress = wordAddress;

    for (attempt = 0; attempt < TC4D7_EEPROM_READY_TIMEOUT_POLLS; ++attempt)
    {
        status = IfxI2c_I2c_write(&g_i2cDevice, txBuffer, 1U);

        if (status != IfxI2c_I2c_Status_nak)
        {
            g_lastI2cStatus = status;
            return status;
        }
    }

    g_lastI2cStatus = status;
    return status;
}

static IfxI2c_I2c_Status eepromRead(uint8 wordAddress, uint8 *data, Ifx_SizeT size)
{
    uint32 attempt;
    IfxI2c_I2c_Status status;

    status = writeInternalAddress(wordAddress);

    if (status != IfxI2c_I2c_Status_ok)
    {
        return status;
    }

    for (attempt = 0; attempt < TC4D7_EEPROM_READY_TIMEOUT_POLLS; ++attempt)
    {
        status = IfxI2c_I2c_read(&g_i2cDevice, data, size);

        if (status != IfxI2c_I2c_Status_nak)
        {
            g_lastI2cStatus = status;
            return status;
        }
    }

    g_lastI2cStatus = status;
    return status;
}

static IfxI2c_I2c_Status eepromWritePage(uint8 wordAddress, const uint8 *data, uint8 size)
{
    uint8 txBuffer[1U + TC4D7_EEPROM_PAGE_SIZE];
    uint32 attempt;
    IfxI2c_I2c_Status status = IfxI2c_I2c_Status_error;

    txBuffer[0] = wordAddress;
    (void)memcpy(&txBuffer[1], data, size);
    g_lastEepromAddress = wordAddress;

    for (attempt = 0; attempt < TC4D7_EEPROM_READY_TIMEOUT_POLLS; ++attempt)
    {
        status = IfxI2c_I2c_write(&g_i2cDevice, txBuffer, (Ifx_SizeT)(size + 1U));

        if (status != IfxI2c_I2c_Status_nak)
        {
            break;
        }
    }

    if (status != IfxI2c_I2c_Status_ok)
    {
        g_lastI2cStatus = status;
        return status;
    }

    status = waitUntilReady();
    g_lastI2cStatus = status;
    return status;
}

static IfxI2c_I2c_Status eepromWrite(uint8 wordAddress, const uint8 *data, Ifx_SizeT size)
{
    Ifx_SizeT remaining = size;
    Ifx_SizeT offset = 0U;
    IfxI2c_I2c_Status status = IfxI2c_I2c_Status_ok;

    if ((((uint32)wordAddress + (uint32)size) > TC4D7_EEPROM_PROTECTED_BASE) ||
        (((uint32)wordAddress + (uint32)size) > TC4D7_EEPROM_SIZE_BYTES))
    {
        g_lastI2cStatus = IfxI2c_I2c_Status_error;
        return IfxI2c_I2c_Status_error;
    }

    while (remaining > 0U)
    {
        uint8 pageOffset = (uint8)((wordAddress + offset) % TC4D7_EEPROM_PAGE_SIZE);
        uint8 chunkSize = (uint8)(TC4D7_EEPROM_PAGE_SIZE - pageOffset);

        if ((Ifx_SizeT)chunkSize > remaining)
        {
            chunkSize = (uint8)remaining;
        }

        status = eepromWritePage((uint8)(wordAddress + offset), &data[offset], chunkSize);

        if (status != IfxI2c_I2c_Status_ok)
        {
            return status;
        }

        offset += chunkSize;
        remaining -= chunkSize;
    }

    g_lastI2cStatus = status;
    return status;
}

static void initI2c(void)
{
    IfxI2c_I2c_Config moduleConfig;
    IfxI2c_I2c_deviceConfig deviceConfig;

    IfxI2c_I2c_initConfig(&moduleConfig, &MODULE_I2C0);
    moduleConfig.pins = &g_i2cPins;
    moduleConfig.baudrate = TC4D7_I2C_BAUDRATE;
    moduleConfig.mode = IfxI2c_Mode_StandardAndFast;

    IfxI2c_I2c_initModule(&g_i2cHandle, &moduleConfig);

    IfxI2c_I2c_initDeviceConfig(&deviceConfig, &g_i2cHandle);
    deviceConfig.deviceAddress = TC4D7_EEPROM_DEVICE_ADDRESS_8BIT;
    deviceConfig.enableRepeatedStart = FALSE;

    IfxI2c_I2c_initDevice(&g_i2cDevice, &deviceConfig);
    g_i2cInitDone = TRUE;
}

static boolean runReadWriteTest(void)
{
    IfxI2c_I2c_Status status;

    status = eepromRead(TC4D7_EEPROM_TEST_OFFSET, g_eepromBackupData, TC4D7_EEPROM_TEST_SIZE);

    if (status != IfxI2c_I2c_Status_ok)
    {
        return FALSE;
    }

    fillPattern(g_eepromWriteData, TC4D7_EEPROM_TEST_SIZE, 0x31U);

    status = eepromWrite(TC4D7_EEPROM_TEST_OFFSET, g_eepromWriteData, TC4D7_EEPROM_TEST_SIZE);

    if (status != IfxI2c_I2c_Status_ok)
    {
        return FALSE;
    }

    status = eepromRead(TC4D7_EEPROM_TEST_OFFSET, g_eepromReadData, TC4D7_EEPROM_TEST_SIZE);

    if (status != IfxI2c_I2c_Status_ok)
    {
        return FALSE;
    }

    return memcmp(g_eepromWriteData, g_eepromReadData, TC4D7_EEPROM_TEST_SIZE) == 0;
}

static boolean runBenchmark(void)
{
    IfxI2c_I2c_Status status = IfxI2c_I2c_Status_ok;
    uint64 startTicks;
    uint32 round;

    g_writeBenchmarkBytes = 0U;
    g_readBenchmarkBytes = 0U;
    g_writeBenchmarkTimeUs = 0U;
    g_readBenchmarkTimeUs = 0U;
    g_writeBenchmarkBytesPerSecond = 0U;
    g_readBenchmarkBytesPerSecond = 0U;

    startTicks = getTimeStamp();

    for (round = 0U; round < TC4D7_EEPROM_WRITE_BENCHMARK_ROUNDS; ++round)
    {
        fillPattern(g_eepromWriteData, TC4D7_EEPROM_TEST_SIZE, (uint8)(0x40U + round * 9U));
        status = eepromWrite(TC4D7_EEPROM_TEST_OFFSET, g_eepromWriteData, TC4D7_EEPROM_TEST_SIZE);

        if (status != IfxI2c_I2c_Status_ok)
        {
            break;
        }

        g_writeBenchmarkBytes += TC4D7_EEPROM_TEST_SIZE;
    }

    g_writeBenchmarkTimeUs = getElapsedMicroseconds(startTicks);
    g_writeBenchmarkBytesPerSecond = calculateBytesPerSecond(g_writeBenchmarkBytes, g_writeBenchmarkTimeUs);

    if (status != IfxI2c_I2c_Status_ok)
    {
        return FALSE;
    }

    startTicks = getTimeStamp();

    for (round = 0U; round < TC4D7_EEPROM_READ_BENCHMARK_ROUNDS; ++round)
    {
        status = eepromRead(TC4D7_EEPROM_TEST_OFFSET, g_eepromReadData, TC4D7_EEPROM_TEST_SIZE);

        if (status != IfxI2c_I2c_Status_ok)
        {
            break;
        }

        g_readBenchmarkBytes += TC4D7_EEPROM_TEST_SIZE;
    }

    g_readBenchmarkTimeUs = getElapsedMicroseconds(startTicks);
    g_readBenchmarkBytesPerSecond = calculateBytesPerSecond(g_readBenchmarkBytes, g_readBenchmarkTimeUs);

    return status == IfxI2c_I2c_Status_ok;
}

static boolean restoreTestArea(void)
{
    IfxI2c_I2c_Status status;

    status = eepromWrite(TC4D7_EEPROM_TEST_OFFSET, g_eepromBackupData, TC4D7_EEPROM_TEST_SIZE);

    if (status != IfxI2c_I2c_Status_ok)
    {
        return FALSE;
    }

    status = eepromRead(TC4D7_EEPROM_TEST_OFFSET, g_eepromReadData, TC4D7_EEPROM_TEST_SIZE);

    if (status != IfxI2c_I2c_Status_ok)
    {
        return FALSE;
    }

    return memcmp(g_eepromBackupData, g_eepromReadData, TC4D7_EEPROM_TEST_SIZE) == 0;
}

void TC4D7_I2cEepromEui_run(void)
{
    IfxI2c_I2c_Status status;

    (void)memset(g_macAddr, 0, sizeof(g_macAddr));
    (void)memset(g_eepromBackupData, 0, sizeof(g_eepromBackupData));
    (void)memset(g_eepromWriteData, 0, sizeof(g_eepromWriteData));
    (void)memset(g_eepromReadData, 0, sizeof(g_eepromReadData));

    g_i2cInitDone = FALSE;
    g_macReadPassed = FALSE;
    g_readWriteTestPassed = FALSE;
    g_restorePassed = FALSE;
    g_benchmarkPassed = FALSE;
    g_allTestsPassed = FALSE;
    g_lastEepromAddress = 0U;
    g_lastI2cStatus = IfxI2c_I2c_Status_error;
    g_readyPollCount = 0U;

    initI2c();

    status = eepromRead(TC4D7_EEPROM_MAC_OFFSET, g_macAddr, TC4D7_EEPROM_MAC_LENGTH);
    g_macReadPassed = (status == IfxI2c_I2c_Status_ok);

    if (!g_macReadPassed)
    {
        return;
    }

    g_readWriteTestPassed = runReadWriteTest();

    if (!g_readWriteTestPassed)
    {
        return;
    }

    g_benchmarkPassed = runBenchmark();
    g_restorePassed = restoreTestArea();
    g_allTestsPassed = g_macReadPassed && g_readWriteTestPassed && g_benchmarkPassed && g_restorePassed;
}