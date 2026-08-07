#include "EthernetMacEeprom.h"

#include "I2c/I2c/IfxI2c_I2c.h"
#include "kit_tc4d7_lite.h"

#define ETHERNET_MAC_EEPROM_BAUDRATE 400000U
#define ETHERNET_MAC_EEPROM_DEVICE_ADDRESS 0x50U
#define ETHERNET_MAC_EEPROM_DEVICE_ADDRESS_8BIT (ETHERNET_MAC_EEPROM_DEVICE_ADDRESS << 1)
#define ETHERNET_MAC_EEPROM_MAC_OFFSET 0xFAU
#define ETHERNET_MAC_EEPROM_READY_TIMEOUT_POLLS 100000U

static IfxI2c_I2c g_eepromI2cHandle;
static IfxI2c_I2c_Device g_eepromDevice;
static boolean g_eepromInitialized = FALSE;

static const IfxI2c_Pins g_eepromI2cPins = {
    &BOARD_I2C_SCL,
    &BOARD_I2C_SDA,
    IfxPort_PadDriver_ttlSpeed1
};

static boolean EthernetMacEeprom_init(void)
{
    IfxI2c_I2c_Config moduleConfig;
    IfxI2c_I2c_deviceConfig deviceConfig;

    if (g_eepromInitialized != FALSE)
    {
        return TRUE;
    }

    IfxI2c_I2c_initConfig(&moduleConfig, &MODULE_I2C0);
    moduleConfig.pins = &g_eepromI2cPins;
    moduleConfig.baudrate = ETHERNET_MAC_EEPROM_BAUDRATE;
    moduleConfig.mode = IfxI2c_Mode_StandardAndFast;
    IfxI2c_I2c_initModule(&g_eepromI2cHandle, &moduleConfig);

    IfxI2c_I2c_initDeviceConfig(&deviceConfig, &g_eepromI2cHandle);
    deviceConfig.deviceAddress = ETHERNET_MAC_EEPROM_DEVICE_ADDRESS_8BIT;
    deviceConfig.enableRepeatedStart = FALSE;
    IfxI2c_I2c_initDevice(&g_eepromDevice, &deviceConfig);

    g_eepromInitialized = TRUE;
    return TRUE;
}

static IfxI2c_I2c_Status EthernetMacEeprom_writeInternalAddress(uint8 wordAddress)
{
    uint8 txBuffer[1] = {wordAddress};
    uint32 attempt;
    IfxI2c_I2c_Status status = IfxI2c_I2c_Status_error;

    for (attempt = 0U; attempt < ETHERNET_MAC_EEPROM_READY_TIMEOUT_POLLS; ++attempt)
    {
        status = IfxI2c_I2c_write(&g_eepromDevice, txBuffer, 1U);

        if (status != IfxI2c_I2c_Status_nak)
        {
            return status;
        }
    }

    return status;
}

static IfxI2c_I2c_Status EthernetMacEeprom_readBytes(uint8 wordAddress, uint8 *buffer, Ifx_SizeT size)
{
    uint32 attempt;
    IfxI2c_I2c_Status status;

    status = EthernetMacEeprom_writeInternalAddress(wordAddress);

    if (status != IfxI2c_I2c_Status_ok)
    {
        return status;
    }

    for (attempt = 0U; attempt < ETHERNET_MAC_EEPROM_READY_TIMEOUT_POLLS; ++attempt)
    {
        status = IfxI2c_I2c_read(&g_eepromDevice, buffer, size);

        if (status != IfxI2c_I2c_Status_nak)
        {
            return status;
        }
    }

    return status;
}

boolean EthernetMacEeprom_read(uint8 macAddress[ETHERNET_MAC_EEPROM_LENGTH])
{
    if (macAddress == NULL_PTR)
    {
        return FALSE;
    }

    if (EthernetMacEeprom_init() == FALSE)
    {
        return FALSE;
    }

    return EthernetMacEeprom_readBytes(ETHERNET_MAC_EEPROM_MAC_OFFSET,
                                       macAddress,
                                       ETHERNET_MAC_EEPROM_LENGTH) == IfxI2c_I2c_Status_ok;
}