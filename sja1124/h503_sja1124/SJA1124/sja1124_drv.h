/*
 * SJA1124 Quad LIN Commander Driver for STM32 HAL
 * Adapted from NXP reference implementation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SJA1124_DRV_H_
#define SJA1124_DRV_H_

#include <stdint.h>
#include <stdbool.h>
#include "sja1124.h"
#include "stm32h5xx_hal.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define MAX_LIN_CHANNELS            (4U)
#define WAKEUP_CHARACTER            (0xAAU)
#define SJA1124_MAX_DEVICE_COUNT    (4)
#define SJA1124_HS_BAUDRATE         (20000)
#define SJA1124_SPI_CMD_LEN         (2)

/*==================================================================================================
 *                                              ENUMS
==================================================================================================*/

typedef enum {
    SJA1124_SUCCESS = 0,
    SJA1124_FAIL,
    SJA1124_ERR_SPI,
    SJA1124_ERR_INIT,
    SJA1124_ERR_PARAM
} SJA1124_StatusType;

typedef enum {
    LIN_CHANNEL_1 = 0,
    LIN_CHANNEL_2,
    LIN_CHANNEL_3,
    LIN_CHANNEL_4
} SJA1124_LinChannelType;

typedef enum {
    STATE_SLEEP = 0,
    STATE_INITIALIZATION,
    STATE_IDLE,
    STATE_BREAK_TRANS_ONGOING,
    STATE_BREAK_COMPLETE_DELIMITER_ONGOING,
    STATE_FIELD_TRANS_ONGOING,
    STATE_ID_TRANS_ONGOING,
    STATE_HEADER_TRANS_COMPLETE,
    STATE_RESPONSE_TRANS_ONGOING,
    STATE_DATA_COMPLETE_CHECKSUM_ONGOING
} SJA1124_LinChannelStateType;

typedef enum {
    RECEIVER_IDLE = 0,
    RECEIVER_BUSY
} SJA1124_ReceiverStateType;

typedef enum {
    MASTER_BREAK_LENGTH_10_BITS = 0,
    MASTER_BREAK_LENGTH_11_BITS,
    MASTER_BREAK_LENGTH_12_BITS,
    MASTER_BREAK_LENGTH_13_BITS,
    MASTER_BREAK_LENGTH_14_BITS,
    MASTER_BREAK_LENGTH_15_BITS,
    MASTER_BREAK_LENGTH_16_BITS,
    MASTER_BREAK_LENGTH_17_BITS,
    MASTER_BREAK_LENGTH_18_BITS,
    MASTER_BREAK_LENGTH_19_BITS,
    MASTER_BREAK_LENGTH_20_BITS,
    MASTER_BREAK_LENGTH_21_BITS,
    MASTER_BREAK_LENGTH_22_BITS,
    MASTER_BREAK_LENGTH_23_BITS,
    MASTER_BREAK_LENGTH_36_BITS,
    MASTER_BREAK_LENGTH_50_BITS
} SJA1124_MasterBreakLenType;

typedef enum {
    DELIMITER_1_BIT = 0,
    DELIMITER_2_BIT
} SJA1124_DelimiterType;

typedef enum {
    MULT_FACTOR_78 = 0,   /* Fin = 0.4-0.5 MHz */
    MULT_FACTOR_65,       /* Fin = 0.5-0.7 MHz */
    MULT_FACTOR_39,       /* Fin = 0.7-1.0 MHz */
    MULT_FACTOR_28,       /* Fin = 1.0-1.4 MHz */
    MULT_FACTOR_20,       /* Fin = 1.4-1.9 MHz */
    MULT_FACTOR_15,       /* Fin = 1.9-2.6 MHz */
    MULT_FACTOR_11,       /* Fin = 2.6-3.5 MHz */
    MULT_FACTOR_8_5,      /* Fin = 3.5-4.5 MHz */
    MULT_FACTOR_6_4,      /* Fin = 4.5-6.0 MHz */
    MULT_FACTOR_4_8,      /* Fin = 6.0-8.0 MHz */
    MULT_FACTOR_3_9       /* Fin = 8.0-10.0 MHz */
} SJA1124_MultiplicationFactorType;

typedef enum {
    DO_NOT_RESET_STATE_MACHINE = 0,
    RESET_STATE_MACHINE
} SJA1124_OnEventActionType;

typedef enum {
    MODE_NORMAL = 0,
    MODE_SLEEP
} SJA1124_LinChannelModeType;

typedef enum {
    STOP_BIT_1 = 0,
    STOP_BIT_2
} SJA1124_StopBitConfigType;

typedef enum {
    RESPONSE_RECEIVE = 0,
    RESPONSE_SEND
} SJA1124_ResponseDirType;

typedef enum {
    CHECKSUM_ENHANCED = 0,
    CHECKSUM_CLASSIC
} SJA1124_ChecksumType;

typedef enum {
    HARDWARE_CHECKSUM = 0,
    SOFTWARE_CHECKSUM
} SJA1124_ChecksumCalcType;

typedef enum {
    LIN_NOT_OK = 0,
    LIN_TX_OK,
    LIN_TX_BUSY,
    LIN_TX_HEADER_ERROR,
    LIN_TX_ERROR,
    LIN_RX_OK,
    LIN_RX_BUSY,
    LIN_RX_ERROR,
    LIN_RX_NO_RESPONSE,
    LIN_OPERATIONAL,
    LIN_CH_SLEEP
} SJA1124_LinStatusType;

typedef enum {
    INT_FRAME_ERROR = 0U,
    INT_CHECKSUM_ERROR = 4U,
    INT_BIT_ERROR,
    INT_TIMEOUT_ERROR,
    INT_STUCK_AT_ZERO,
    INT_DATA_TRANSMISSION_COMPLETE = 9U,
    INT_DATA_RECEPTION_COMPLETE,
    INT_DATA_RECEPTION_BUF_NOT_EMPTY = 14U,
    NO_SECOND_LEVEL_INT
} SJA1124_SecondLevelIntType;

/*==================================================================================================
 *                                  STRUCTURES
==================================================================================================*/

/**
 * @brief STM32 HAL SPI handle for SJA1124
 */
typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
    bool               initialized;
} SJA1124_SpiHandle_t;

typedef uint32_t SJA1124_TopLevelInterruptsType;
typedef uint32_t SJA1124_SecondLevelInterruptsType;

/**
 * @brief Global interrupts configuration for a LIN channel.
 */
typedef struct {
    int WakeupIntEn;
    int ControllerErrIntEn;
    int ControllerStatusIntEn;
} SJA1124_LinGlobalIntConfigType;

/**
 * @brief Top level interrupts configuration.
 */
typedef struct {
    int OvertempIntEn;
    int PllOutOfLockIntEn;
    int PllInLockIntEn;
    int PllInFreqFailIntEn;
    int SpiErrIntEn;
    SJA1124_LinGlobalIntConfigType LinGlobalIntConfig[MAX_LIN_CHANNELS];
} SJA1124_TopLevelIntConfigType;

/**
 * @brief Second level interrupts configuration (per channel).
 */
typedef struct {
    int StuckAtZeroIntEn;
    int TimeoutIntEn;
    int BitErrIntEn;
    int ChecksumErrIntEn;
    int DataRecepCompleteIntEn;
    int DataTransCompleteIntEn;
    int FrameErrIntEn;
} SJA1124_SecondLevelIntConfigType;

/**
 * @brief LIN channel configuration.
 */
typedef struct {
    int                           ChecksumCalcEn;
    SJA1124_MasterBreakLenType    LinMasterBreakLength;
    SJA1124_LinChannelModeType    Mode;
    SJA1124_DelimiterType         Delimiter;
    SJA1124_OnEventActionType     IdleOnBitErrorEn;
    SJA1124_OnEventActionType     IdleOnTimeoutEn;
    SJA1124_StopBitConfigType     StopBitConfig;
    uint8_t                       ResponseTimeout;
    uint8_t                       FractionalBaudrate;
    uint16_t                      IntegerBaudrate;
    SJA1124_SecondLevelIntConfigType SecondLevelIntConfig;
} SJA1124_LinChannelConfigType;

/**
 * @brief LIN channel state.
 */
typedef struct {
    SJA1124_LinChannelStateType LinChannelState;
    SJA1124_ReceiverStateType   LinReceiverState;
} SJA1124_LinStateType;

/**
 * @brief LIN frame structure.
 */
typedef struct {
    uint8_t                  Pid;
    uint8_t                  DataFieldLength;
    SJA1124_ResponseDirType  ResponseDir;
    SJA1124_ChecksumType     ChecksumType;
    uint8_t                  Checksum;
    uint8_t                 *Data;
} SJA1124_LinFrameType;

/**
 * @brief LIN channel controller configuration.
 */
typedef struct {
    int HighSpeedModeEn;
    int SyncFrameTransmissionEn;
} SJA1124_LinChanControllerConfigType;

/**
 * @brief Maps LIN channel configuration to physical channel.
 */
typedef struct {
    SJA1124_LinChannelType       LinChannelMapping;
    SJA1124_LinChannelConfigType LinChannelConfig;
} SJA1124_LinChannelMappingType;

/**
 * @brief List of configured LIN channels.
 */
typedef struct {
    uint8_t                       ConfiguredChannelsNumber;
    SJA1124_LinChannelMappingType LinChannels[MAX_LIN_CHANNELS];
} SJA1124_LinChannelListType;

/**
 * @brief Device configuration structure.
 */
typedef struct {
    SJA1124_TopLevelIntConfigType       TopLevelIntConfig;
    SJA1124_LinChanControllerConfigType ChanControllerConfig[MAX_LIN_CHANNELS];
    SJA1124_MultiplicationFactorType    MultiplicationFactor;
    SJA1124_LinChannelListType          LinChannelList;
    SJA1124_SpiHandle_t                *spiHandle;
    uint32_t                            Freq;
} SJA1124_DeviceConfigType;

typedef struct {
    int32_t                 IsChannelInSleep;
    SJA1124_ResponseDirType ResponseDir;
} SJA1124_GetStatusType;

/*==================================================================================================
 *                                  MACROS
==================================================================================================*/
#define SET_INTERRUPT(InterruptsList, Interrupt) \
    ((InterruptsList) | (0x1U << ((uint32_t)(Interrupt))))

#define IS_INTERRUPT_SET(InterruptsList, Interrupt) \
    ((int32_t)(((InterruptsList) >> ((uint32_t)(Interrupt))) & 0x1U))

/*==================================================================================================
 *                                       FUNCTION PROTOTYPES
==================================================================================================*/

/* Low-level SPI */
SJA1124_StatusType SJA1124_SPI_Init(SJA1124_SpiHandle_t *handle,
                                     SPI_HandleTypeDef *hspi,
                                     GPIO_TypeDef *cs_port,
                                     uint16_t cs_pin);
SJA1124_StatusType SJA1124_WriteReg(SJA1124_SpiHandle_t *handle, uint8_t reg, uint8_t data);
SJA1124_StatusType SJA1124_ReadReg(SJA1124_SpiHandle_t *handle, uint8_t reg, uint8_t *data);

/* Device level */
SJA1124_StatusType SJA1124_GetDefaultConfig(SJA1124_DeviceConfigType *config,
                                             SJA1124_SpiHandle_t *handle);
SJA1124_StatusType SJA1124_DeviceInit(uint8_t device, uint32_t freqKHz,
                                       SJA1124_DeviceConfigType *config, uint8_t powerCycle);
SJA1124_StatusType SJA1124_DeviceDeinit(uint8_t device);

/* Channel level */
SJA1124_StatusType SJA1124_ChannelInit(uint8_t device, uint32_t baudrate,
                                        SJA1124_LinChannelType channel);
SJA1124_StatusType SJA1124_SetLinChannelMode(SJA1124_SpiHandle_t *handle,
                                              SJA1124_LinChannelType channel,
                                              SJA1124_LinChannelModeType mode);
SJA1124_StatusType SJA1124_EnterLinChannelInitMode(uint8_t device,
                                                    SJA1124_LinChannelType channel);
SJA1124_StatusType SJA1124_LeaveLinChannelInitMode(uint8_t device,
                                                    SJA1124_LinChannelType channel);

/* Frame operations */
SJA1124_StatusType SJA1124_SendFrame(uint8_t device, SJA1124_LinChannelType channel,
                                      SJA1124_LinFrameType *frame);
SJA1124_StatusType SJA1124_SendHeader(uint8_t device, SJA1124_LinChannelType channel,
                                       SJA1124_LinFrameType *frame);
SJA1124_StatusType SJA1124_GetData(uint8_t device, SJA1124_LinChannelType channel,
                                    SJA1124_LinFrameType *frame,
                                    SJA1124_SecondLevelIntType *intType);
SJA1124_StatusType SJA1124_SendWakeupRequest(uint8_t device, SJA1124_LinChannelType channel);
SJA1124_StatusType SJA1124_SendAbortRequest(uint8_t device, SJA1124_LinChannelType channel);

/* Status */
SJA1124_StatusType SJA1124_GetLinState(uint8_t device, SJA1124_LinChannelType channel,
                                        SJA1124_LinStateType *state);
uint8_t SJA1124_GetChannelStatus(uint8_t device, SJA1124_LinChannelType channel);
SJA1124_LinStatusType SJA1124_GetStatus(uint8_t device, SJA1124_LinChannelType channel);
SJA1124_StatusType SJA1124_GetDeviceId(uint8_t device, uint8_t *id);

/* Interrupts */
SJA1124_StatusType SJA1124_GetTopLevelInterrupts(uint8_t device,
                                                  SJA1124_TopLevelInterruptsType *interrupts);
SJA1124_StatusType SJA1124_ClearTopLevelInterrupts(uint8_t device,
                                                    SJA1124_TopLevelInterruptsType interrupts);
SJA1124_StatusType SJA1124_GetSecondLevelInterrupts(uint8_t device,
                                                     SJA1124_LinChannelType channel,
                                                     SJA1124_SecondLevelInterruptsType *interrupts);
SJA1124_StatusType SJA1124_ClearSecondLevelInterrupts(uint8_t device,
                                                       SJA1124_LinChannelType channel,
                                                       SJA1124_SecondLevelInterruptsType interrupts);

/* Configuration */
SJA1124_StatusType SJA1124_SetMBL(uint8_t device, SJA1124_LinChannelType channel,
                                   SJA1124_MasterBreakLenType mbl);
SJA1124_StatusType SJA1124_SetTBDE(uint8_t device, SJA1124_LinChannelType channel,
                                    SJA1124_DelimiterType tbde);
SJA1124_StatusType SJA1124_SetStopBits(uint8_t device, SJA1124_LinChannelType channel,
                                        SJA1124_StopBitConfigType stopBits);
SJA1124_StatusType SJA1124_EnableHwSwChecksum(uint8_t device, SJA1124_LinChannelType channel,
                                               SJA1124_ChecksumCalcType calc);

/* Power */
SJA1124_StatusType SJA1124_EnterLowPowerMode(uint8_t device);
SJA1124_StatusType SJA1124_ExitLowPowerMode(uint8_t device);

/* Utility */
uint8_t SJA1124_CalculateChecksum(SJA1124_ChecksumType type, uint8_t pid,
                                   uint8_t *data, uint8_t length);
void SJA1124_CalBaudRateReg(uint8_t device, uint32_t freqKHz, uint32_t baudrate,
                             uint8_t *data);
SJA1124_StatusType SJA1124_ProcessSecondLevelInterrupt(uint8_t device,
                                                        SJA1124_LinChannelType channel,
                                                        SJA1124_SecondLevelIntType *intType);

/* Register read/write (device level) */
SJA1124_StatusType SJA1124_DRV_WriteRegister(uint8_t device, uint8_t reg, uint8_t data);
SJA1124_StatusType SJA1124_DRV_ReadRegister(uint8_t device, uint8_t reg, uint8_t *data);

#endif /* SJA1124_DRV_H_ */
