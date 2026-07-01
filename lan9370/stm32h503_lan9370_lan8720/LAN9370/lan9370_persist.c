#include "lan9370_persist.h"

#include "main.h"
#include <string.h>

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payloadSize;
    uint32_t crc32;
    LAN9370_PersistSettings_t settings;
} PersistBlob_t;

#define PERSIST_MAGIC            0x4C394337UL /* 'L9C7' */
#define PERSIST_VERSION          1U
#define PERSIST_FLASH_SECTOR     (FLASH_SECTOR_NB - 1U)
#define PERSIST_FLASH_BANK       FLASH_BANK_2
#define PERSIST_FLASH_BASE       (FLASH_BASE + FLASH_SIZE - FLASH_SECTOR_SIZE)
#define PERSIST_WRITE_GRANULARITY 16U

static uint32_t persist_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint32_t j;

    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (j = 0; j < 8U; j++) {
            if ((crc & 1UL) != 0UL) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
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
    uint32_t crc;

    if (blob == NULL) {
        return 0;
    }

    if (blob->magic != PERSIST_MAGIC ||
        blob->version != PERSIST_VERSION ||
        blob->payloadSize != sizeof(LAN9370_PersistSettings_t)) {
        return 0;
    }

    crc = persist_crc32((const uint8_t *)&blob->settings, sizeof(LAN9370_PersistSettings_t));
    return (crc == blob->crc32) ? 1 : 0;
}

int LAN9370_PersistLoad(LAN9370_PersistSettings_t *settings)
{
    const PersistBlob_t *blob = persist_blob_ptr();

    if (settings == NULL) {
        return -1;
    }

    if (!persist_blob_valid(blob)) {
        return -1;
    }

    memcpy(settings, &blob->settings, sizeof(*settings));
    return 0;
}

int LAN9370_PersistErase(void)
{
    HAL_StatusTypeDef halStatus;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sectorError = 0;

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = PERSIST_FLASH_BANK;
    erase.Sector = PERSIST_FLASH_SECTOR;
    erase.NbSectors = 1;

    halStatus = HAL_FLASHEx_Erase(&erase, &sectorError);
    HAL_FLASH_Lock();

    return (halStatus == HAL_OK) ? 0 : -1;
}

int LAN9370_PersistSave(const LAN9370_PersistSettings_t *settings)
{
    PersistBlob_t blob;
    HAL_StatusTypeDef halStatus;
    uint32_t addr;
    uint8_t *blobBytes = (uint8_t *)&blob;
    uint32_t totalSize = (uint32_t)sizeof(blob);
    uint32_t paddedSize = (totalSize + (PERSIST_WRITE_GRANULARITY - 1U)) & ~(PERSIST_WRITE_GRANULARITY - 1U);

    if (settings == NULL) {
        return -1;
    }

    memset(&blob, 0xFF, sizeof(blob));
    blob.magic = PERSIST_MAGIC;
    blob.version = PERSIST_VERSION;
    blob.payloadSize = (uint16_t)sizeof(LAN9370_PersistSettings_t);
    memcpy(&blob.settings, settings, sizeof(blob.settings));
    blob.crc32 = persist_crc32((const uint8_t *)&blob.settings, sizeof(blob.settings));

    if (LAN9370_PersistErase() != 0) {
        return -1;
    }

    HAL_FLASH_Unlock();

    for (addr = PERSIST_FLASH_BASE; addr < (PERSIST_FLASH_BASE + paddedSize); addr += PERSIST_WRITE_GRANULARITY) {
        uint32_t offset = addr - PERSIST_FLASH_BASE;
        halStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr, (uint32_t)(uintptr_t)&blobBytes[offset]);
        if (halStatus != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}
