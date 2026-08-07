#ifndef SERIALIO_H
#define SERIALIO_H

#include <stdio.h>
#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

void SERIALIO_Init(sint32 baudrate);
boolean SERIALIO_TryReadByte(uint8 *byte);
boolean SERIALIO_WriteBuffer(const uint8 *data, Ifx_SizeT count);

#ifdef __cplusplus
}
#endif

#endif