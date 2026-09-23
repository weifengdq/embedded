#ifndef __SHELL_SD_H
#define __SHELL_SD_H

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Configure SD card-detect pin P10.7 (input pull-up). Call once at boot. */
void SdShell_InitPins(void);

#ifdef __cplusplus
}
#endif

#endif
