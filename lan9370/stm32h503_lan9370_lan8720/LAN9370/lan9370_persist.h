#ifndef __LAN9370_PERSIST_H
#define __LAN9370_PERSIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t t1Mode[4];      /* 0=slave, 1=master for port1..4 */
    uint8_t mirrorDst[5];   /* 0=off, 1..5 dst for source port1..5 */
    uint16_t vlanVid[5];    /* Default VLAN ID for port1..5 */
    uint8_t vlanEnable;     /* 0=off, 1=on */
    uint8_t ptpEnable;      /* 0=off, 1=on */
    uint8_t gptpEnable;     /* 0=off, 1=on */
} LAN9370_PersistSettings_t;

int LAN9370_PersistLoad(LAN9370_PersistSettings_t *settings);
int LAN9370_PersistSave(const LAN9370_PersistSettings_t *settings);
int LAN9370_PersistErase(void);

#ifdef __cplusplus
}
#endif

#endif
