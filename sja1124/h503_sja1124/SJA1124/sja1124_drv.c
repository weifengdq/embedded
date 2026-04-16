/*
 * SJA1124 Quad LIN Commander Driver for STM32 HAL
 * Adapted from NXP reference implementation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sja1124_drv.h"
#include <string.h>

/*******************************************************************************
 * Global variables
 ******************************************************************************/
static SJA1124_DeviceConfigType *g_DeviceList[SJA1124_MAX_DEVICE_COUNT];

/*******************************************************************************
 * Internal helpers
 ******************************************************************************/
static inline void SJA1124_CS_Low(SJA1124_SpiHandle_t *h)
{
    HAL_GPIO_WritePin(h->cs_port, h->cs_pin, GPIO_PIN_RESET);
}

static inline void SJA1124_CS_High(SJA1124_SpiHandle_t *h)
{
    HAL_GPIO_WritePin(h->cs_port, h->cs_pin, GPIO_PIN_SET);
}

/*******************************************************************************
 * Low-level SPI read/write
 ******************************************************************************/
SJA1124_StatusType SJA1124_SPI_Init(SJA1124_SpiHandle_t *handle,
                                     SPI_HandleTypeDef *hspi,
                                     GPIO_TypeDef *cs_port,
                                     uint16_t cs_pin)
{
    if (!handle || !hspi) return SJA1124_ERR_PARAM;

    handle->hspi    = hspi;
    handle->cs_port = cs_port;
    handle->cs_pin  = cs_pin;
    handle->initialized = true;

    /* Ensure CS is deasserted (high) */
    SJA1124_CS_High(handle);

    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_WriteReg(SJA1124_SpiHandle_t *handle, uint8_t reg, uint8_t data)
{
    uint8_t txBuf[3];
    uint8_t rxBuf[3];
    HAL_StatusTypeDef status;

    /* SJA1124 SPI write: [RegAddr][0x00|(size-1)][Data] */
    txBuf[0] = reg;
    txBuf[1] = SJA1124_SPI_WRITE_CMD | 0x00; /* size=1, so size-1=0 */
    txBuf[2] = data;

    SJA1124_CS_Low(handle);
    status = HAL_SPI_TransmitReceive(handle->hspi, txBuf, rxBuf, 3, 100);
    SJA1124_CS_High(handle);

    return (status == HAL_OK) ? SJA1124_SUCCESS : SJA1124_ERR_SPI;
}

SJA1124_StatusType SJA1124_ReadReg(SJA1124_SpiHandle_t *handle, uint8_t reg, uint8_t *data)
{
    uint8_t txBuf[3];
    uint8_t rxBuf[3];
    HAL_StatusTypeDef status;

    /* SJA1124 SPI read: [RegAddr][0x80|(size-1)][dummy] -> data in rxBuf[2] */
    txBuf[0] = reg;
    txBuf[1] = SJA1124_SPI_READ_CMD | 0x00; /* size=1, so size-1=0 */
    txBuf[2] = 0x00; /* dummy */

    SJA1124_CS_Low(handle);
    status = HAL_SPI_TransmitReceive(handle->hspi, txBuf, rxBuf, 3, 100);
    SJA1124_CS_High(handle);

    if (status == HAL_OK) {
        *data = rxBuf[2];
        return SJA1124_SUCCESS;
    }
    return SJA1124_ERR_SPI;
}

/* Device-level register access wrappers */
SJA1124_StatusType SJA1124_DRV_WriteRegister(uint8_t device, uint8_t reg, uint8_t data)
{
    return SJA1124_WriteReg(g_DeviceList[device]->spiHandle, reg, data);
}

SJA1124_StatusType SJA1124_DRV_ReadRegister(uint8_t device, uint8_t reg, uint8_t *data)
{
    return SJA1124_ReadReg(g_DeviceList[device]->spiHandle, reg, data);
}

/*******************************************************************************
 * PLL multiplication factor
 ******************************************************************************/
static void SJA1124_SetMult(uint8_t device, uint32_t freqKHz, uint8_t *val)
{
    if      (freqKHz >= 400  && freqKHz < 500)  *val = MULT_FACTOR_78;
    else if (freqKHz >= 500  && freqKHz < 700)  *val = MULT_FACTOR_65;
    else if (freqKHz >= 700  && freqKHz < 1000) *val = MULT_FACTOR_39;
    else if (freqKHz >= 1000 && freqKHz < 1400) *val = MULT_FACTOR_28;
    else if (freqKHz >= 1400 && freqKHz < 1900) *val = MULT_FACTOR_20;
    else if (freqKHz >= 1900 && freqKHz < 2600) *val = MULT_FACTOR_15;
    else if (freqKHz >= 2600 && freqKHz < 3500) *val = MULT_FACTOR_11;
    else if (freqKHz >= 3500 && freqKHz < 4500) *val = MULT_FACTOR_8_5;
    else if (freqKHz >= 4500 && freqKHz < 6000) *val = MULT_FACTOR_6_4;
    else if (freqKHz >= 6000 && freqKHz < 8000) *val = MULT_FACTOR_4_8;
    else                                         *val = MULT_FACTOR_3_9;

    g_DeviceList[device]->MultiplicationFactor = (SJA1124_MultiplicationFactorType)*val;
    g_DeviceList[device]->Freq = freqKHz;
}

static double SJA1124_GetPllMult(uint8_t device)
{
    switch (g_DeviceList[device]->MultiplicationFactor) {
        case MULT_FACTOR_78:  return 78.0;
        case MULT_FACTOR_65:  return 65.0;
        case MULT_FACTOR_39:  return 39.0;
        case MULT_FACTOR_28:  return 28.0;
        case MULT_FACTOR_20:  return 20.0;
        case MULT_FACTOR_15:  return 15.0;
        case MULT_FACTOR_11:  return 11.0;
        case MULT_FACTOR_8_5: return 8.5;
        case MULT_FACTOR_6_4: return 6.4;
        case MULT_FACTOR_4_8: return 4.8;
        case MULT_FACTOR_3_9: return 3.9;
        default:              return 3.9;
    }
}

void SJA1124_CalBaudRateReg(uint8_t device, uint32_t freqKHz, uint32_t baudrate, uint8_t *data)
{
    double pllMult = SJA1124_GetPllMult(device);
    double tempVal = (double)freqKHz * pllMult;
    uint16_t ibr;

    /* Fractional baudrate: Data[0] */
    data[0] = (uint8_t)((uint64_t)(tempVal * 1000 / baudrate) % 16);

    /* Integer baudrate: Data[1] (MSB), Data[2] (LSB) */
    ibr = (uint16_t)(tempVal * 1000 / (16 * baudrate));
    data[1] = (uint8_t)(ibr >> 8U);
    data[2] = (uint8_t)(ibr & 0xFFU);
}

/*******************************************************************************
 * Serialization helpers
 ******************************************************************************/
static void SerializeTopLevelInterrupts(SJA1124_TopLevelIntConfigType *tli, uint8_t *data)
{
    uint8_t idx;
    data[0] = 0; data[1] = 0; data[2] = 0;

    for (idx = 0; idx < MAX_LIN_CHANNELS; idx++) {
        data[0] |= (uint8_t)(tli->LinGlobalIntConfig[idx].WakeupIntEn << idx);
        data[2] |= (uint8_t)(((uint8_t)tli->LinGlobalIntConfig[idx].ControllerStatusIntEn << idx) |
                              ((uint8_t)tli->LinGlobalIntConfig[idx].ControllerErrIntEn << (idx + 4U)));
    }

    data[1] = (uint8_t)(((uint8_t)tli->OvertempIntEn      << SJA1124_INT2EN_OVERTMPWARNIEN_SHIFT) |
                         ((uint8_t)tli->PllOutOfLockIntEn  << SJA1124_INT2EN_PLLNOLOCKIEN_SHIFT) |
                         ((uint8_t)tli->PllInLockIntEn     << SJA1124_INT2EN_PLLINLOCKIEN_SHIFT) |
                         ((uint8_t)tli->PllInFreqFailIntEn << SJA1124_INT2EN_PLLFRQFAILIEN_SHIFT) |
                         ((uint8_t)tli->SpiErrIntEn        << SJA1124_INT2EN_SPIERRIEN_SHIFT));
}

static void SerializeSecondLevelInterrupts(SJA1124_SecondLevelIntConfigType *sli, uint8_t *data)
{
    *data = (uint8_t)(((uint8_t)sli->StuckAtZeroIntEn      << SJA1124_LIE_STUCKZEROIEN_SHIFT) |
                      ((uint8_t)sli->TimeoutIntEn           << SJA1124_LIE_TIMEOUTIEN_SHIFT) |
                      ((uint8_t)sli->BitErrIntEn            << SJA1124_LIE_BITERRIEN_SHIFT) |
                      ((uint8_t)sli->ChecksumErrIntEn       << SJA1124_LIE_CSERRIEN_SHIFT) |
                      ((uint8_t)sli->DataRecepCompleteIntEn << SJA1124_LIE_DTRCVIEN_SHIFT) |
                      ((uint8_t)sli->DataTransCompleteIntEn << SJA1124_LIE_DTTRANSIEN_SHIFT) |
                      ((uint8_t)sli->FrameErrIntEn          << SJA1124_LIE_FRAMEERRIEN_SHIFT));
}

static void SerializeLinFrame(SJA1124_LinFrameType *frame, uint8_t *data, uint8_t *bytesCount)
{
    data[0] = frame->Pid;
    data[1] = (uint8_t)(((uint8_t)frame->ChecksumType << SJA1124_LBC_CLASSICCS_SHIFT) |
                         ((uint8_t)frame->ResponseDir << SJA1124_LBC_DIRECTION_SHIFT) |
                         ((uint8_t)(frame->DataFieldLength - 1U) << SJA1124_LBC_DTFIELDLEN_SHIFT));
    data[2] = frame->Checksum;
    *bytesCount = 3U;

    if (RESPONSE_SEND == frame->ResponseDir) {
        memcpy(data + 3U, frame->Data, frame->DataFieldLength);
        *bytesCount += frame->DataFieldLength;
    }
}

static void SerializeLinCommander(uint8_t device, uint8_t *data, uint32_t baudrate,
                                   SJA1124_LinChannelType channel)
{
    uint8_t regData;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    SJA1124_LinChanControllerConfigType *cfg = &(g_DeviceList[device]->ChanControllerConfig[channel]);

    SJA1124_ReadReg(h, SJA1124_LCOM1, &regData);
    *data = regData;
    *data = (*data & ~(1 << channel)) | ((uint8_t)cfg->SyncFrameTransmissionEn << channel);
    *data = (*data & ~(1 << (channel + SJA1124_LCOM1_L1HSMODE_SHIFT))) |
            ((uint8_t)cfg->HighSpeedModeEn << (channel + SJA1124_LCOM1_L1HSMODE_SHIFT));
}

/*******************************************************************************
 * Default configuration
 ******************************************************************************/
SJA1124_StatusType SJA1124_GetDefaultConfig(SJA1124_DeviceConfigType *config,
                                             SJA1124_SpiHandle_t *handle)
{
    int i;
    config->spiHandle = handle;
    config->MultiplicationFactor = MULT_FACTOR_3_9;

    for (i = 0; i < MAX_LIN_CHANNELS; i++) {
        config->ChanControllerConfig[i].HighSpeedModeEn = 1;
        config->ChanControllerConfig[i].SyncFrameTransmissionEn = 1;
    }

    config->TopLevelIntConfig.OvertempIntEn = 0;
    config->TopLevelIntConfig.PllOutOfLockIntEn = 0;
    config->TopLevelIntConfig.PllInLockIntEn = 0;
    config->TopLevelIntConfig.PllInFreqFailIntEn = 0;
    config->TopLevelIntConfig.SpiErrIntEn = 0;

    for (i = 0; i < MAX_LIN_CHANNELS; i++) {
        config->TopLevelIntConfig.LinGlobalIntConfig[i].WakeupIntEn = 1;
        config->TopLevelIntConfig.LinGlobalIntConfig[i].ControllerErrIntEn = 0;
        config->TopLevelIntConfig.LinGlobalIntConfig[i].ControllerStatusIntEn = 0;
    }

    config->LinChannelList.ConfiguredChannelsNumber = MAX_LIN_CHANNELS;
    config->LinChannelList.LinChannels[0].LinChannelMapping = LIN_CHANNEL_1;
    config->LinChannelList.LinChannels[1].LinChannelMapping = LIN_CHANNEL_2;
    config->LinChannelList.LinChannels[2].LinChannelMapping = LIN_CHANNEL_3;
    config->LinChannelList.LinChannels[3].LinChannelMapping = LIN_CHANNEL_4;

    for (i = 0; i < MAX_LIN_CHANNELS; i++) {
        SJA1124_LinChannelConfigType *ch = &config->LinChannelList.LinChannels[i].LinChannelConfig;
        ch->ChecksumCalcEn = 0;
        ch->LinMasterBreakLength = MASTER_BREAK_LENGTH_13_BITS;
        ch->Mode = MODE_NORMAL;
        ch->Delimiter = DELIMITER_2_BIT;
        ch->IdleOnBitErrorEn = RESET_STATE_MACHINE;
        ch->IdleOnTimeoutEn = RESET_STATE_MACHINE;
        ch->StopBitConfig = STOP_BIT_1;
        ch->ResponseTimeout = 0x0E;
        ch->FractionalBaudrate = 0;
        ch->IntegerBaudrate = 0;
        ch->SecondLevelIntConfig.StuckAtZeroIntEn = 0;
        ch->SecondLevelIntConfig.TimeoutIntEn = 0;
        ch->SecondLevelIntConfig.BitErrIntEn = 0;
        ch->SecondLevelIntConfig.ChecksumErrIntEn = 0;
        ch->SecondLevelIntConfig.DataRecepCompleteIntEn = 0;
        ch->SecondLevelIntConfig.DataTransCompleteIntEn = 0;
        ch->SecondLevelIntConfig.FrameErrIntEn = 0;
    }

    return SJA1124_SUCCESS;
}

/*******************************************************************************
 * Channel mode / init mode
 ******************************************************************************/
SJA1124_StatusType SJA1124_SetLinChannelMode(SJA1124_SpiHandle_t *handle,
                                              SJA1124_LinChannelType channel,
                                              SJA1124_LinChannelModeType mode)
{
    uint8_t data;
    SJA1124_ReadReg(handle, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), &data);
    data &= (uint8_t)(~SJA1124_LCFG1_SLEEP_MASK) & (uint8_t)(~SJA1124_LCFG1_INIT_MASK);
    data |= (uint8_t)(mode << SJA1124_LCFG1_SLEEP_SHIFT);
    SJA1124_WriteReg(handle, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), data);
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_EnterLinChannelInitMode(uint8_t device, SJA1124_LinChannelType channel)
{
    SJA1124_StatusType status;
    SJA1124_LinStateType linState;
    uint8_t data;
    uint8_t retries = 20U;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    while (retries--) {
        status = SJA1124_SetLinChannelMode(h, channel, MODE_NORMAL);
        if (status != SJA1124_SUCCESS) {
            continue;
        }

        status = SJA1124_ReadReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), &data);
        if (status != SJA1124_SUCCESS) {
            continue;
        }

        data &= (uint8_t)(~SJA1124_LCFG1_SLEEP_MASK) & (uint8_t)(~SJA1124_LCFG1_INIT_MASK);
        data |= SJA1124_LCFG1_INIT_MASK;

        status = SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), data);
        if (status != SJA1124_SUCCESS) {
            continue;
        }

        HAL_Delay(1);
        status = SJA1124_GetLinState(device, channel, &linState);
        if ((status == SJA1124_SUCCESS) && (STATE_INITIALIZATION == linState.LinChannelState)) {
            return SJA1124_SUCCESS;
        }
    }

    return SJA1124_FAIL;
}

SJA1124_StatusType SJA1124_LeaveLinChannelInitMode(uint8_t device, SJA1124_LinChannelType channel)
{
    uint8_t data;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    if (SJA1124_ReadReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), &data) != SJA1124_SUCCESS) {
        return SJA1124_ERR_SPI;
    }
    data &= ~SJA1124_LCFG1_INIT_MASK;
    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), data) != SJA1124_SUCCESS) {
        return SJA1124_ERR_SPI;
    }

    return SJA1124_SUCCESS;
}

/*******************************************************************************
 * State / Status
 ******************************************************************************/
SJA1124_StatusType SJA1124_GetLinState(uint8_t device, SJA1124_LinChannelType channel,
                                        SJA1124_LinStateType *state)
{
    uint8_t regData = 0;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    SJA1124_StatusType st = SJA1124_ReadReg(h,
        SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LSTATE, channel), &regData);
    if (st != SJA1124_SUCCESS) return st;

    state->LinChannelState  = (SJA1124_LinChannelStateType)(regData & SJA1124_LSTATE_LINSTATE_MASK);
    state->LinReceiverState = (SJA1124_ReceiverStateType)(regData >> SJA1124_LSTATE_RCVBUSY_SHIFT);
    return SJA1124_SUCCESS;
}

uint8_t SJA1124_GetChannelStatus(uint8_t device, SJA1124_LinChannelType channel)
{
    uint8_t regData = 0;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    SJA1124_ReadReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LS, channel), &regData);
    return regData;
}

/*******************************************************************************
 * Interrupts
 ******************************************************************************/
SJA1124_StatusType SJA1124_GetTopLevelInterrupts(uint8_t device,
                                                  SJA1124_TopLevelInterruptsType *interrupts)
{
    uint8_t data[3];
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    /* Dummy read then real read */
    SJA1124_ReadReg(h, SJA1124_INT1, &data[0]);
    SJA1124_ReadReg(h, SJA1124_INT1, &data[0]);
    SJA1124_ReadReg(h, SJA1124_INT2, &data[1]);
    SJA1124_ReadReg(h, SJA1124_INT3, &data[2]);

    *interrupts = (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U);
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_ClearTopLevelInterrupts(uint8_t device,
                                                    SJA1124_TopLevelInterruptsType interrupts)
{
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    SJA1124_WriteReg(h, SJA1124_INT1, (uint8_t)(interrupts & 0xFF));
    SJA1124_WriteReg(h, SJA1124_INT2, (uint8_t)(interrupts >> 8));
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_GetSecondLevelInterrupts(uint8_t device,
                                                     SJA1124_LinChannelType channel,
                                                     SJA1124_SecondLevelInterruptsType *interrupts)
{
    uint8_t data[2];
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    SJA1124_ReadReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LES, channel), &data[0]);
    SJA1124_ReadReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LS, channel), &data[1]);

    *interrupts = (uint32_t)data[0] | ((uint32_t)data[1] << 8U);
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_ClearSecondLevelInterrupts(uint8_t device,
                                                       SJA1124_LinChannelType channel,
                                                       SJA1124_SecondLevelInterruptsType interrupts)
{
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LES, channel), (uint8_t)(interrupts & 0xFF));
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LS, channel), (uint8_t)(interrupts >> 8));
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_ProcessSecondLevelInterrupt(uint8_t device,
                                                        SJA1124_LinChannelType channel,
                                                        SJA1124_SecondLevelIntType *intType)
{
    SJA1124_SecondLevelInterruptsType sli = 0;
    SJA1124_SecondLevelInterruptsType clearInt = 0;

    SJA1124_GetSecondLevelInterrupts(device, channel, &sli);

    if      (IS_INTERRUPT_SET(sli, INT_DATA_TRANSMISSION_COMPLETE))  { *intType = INT_DATA_TRANSMISSION_COMPLETE; clearInt = SET_INTERRUPT(0, INT_DATA_TRANSMISSION_COMPLETE); }
    else if (IS_INTERRUPT_SET(sli, INT_DATA_RECEPTION_COMPLETE))     { *intType = INT_DATA_RECEPTION_COMPLETE;    clearInt = SET_INTERRUPT(0, INT_DATA_RECEPTION_COMPLETE); }
    else if (IS_INTERRUPT_SET(sli, INT_BIT_ERROR))                   { *intType = INT_BIT_ERROR;                  clearInt = SET_INTERRUPT(0, INT_BIT_ERROR); }
    else if (IS_INTERRUPT_SET(sli, INT_TIMEOUT_ERROR))               { *intType = INT_TIMEOUT_ERROR;              clearInt = SET_INTERRUPT(0, INT_TIMEOUT_ERROR); }
    else if (IS_INTERRUPT_SET(sli, INT_FRAME_ERROR))                 { *intType = INT_FRAME_ERROR;                clearInt = SET_INTERRUPT(0, INT_FRAME_ERROR); }
    else if (IS_INTERRUPT_SET(sli, INT_CHECKSUM_ERROR))              { *intType = INT_CHECKSUM_ERROR;             clearInt = SET_INTERRUPT(0, INT_CHECKSUM_ERROR); }
    else if (IS_INTERRUPT_SET(sli, INT_STUCK_AT_ZERO))               { *intType = INT_STUCK_AT_ZERO;              clearInt = SET_INTERRUPT(0, INT_STUCK_AT_ZERO); }
    else if (IS_INTERRUPT_SET(sli, INT_DATA_RECEPTION_BUF_NOT_EMPTY)){ *intType = INT_DATA_RECEPTION_BUF_NOT_EMPTY; }
    else                                                              { *intType = NO_SECOND_LEVEL_INT; }

    if (*intType != NO_SECOND_LEVEL_INT) {
        SJA1124_ClearSecondLevelInterrupts(device, channel,
            clearInt | SET_INTERRUPT(0, INT_DATA_RECEPTION_BUF_NOT_EMPTY));
    }

    return SJA1124_SUCCESS;
}

/*******************************************************************************
 * GetStatus (combined status check)
 ******************************************************************************/
static SJA1124_StatusType GetStatusInternal(uint8_t device, SJA1124_LinChannelType channel,
                                             const SJA1124_GetStatusType *gs,
                                             SJA1124_LinStatusType *txStatus)
{
    SJA1124_LinStateType linState;
    SJA1124_SecondLevelIntType sli = NO_SECOND_LEVEL_INT;

    SJA1124_ProcessSecondLevelInterrupt(device, channel, &sli);
    SJA1124_GetLinState(device, channel, &linState);

    switch (sli) {
        case INT_DATA_TRANSMISSION_COMPLETE: *txStatus = LIN_TX_OK; break;
        case INT_DATA_RECEPTION_COMPLETE:    *txStatus = LIN_RX_OK; break;
        case INT_FRAME_ERROR:
        case INT_CHECKSUM_ERROR:             *txStatus = LIN_RX_ERROR; break;
        case INT_STUCK_AT_ZERO:
            *txStatus = (gs->ResponseDir == RESPONSE_RECEIVE) ? LIN_RX_ERROR : LIN_TX_ERROR;
            break;
        case INT_TIMEOUT_ERROR:              *txStatus = LIN_RX_NO_RESPONSE; break;
        case INT_BIT_ERROR:
            switch (linState.LinChannelState) {
                case STATE_BREAK_TRANS_ONGOING:
                case STATE_BREAK_COMPLETE_DELIMITER_ONGOING:
                case STATE_FIELD_TRANS_ONGOING:
                case STATE_ID_TRANS_ONGOING:
                    *txStatus = LIN_TX_HEADER_ERROR; break;
                default:
                    *txStatus = LIN_TX_ERROR; break;
            }
            break;
        default:
            switch (linState.LinChannelState) {
                case STATE_BREAK_TRANS_ONGOING:
                case STATE_BREAK_COMPLETE_DELIMITER_ONGOING:
                case STATE_FIELD_TRANS_ONGOING:
                case STATE_ID_TRANS_ONGOING:
                case STATE_HEADER_TRANS_COMPLETE:
                    *txStatus = LIN_TX_BUSY; break;
                case STATE_RESPONSE_TRANS_ONGOING:
                case STATE_DATA_COMPLETE_CHECKSUM_ONGOING:
                    *txStatus = (RESPONSE_RECEIVE == gs->ResponseDir) ? LIN_RX_BUSY : LIN_TX_BUSY;
                    break;
                case STATE_SLEEP:
                    *txStatus = LIN_CH_SLEEP; break;
                case STATE_IDLE:
                    *txStatus = gs->IsChannelInSleep ? LIN_CH_SLEEP : LIN_OPERATIONAL;
                    break;
                default:
                    *txStatus = LIN_OPERATIONAL; break;
            }
            break;
    }
    return SJA1124_SUCCESS;
}

SJA1124_LinStatusType SJA1124_GetStatus(uint8_t device, SJA1124_LinChannelType channel)
{
    SJA1124_GetStatusType gs = {0};
    SJA1124_LinStatusType txStatus = LIN_NOT_OK;
    SJA1124_StatusType st = GetStatusInternal(device, channel, &gs, &txStatus);
    return (st == SJA1124_SUCCESS) ? txStatus : LIN_NOT_OK;
}

/*******************************************************************************
 * Device Init / Deinit
 ******************************************************************************/
SJA1124_StatusType SJA1124_DeviceDeinit(uint8_t device)
{
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    return SJA1124_WriteReg(h, SJA1124_MODE, SJA1124_MODE_RESET_MASK);
}

SJA1124_StatusType SJA1124_DeviceInit(uint8_t device, uint32_t freqKHz,
                                       SJA1124_DeviceConfigType *config, uint8_t powerCycle)
{
    uint8_t data[4] = {0};
    SJA1124_SpiHandle_t *h;

    g_DeviceList[device] = config;
    h = config->spiHandle;

    if (powerCycle) {
        SJA1124_DeviceDeinit(device);
    }
    HAL_Delay(100);

    /* Read status to trigger SPI wakeup */
    SJA1124_ReadReg(h, SJA1124_STATUS, &data[0]);
    HAL_Delay(100);

    /* Check INITI bit */
    SJA1124_ReadReg(h, SJA1124_INT1, &data[0]);
    if ((data[0] & SJA1124_INT1_INITSTATINT_MASK) == 0) {
        return SJA1124_ERR_INIT;
    }

    /* Set PLL multiplication factor */
    SJA1124_SetMult(device, freqKHz, &data[0]);

    /* Configure top level interrupts */
    SerializeTopLevelInterrupts(&config->TopLevelIntConfig, &data[1]);

    SJA1124_WriteReg(h, SJA1124_PLLCFG, data[0]);
    SJA1124_WriteReg(h, SJA1124_INT1EN, data[1]);
    SJA1124_WriteReg(h, SJA1124_INT2EN, data[2]);
    SJA1124_WriteReg(h, SJA1124_INT3EN, data[3]);

    /* Clear INITI bit */
    SJA1124_WriteReg(h, SJA1124_INT1, (uint8_t)(1U << SJA1124_INT1_INITSTATINT_SHIFT));

    return SJA1124_SUCCESS;
}

/*******************************************************************************
 * Channel Init
 ******************************************************************************/
static SJA1124_StatusType ConfigureLinChannel(uint8_t device, uint32_t freqKHz,
                                               uint32_t baudrate, uint8_t idx)
{
    uint8_t data[9];
    SJA1124_StatusType status;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    SJA1124_LinChannelType ch = g_DeviceList[device]->LinChannelList.LinChannels[idx].LinChannelMapping;
    SJA1124_LinChannelConfigType *cfg = &g_DeviceList[device]->LinChannelList.LinChannels[idx].LinChannelConfig;

    data[0] = (uint8_t)(((uint8_t)cfg->ChecksumCalcEn << SJA1124_LCFG1_CSCALCDIS_SHIFT) |
                         ((uint8_t)cfg->LinMasterBreakLength << SJA1124_LCFG1_MASBREAKLEN_SHIFT) |
                         SJA1124_LCFG1_INIT_MASK);
    data[1] = (uint8_t)(((uint8_t)cfg->Delimiter << SJA1124_LCFG2_TWOBITDELIM_SHIFT) |
                         ((uint8_t)cfg->IdleOnBitErrorEn << SJA1124_LCFG2_IDLEONBITERR_SHIFT));
    data[2] = (uint8_t)(cfg->IdleOnTimeoutEn << SJA1124_LITC_IDLEONTIMEOUT_SHIFT);
    data[3] = (uint8_t)(cfg->StopBitConfig << SJA1124_LGC_STOPBITCONF_SHIFT);
    data[4] = cfg->ResponseTimeout;

    SJA1124_CalBaudRateReg(device, freqKHz, baudrate, &data[5]);
    SerializeSecondLevelInterrupts(&cfg->SecondLevelIntConfig, &data[8]);

    status = SJA1124_EnterLinChannelInitMode(device, ch);
    if (status != SJA1124_SUCCESS) {
        return status;
    }

    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, ch), data[0]) != SJA1124_SUCCESS) return SJA1124_ERR_SPI;
    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG2, ch), data[1]) != SJA1124_SUCCESS) return SJA1124_ERR_SPI;
    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LITC, ch),  data[2]) != SJA1124_SUCCESS) return SJA1124_ERR_SPI;
    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LGC, ch),   data[3]) != SJA1124_SUCCESS) return SJA1124_ERR_SPI;
    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LRTC, ch),  data[4]) != SJA1124_SUCCESS) return SJA1124_ERR_SPI;
    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LFR, ch),   data[5]) != SJA1124_SUCCESS) return SJA1124_ERR_SPI;
    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LBRM, ch),  data[6]) != SJA1124_SUCCESS) return SJA1124_ERR_SPI;
    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LBRL, ch),  data[7]) != SJA1124_SUCCESS) return SJA1124_ERR_SPI;
    if (SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LIE, ch),   data[8]) != SJA1124_SUCCESS) return SJA1124_ERR_SPI;

    return SJA1124_LeaveLinChannelInitMode(device, ch);
}

SJA1124_StatusType SJA1124_ChannelInit(uint8_t device, uint32_t baudrate,
                                        SJA1124_LinChannelType channel)
{
    uint8_t data;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    SerializeLinCommander(device, &data, baudrate, channel);
    SJA1124_WriteReg(h, SJA1124_LCOM1, data);

    return ConfigureLinChannel(device, g_DeviceList[device]->Freq, baudrate, channel);
}

/*******************************************************************************
 * Frame operations
 ******************************************************************************/
SJA1124_StatusType SJA1124_SendFrame(uint8_t device, SJA1124_LinChannelType channel,
                                      SJA1124_LinFrameType *frame)
{
    uint8_t bytesCount = 0;
    uint8_t data[12] = {0};
    uint8_t offset;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    data[0] = SJA1124_LC_HEADERTRANSREQ_MASK;
    SerializeLinFrame(frame, data + 1, &bytesCount);

    offset = SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_SF_LBI, channel);
    for (int i = 0; i < bytesCount; i++) {
        SJA1124_WriteReg(h, offset, data[i + 1]);
        offset++;
    }
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_SF_LC, channel), data[0]);

    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_SendHeader(uint8_t device, SJA1124_LinChannelType channel,
                                       SJA1124_LinFrameType *frame)
{
    uint8_t data[3];
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    data[0] = frame->Pid;
    data[1] = (uint8_t)(((uint8_t)frame->ChecksumType << SJA1124_LBC_CLASSICCS_SHIFT) |
                         ((uint8_t)frame->ResponseDir << SJA1124_LBC_DIRECTION_SHIFT) |
                         ((uint8_t)(frame->DataFieldLength - 1U) << SJA1124_LBC_DTFIELDLEN_SHIFT));
    data[2] = SJA1124_LC_HEADERTRANSREQ_MASK;

    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_SF_LBI, channel), data[0]);
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_SF_LBC, channel), data[1]);
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_SF_LC, channel),  data[2]);

    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_GetData(uint8_t device, SJA1124_LinChannelType channel,
                                    SJA1124_LinFrameType *frame,
                                    SJA1124_SecondLevelIntType *intType)
{
    uint8_t getState;
    uint8_t data[9] = {0};
    uint8_t offset;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    SJA1124_StatusType status = SJA1124_SUCCESS;
    SJA1124_SecondLevelInterruptsType sli;
    int retries = MAX_RETRIES;

    /* Wait for reception complete */
    getState = SJA1124_GetChannelStatus(device, channel);
    while (retries-- && !(getState & SJA1124_LS_RCVCOMPFLG_MASK)) {
        getState = SJA1124_GetChannelStatus(device, channel);
    }

    /* Read data bytes */
    offset = SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LBD1, channel);
    for (int i = 0; i < frame->DataFieldLength; i++) {
        SJA1124_ReadReg(h, offset, &data[i]);
        frame->Data[i] = data[i];
        offset++;
    }

    /* Read checksum */
    offset = SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LCF, channel);
    SJA1124_ReadReg(h, offset, &data[0]);
    frame->Checksum = data[0];

    /* Check for errors */
    SJA1124_GetSecondLevelInterrupts(device, channel, &sli);

    if      (sli & SJA1124_LIE_STUCKZEROIEN_MASK) { *intType = INT_STUCK_AT_ZERO;   status = SJA1124_FAIL; }
    else if (sli & SJA1124_LIE_TIMEOUTIEN_MASK)   { *intType = INT_TIMEOUT_ERROR;   status = SJA1124_FAIL; }
    else if (sli & SJA1124_LIE_BITERRIEN_MASK)    { *intType = INT_BIT_ERROR;       status = SJA1124_FAIL; }
    else if (sli & SJA1124_LIE_CSERRIEN_MASK)     { *intType = INT_CHECKSUM_ERROR;  status = SJA1124_FAIL; }
    else if (sli & SJA1124_LIE_FRAMEERRIEN_MASK)  { *intType = INT_FRAME_ERROR;     status = SJA1124_FAIL; }
    else if ((sli >> 8) & SJA1124_LIE_DTRCVIEN_MASK) { *intType = INT_DATA_RECEPTION_COMPLETE; }
    else { *intType = NO_SECOND_LEVEL_INT; }

    SJA1124_ClearSecondLevelInterrupts(device, channel, sli);
    return status;
}

SJA1124_StatusType SJA1124_SendWakeupRequest(uint8_t device, SJA1124_LinChannelType channel)
{
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_SF_LBD1, channel), WAKEUP_CHARACTER);
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_SF_LC, channel), SJA1124_LC_WAKEUPREQ_MASK);
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_SendAbortRequest(uint8_t device, SJA1124_LinChannelType channel)
{
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    return SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_SF_LC, channel), SJA1124_LC_ABORTREQ_MASK);
}

/*******************************************************************************
 * Configuration functions
 ******************************************************************************/
SJA1124_StatusType SJA1124_SetMBL(uint8_t device, SJA1124_LinChannelType channel,
                                   SJA1124_MasterBreakLenType mbl)
{
    uint8_t data;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    g_DeviceList[device]->LinChannelList.LinChannels[channel].LinChannelConfig.LinMasterBreakLength = mbl;
    SJA1124_EnterLinChannelInitMode(device, channel);
    SJA1124_ReadReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), &data);
    data = (data & ~SJA1124_LCFG1_MASBREAKLEN_MASK) | SJA1124_LCFG1_INIT_MASK;
    data |= (uint8_t)(mbl << SJA1124_LCFG1_MASBREAKLEN_SHIFT);
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), data);
    SJA1124_LeaveLinChannelInitMode(device, channel);
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_SetTBDE(uint8_t device, SJA1124_LinChannelType channel,
                                    SJA1124_DelimiterType tbde)
{
    uint8_t data;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    g_DeviceList[device]->LinChannelList.LinChannels[channel].LinChannelConfig.Delimiter = tbde;
    SJA1124_EnterLinChannelInitMode(device, channel);
    SJA1124_ReadReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG2, channel), &data);
    data &= ~SJA1124_LCFG2_TWOBITDELIM_MASK;
    data |= (uint8_t)(tbde << SJA1124_LCFG2_TWOBITDELIM_SHIFT);
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG2, channel), data);
    SJA1124_LeaveLinChannelInitMode(device, channel);
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_SetStopBits(uint8_t device, SJA1124_LinChannelType channel,
                                        SJA1124_StopBitConfigType stopBits)
{
    uint8_t data;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    g_DeviceList[device]->LinChannelList.LinChannels[channel].LinChannelConfig.StopBitConfig = stopBits;
    SJA1124_EnterLinChannelInitMode(device, channel);
    SJA1124_ReadReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LGC, channel), &data);
    data &= ~SJA1124_LGC_STOPBITCONF_MASK;
    data |= (uint8_t)(stopBits << SJA1124_LGC_STOPBITCONF_SHIFT);
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LGC, channel), data);
    SJA1124_LeaveLinChannelInitMode(device, channel);
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_EnableHwSwChecksum(uint8_t device, SJA1124_LinChannelType channel,
                                               SJA1124_ChecksumCalcType calc)
{
    uint8_t data;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    g_DeviceList[device]->LinChannelList.LinChannels[channel].LinChannelConfig.ChecksumCalcEn = calc;
    SJA1124_EnterLinChannelInitMode(device, channel);
    SJA1124_ReadReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), &data);
    data = (data & ~SJA1124_LCFG1_CSCALSW_MASK) | SJA1124_LCFG1_INIT_MASK;
    data |= (uint8_t)(calc << SJA1124_LCFG1_CSCALCDIS_SHIFT);
    SJA1124_WriteReg(h, SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, channel), data);
    SJA1124_LeaveLinChannelInitMode(device, channel);
    return SJA1124_SUCCESS;
}

/*******************************************************************************
 * Power management
 ******************************************************************************/
SJA1124_StatusType SJA1124_EnterLowPowerMode(uint8_t device)
{
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;
    uint8_t channelsNo = g_DeviceList[device]->LinChannelList.ConfiguredChannelsNumber;
    SJA1124_TopLevelInterruptsType tli;
    SJA1124_SecondLevelInterruptsType sli;

    for (int i = 0; i < channelsNo; i++)
        SJA1124_SendAbortRequest(device, i);

    SJA1124_GetTopLevelInterrupts(device, &tli);
    SJA1124_ClearTopLevelInterrupts(device, tli);

    for (int i = 0; i < channelsNo; i++) {
        SJA1124_GetSecondLevelInterrupts(device, i, &sli);
        SJA1124_ClearSecondLevelInterrupts(device, i, sli);
        SJA1124_SetLinChannelMode(h, i, MODE_SLEEP);
    }

    SJA1124_WriteReg(h, SJA1124_MODE, SJA1124_MODE_LOWPWRMODE_MASK);
    return SJA1124_SUCCESS;
}

SJA1124_StatusType SJA1124_ExitLowPowerMode(uint8_t device)
{
    uint8_t data;
    SJA1124_SpiHandle_t *h = g_DeviceList[device]->spiHandle;

    SJA1124_ReadReg(h, SJA1124_STATUS, &data);
    HAL_Delay(100);
    SJA1124_WriteReg(h, SJA1124_INT1, (uint8_t)(1U << SJA1124_INT1_INITSTATINT_SHIFT));
    return SJA1124_SUCCESS;
}

/*******************************************************************************
 * Utility functions
 ******************************************************************************/
uint8_t SJA1124_CalculateChecksum(SJA1124_ChecksumType type, uint8_t pid,
                                   uint8_t *data, uint8_t length)
{
    uint16_t checkSum = 0;

    if (CHECKSUM_ENHANCED == type) {
        checkSum = pid;
    }

    for (uint8_t i = 0; i < length; i++) {
        checkSum += data[i];
        if (checkSum > 0xFF) checkSum -= 0xFF;
    }

    return (~checkSum) & 0xFF;
}

SJA1124_StatusType SJA1124_GetDeviceId(uint8_t device, uint8_t *id)
{
    return SJA1124_DRV_ReadRegister(device, SJA1124_OR_ID, id);
}
