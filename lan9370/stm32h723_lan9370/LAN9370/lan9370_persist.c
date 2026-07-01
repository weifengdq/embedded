/**
  ******************************************************************************
  * @file    lan9370_persist.c
  * @brief   LAN9370 settings persistence in STM32H723 internal Flash
  * @details Uses last Flash sector (sector 7, 128KB) for config blob storage.
  *          H723ZG: single-bank 1MB Flash, 8 sectors of 128KB each.
  ******************************************************************************
  */

#include "lan9370_persist.h"
#include "stm32h7xx_hal.h"
#include <string.h>

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payloadSize;
    uint32_t crc32;
    uint8_t  padding[8];  /* align to 32 bytes */
    LAN9370_PersistSettings_t settings;
} PersistBlob_t;

#define PERSIST_MAGIC             0x4C394337UL /* 'L9C7' */
#define PERSIST_VERSION           1U
/* H723ZG: single bank, 8 sectors (0..7), each 128KB.
 * Use last sector for config storage. */
#define PERSIST_FLASH_SECTOR      7U
#define PERSIST_FLASH_BANK        FLASH_BANK_1
#define PERSIST_FLASH_BASE        (0x08000000UL + (128UL * 1024UL * 7UL))
#define PERSIST_WRITE_GRANULARITY 32U

static uint32_t persist_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (uint32_t j = 0; j < 8U; j++) {
            if ((crc & 1UL) != 0UL)
                crc = (crc >> 1) ^ 0xEDB88320UL;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

static const PersistBlob_t *persist_blob_ptr(void)
{
    return (const PersistBlob_t *)PERSIST_FLASH_BASE;
}

static int persist_blob_valid(const PersistBlob_t *blob)
{
    if (blob == NULL) return 0;
    if (blob->magic != PERSIST_MAGIC ||
        blob->version != PERSIST_VERSION ||
        blob->payloadSize != sizeof(LAN9370_PersistSettings_t))
        return 0;
    uint32_t crc = persist_crc32((const uint8_t *)&blob->settings, sizeof(LAN9370_PersistSettings_t));
    return (crc == blob->crc32) ? 1 : 0;
}

int LAN9370_PersistLoad(LAN9370_PersistSettings_t *settings)
{
    if (settings == NULL) return -1;
    const PersistBlob_t *blob = persist_blob_ptr();
    if (!persist_blob_valid(blob)) return -1;
    memcpy(settings, &blob->settings, sizeof(*settings));
    return 0;
}

int LAN9370_PersistErase(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sectorError = 0;

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks     = PERSIST_FLASH_BANK;
    erase.Sector    = PERSIST_FLASH_SECTOR;
    erase.NbSectors = 1;
    HAL_StatusTypeDef s = HAL_FLASHEx_Erase(&erase, &sectorError);
    HAL_FLASH_Lock();
    return (s == HAL_OK) ? 0 : -1;
}

int LAN9370_PersistSave(const LAN9370_PersistSettings_t *settings)
{
    if (settings == NULL) return -1;

    PersistBlob_t blob;
    memset(&blob, 0xFF, sizeof(blob));
    blob.magic       = PERSIST_MAGIC;
    blob.version     = PERSIST_VERSION;
    blob.payloadSize = (uint16_t)sizeof(LAN9370_PersistSettings_t);
    memcpy(&blob.settings, settings, sizeof(blob.settings));
    blob.crc32 = persist_crc32((const uint8_t *)&blob.settings, sizeof(blob.settings));

    uint32_t totalSize  = (uint32_t)sizeof(blob);
    uint32_t paddedSize = (totalSize + (PERSIST_WRITE_GRANULARITY - 1U)) & ~(PERSIST_WRITE_GRANULARITY - 1U);

    if (LAN9370_PersistErase() != 0) return -1;

    HAL_FLASH_Unlock();
    for (uint32_t addr = PERSIST_FLASH_BASE; addr < (PERSIST_FLASH_BASE + paddedSize); addr += PERSIST_WRITE_GRANULARITY) {
        uint32_t offset = addr - PERSIST_FLASH_BASE;
        HAL_StatusTypeDef s = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addr, (uint32_t)(uintptr_t)&((uint8_t*)&blob)[offset]);
        if (s != HAL_OK) { HAL_FLASH_Lock(); return -1; }
    }
    HAL_FLASH_Lock();
    return 0;
}
