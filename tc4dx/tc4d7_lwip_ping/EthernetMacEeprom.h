#ifndef ETHERNET_MAC_EEPROM_H
#define ETHERNET_MAC_EEPROM_H 1

#include "Ifx_Types.h"

#define ETHERNET_MAC_EEPROM_LENGTH 6U

boolean EthernetMacEeprom_read(uint8 macAddress[ETHERNET_MAC_EEPROM_LENGTH]);

#endif