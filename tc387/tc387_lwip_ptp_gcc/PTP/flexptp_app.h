#ifndef FLEXPTP_APP_H
#define FLEXPTP_APP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FLEXPTP_MODE_NONE = 0,
    FLEXPTP_MODE_DEFAULT_PTP,
    FLEXPTP_MODE_GPTP
} FlexPTP_Mode;

int  FlexPTP_AppInit(void);
void FlexPTP_AppProcess(void);

int  FlexPTP_Start(void);
int  FlexPTP_StartGptp(void);
int  FlexPTP_Stop(void);
int  FlexPTP_IsRunning(void);

FlexPTP_Mode FlexPTP_GetMode(void);

void FlexPTP_PrintStatus(void);
void FlexPTP_PrintCommands(const char *prefix);
int  FlexPTP_DispatchCommand(int argc, char *argv[]);
int  FlexPTP_ShellPtp(int argc, char *argv[]);
int  FlexPTP_ShellTime(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif
