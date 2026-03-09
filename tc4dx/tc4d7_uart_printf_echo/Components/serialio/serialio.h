#ifndef SERIALIO_H
#define SERIALIO_H

#include <stdio.h>
#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

void SERIALIO_Init(sint32 baudrate);
boolean SERIALIO_TryReadByte(uint8 *byte);

#ifdef __cplusplus
}
#endif

#endif