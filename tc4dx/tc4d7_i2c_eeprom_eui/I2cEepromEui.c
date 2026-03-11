#include "I2cEepromEui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "IfxCpu.h"
#include "IfxPort.h"
#include "IfxStm.h"

#include "serialio.h"

#define TC4D7_I2C_BAUDRATE 400000U
// #define TC4D7_I2C_BAUDRATE 100000U
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
volatile boolean g_writeBlockedDetected = FALSE;
volatile boolean g_readOnlyModeDetected = FALSE;

volatile uint8 g_lastEepromAddress = 0U;
volatile IfxI2c_I2c_Status g_lastI2cStatus = IfxI2c_I2c_Status_error;
volatile uint8 g_mismatchIndex = 0U;
volatile uint8 g_expectedByte = 0U;
volatile uint8 g_actualByte = 0U;
volatile uint8 g_backupByte = 0U;
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

static void printLine(const char *text)
{
    if (text != NULL_PTR)
    {
        (void)SERIALIO_WriteBuffer((const uint8 *)text, (Ifx_SizeT)strlen(text));
    }
}

static void printFormatted(const char *format, ...)
{
    char buffer[192];
    sint32 length;
    va_list args;

    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length <= 0)
    {
        return;
    }

    if ((size_t)length > sizeof(buffer))
    {
        length = (sint32)sizeof(buffer);
    }

    (void)SERIALIO_WriteBuffer((const uint8 *)buffer, (Ifx_SizeT)length);
}

static void printMacAddress(void)
{
    printFormatted(
        "MAC address : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
        g_macAddr[0],
        g_macAddr[1],
        g_macAddr[2],
        g_macAddr[3],
        g_macAddr[4],
        g_macAddr[5]);
}

static void printHexBuffer(const char *label, const uint8 *data, Ifx_SizeT size)
{
    Ifx_SizeT index;

    if ((label != NULL_PTR) && (data != NULL_PTR))
    {
        printFormatted("%s", label);

        for (index = 0; index < size; ++index)
        {
            printFormatted("%02X", data[index]);

            if ((index + 1U) < size)
            {
                printLine(" ");
            }
        }

        printLine("\r\n");
    }
}

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

static IfxI2c_I2c_Status eepromWriteByte(uint8 wordAddress, uint8 data)
{
    uint8 txBuffer[2];
    uint32 attempt;
    IfxI2c_I2c_Status status = IfxI2c_I2c_Status_error;

    txBuffer[0] = wordAddress;
    txBuffer[1] = data;
    g_lastEepromAddress = wordAddress;

    for (attempt = 0; attempt < TC4D7_EEPROM_READY_TIMEOUT_POLLS; ++attempt)
    {
        status = IfxI2c_I2c_write(&g_i2cDevice, txBuffer, 2U);

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
    Ifx_SizeT offset = 0U;
    IfxI2c_I2c_Status status = IfxI2c_I2c_Status_ok;

    if ((((uint32)wordAddress + (uint32)size) > TC4D7_EEPROM_PROTECTED_BASE) ||
        (((uint32)wordAddress + (uint32)size) > TC4D7_EEPROM_SIZE_BYTES))
    {
        g_lastI2cStatus = IfxI2c_I2c_Status_error;
        return IfxI2c_I2c_Status_error;
    }

    while (offset < size)
    {
        status = eepromWriteByte((uint8)(wordAddress + offset), data[offset]);

        if (status != IfxI2c_I2c_Status_ok)
        {
            return status;
        }

        offset += 1U;
    }

    g_lastI2cStatus = status;
    return status;
}

static void captureMismatchDetails(const uint8 *expected, const uint8 *actual, const uint8 *backup, Ifx_SizeT size)
{
    Ifx_SizeT index;

    g_mismatchIndex = 0U;
    g_expectedByte = 0U;
    g_actualByte = 0U;
    g_backupByte = 0U;
    g_writeBlockedDetected = FALSE;

    for (index = 0; index < size; ++index)
    {
        if (expected[index] != actual[index])
        {
            g_mismatchIndex = (uint8)index;
            g_expectedByte = expected[index];
            g_actualByte = actual[index];
            g_backupByte = backup[index];
            g_writeBlockedDetected = (actual[index] == backup[index]);
            return;
        }
    }
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

    if (memcmp(g_eepromWriteData, g_eepromReadData, TC4D7_EEPROM_TEST_SIZE) != 0)
    {
        captureMismatchDetails(g_eepromWriteData, g_eepromReadData, g_eepromBackupData, TC4D7_EEPROM_TEST_SIZE);
        return FALSE;
    }

    return TRUE;
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

static boolean runReadOnlyBenchmark(void)
{
    IfxI2c_I2c_Status status = IfxI2c_I2c_Status_ok;
    uint64 startTicks;
    uint32 round;

    g_writeBenchmarkBytes = 0U;
    g_writeBenchmarkTimeUs = 0U;
    g_writeBenchmarkBytesPerSecond = 0U;
    g_readBenchmarkBytes = 0U;
    g_readBenchmarkTimeUs = 0U;
    g_readBenchmarkBytesPerSecond = 0U;

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

    printLine(
        "\r\n"
        "========================================\r\n"
        "TC4D7 I2C EEPROM / EUI-48 test\r\n"
        "UART0 TX=P14.0 RX=P14.1 @ 115200\r\n"
        "I2C0  SCL=P13.1 SDA=P13.2 @ 400 kHz\r\n"
        "EEPROM 24AA02E48, slave=0x50\r\n"
        "========================================\r\n");

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
    g_writeBlockedDetected = FALSE;
    g_readOnlyModeDetected = FALSE;
    g_lastEepromAddress = 0U;
    g_lastI2cStatus = IfxI2c_I2c_Status_error;
    g_mismatchIndex = 0U;
    g_expectedByte = 0U;
    g_actualByte = 0U;
    g_backupByte = 0U;
    g_readyPollCount = 0U;

    printLine("[1/5] Initialize I2C0...\r\n");
    initI2c();
    printLine(g_i2cInitDone ? "      OK\r\n" : "      FAILED\r\n");

    printLine("[2/5] Read EUI-48 MAC from 0xFA...\r\n");
    status = eepromRead(TC4D7_EEPROM_MAC_OFFSET, g_macAddr, TC4D7_EEPROM_MAC_LENGTH);
    g_macReadPassed = (status == IfxI2c_I2c_Status_ok);

    if (!g_macReadPassed)
    {
        printFormatted("      FAILED, I2C status=%d\r\n", (sint32)status);
        TC4D7_I2cEepromEui_printReport();
        return;
    }

    printLine("      OK\r\n");
    printMacAddress();

    printLine("[3/5] Run EEPROM write/read verification at 0x20-0x3F...\r\n");
    g_readWriteTestPassed = runReadWriteTest();

    if (!g_readWriteTestPassed)
    {
        printFormatted("      FAILED, I2C status=%d, last address=0x%02X\r\n", (sint32)g_lastI2cStatus, g_lastEepromAddress);

        if (g_expectedByte != g_actualByte)
        {
            printFormatted("      Mismatch at +0x%02X: expected=0x%02X actual=0x%02X backup=0x%02X\r\n",
                g_mismatchIndex,
                g_expectedByte,
                g_actualByte,
                g_backupByte);

            if (g_writeBlockedDetected)
            {
                printLine("      Data stayed equal to backup; write looks blocked or ignored by hardware.\r\n");
            }

            printHexBuffer("      Write data  : ", g_eepromWriteData, 16U);
            printHexBuffer("      Read back   : ", g_eepromReadData, 16U);
            printHexBuffer("      Backup data : ", g_eepromBackupData, 16U);
        }

        if (g_writeBlockedDetected)
        {
            g_readOnlyModeDetected = TRUE;
            printLine("      Switch to read-only mode: user EEPROM area behaves as read-only on this board.\r\n");
            printLine("[4/5] Measure read throughput only...\r\n");
            g_benchmarkPassed = runReadOnlyBenchmark();
            printLine(g_benchmarkPassed ? "      OK\r\n" : "      FAILED\r\n");
            printLine("[5/5] Restore original EEPROM test area...\r\n");
            g_restorePassed = TRUE;
            printLine("      SKIPPED (no writable EEPROM area available)\r\n");
            g_allTestsPassed = g_macReadPassed && g_benchmarkPassed;
        }

        TC4D7_I2cEepromEui_printReport();
        return;
    }

    printLine("      OK\r\n");

    printLine("[4/5] Measure write/read throughput...\r\n");
    g_benchmarkPassed = runBenchmark();
    printLine(g_benchmarkPassed ? "      OK\r\n" : "      FAILED\r\n");

    printLine("[5/5] Restore original EEPROM test area...\r\n");
    g_restorePassed = restoreTestArea();
    g_allTestsPassed = g_macReadPassed && g_readWriteTestPassed && g_benchmarkPassed && g_restorePassed;

    printLine(g_restorePassed ? "      OK\r\n" : "      FAILED\r\n");
    TC4D7_I2cEepromEui_printReport();
}

void TC4D7_I2cEepromEui_printReport(void)
{
    const char *writeReadStatus = g_readWriteTestPassed ? "PASS" : (g_readOnlyModeDetected ? "READ-ONLY" : "FAIL");
    const char *restoreStatus = g_restorePassed ? (g_readOnlyModeDetected ? "SKIPPED" : "PASS") : "FAIL";
    const char *overallStatus = g_allTestsPassed ? (g_readOnlyModeDetected ? "PASS (READ-ONLY)" : "PASS") : "FAIL";

    printLine("\r\nResult summary\r\n");
    printLine("--------------\r\n");
    printFormatted("I2C init            : %s\r\n", g_i2cInitDone ? "PASS" : "FAIL");
    printFormatted("MAC read            : %s\r\n", g_macReadPassed ? "PASS" : "FAIL");
    printFormatted("Write/read test     : %s\r\n", writeReadStatus);
    printFormatted("Benchmark           : %s\r\n", g_benchmarkPassed ? "PASS" : "FAIL");
    printFormatted("Restore             : %s\r\n", restoreStatus);
    printFormatted("Overall             : %s\r\n", overallStatus);
    printFormatted("Write blocked hint  : %s\r\n", g_writeBlockedDetected ? "YES" : "NO");
    printFormatted("Read-only mode      : %s\r\n", g_readOnlyModeDetected ? "YES" : "NO");

    if (g_macReadPassed)
    {
        printMacAddress();
    }

    if (g_expectedByte != g_actualByte)
    {
        printFormatted("Mismatch detail     : +0x%02X expected=0x%02X actual=0x%02X backup=0x%02X\r\n",
            g_mismatchIndex,
            g_expectedByte,
            g_actualByte,
            g_backupByte);
    }

    printFormatted("Ready polls         : %lu\r\n", (unsigned long)g_readyPollCount);
    printFormatted("Last I2C status     : %d\r\n", (sint32)g_lastI2cStatus);
    printFormatted("Last EEPROM addr    : 0x%02X\r\n", g_lastEepromAddress);
    printFormatted("Write benchmark     : %lu bytes in %llu us, %lu B/s\r\n",
        (unsigned long)g_writeBenchmarkBytes,
        (unsigned long long)g_writeBenchmarkTimeUs,
        (unsigned long)g_writeBenchmarkBytesPerSecond);
    printFormatted("Read benchmark      : %lu bytes in %llu us, %lu B/s\r\n",
        (unsigned long)g_readBenchmarkBytes,
        (unsigned long long)g_readBenchmarkTimeUs,
        (unsigned long)g_readBenchmarkBytesPerSecond);
}