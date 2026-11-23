/*
 * Dio.c —— AUTOSAR CP Dio Driver（HPM5E31 / RV32，寄存器级）
 *
 * 参考：AUTOSAR_CP_SWS_DIODriver R25-11 §7.6.1 开发错误 / §8.3 函数
 *       HPM5E31 RM：GPIO DI/DO/OE 寄存器（DO 有 SET/CLEAR/TOGGLE 单比特写口）
 * 例程：samples/mcal/dio_led_button
 *
 * 注意：Dio 没有 Init API（AUTOSAR 规定方向/模式由 Port 初始化），
 *       通道映射是编译期固定的（VARIANT-PRE-COMPILE 语义）。
 */
#include "Dio.h"
#include "Dio_PBcfg.h"
#include "Det.h"
#include "hpm_soc.h"

#if (DIO_DEV_ERROR_DETECT == STD_ON)
#define DIO_DET(api, err)  MCAL_DET_REPORT(DIO_MODULE_ID, 0u, (api), (err))
#else
#define DIO_DET(api, err)  ((void)0)
#endif

/* ---------------------------------------------------------------- 内部工具 */

static GPIO_Type *dio_gpio(void)
{
    return (GPIO_Type *)Dio_Config.gpioBase;
}

static const Dio_ChannelConfigType *dio_channel(Dio_ChannelType ch)
{
    return &Dio_Config.channels[ch];
}

static boolean dio_channel_valid(Dio_ChannelType ch)
{
    return ((uint32)ch < (uint32)Dio_Config.channelCount) ? TRUE : FALSE;
}

static boolean dio_port_valid(Dio_PortType port)
{
    return ((uint32)port < (uint32)DIO_MAX_PORTS) ? TRUE : FALSE;
}

/* 通道组必须是配置里声明过的（SWS_Dio_00184：由配置工具给出），否则报 DIO_E_PARAM_INVALID_GROUP */
static boolean dio_group_known(const Dio_ChannelGroupType *group)
{
    uint16 i;
    boolean known = FALSE;

    for (i = 0u; i < Dio_Config.groupCount; i++)
    {
        const Dio_ChannelGroupType *g = &Dio_Config.groups[i];
        if ((g->port == group->port) && (g->offset == group->offset) && (g->mask == group->mask))
        {
            known = TRUE;
            break;
        }
    }
    return known;
}

/* ---------------------------------------------------------------- API 实现 */

Dio_LevelType Dio_ReadChannel(Dio_ChannelType ChannelId)
{
    const Dio_ChannelConfigType *ch;
    uint32 level;

    if (!dio_channel_valid(ChannelId))
    {
        DIO_DET(DIO_API_ID_READ_CHANNEL, DIO_E_PARAM_INVALID_CHANNEL_ID);
        return (Dio_LevelType)STD_LOW;
    }
    ch = dio_channel(ChannelId);
    /* SWS_Dio_00083：直接读引脚电平（输出通道也返回真实引脚电平） */
    level = dio_gpio()->DI[ch->gpioPort].VALUE & ((uint32)1u << ch->gpioPin);
    return (level != 0u) ? (Dio_LevelType)STD_HIGH : (Dio_LevelType)STD_LOW;
}

void Dio_WriteChannel(Dio_ChannelType ChannelId, Dio_LevelType Level)
{
    const Dio_ChannelConfigType *ch;
    uint32 mask;

    if (!dio_channel_valid(ChannelId))
    {
        DIO_DET(DIO_API_ID_WRITE_CHANNEL, DIO_E_PARAM_INVALID_CHANNEL_ID);
        return;
    }
    ch = dio_channel(ChannelId);
    mask = (uint32)1u << ch->gpioPin;
    if (Level != STD_LOW)
    {
        dio_gpio()->DO[ch->gpioPort].SET = mask;
    }
    else
    {
        dio_gpio()->DO[ch->gpioPort].CLEAR = mask;
    }
}

Dio_PortLevelType Dio_ReadPort(Dio_PortType PortId)
{
    if (!dio_port_valid(PortId))
    {
        DIO_DET(DIO_API_ID_READ_PORT, DIO_E_PARAM_INVALID_PORT_ID);
        return 0u;
    }
    return (Dio_PortLevelType)dio_gpio()->DI[PortId].VALUE;
}

void Dio_WritePort(Dio_PortType PortId, Dio_PortLevelType Level)
{
    if (!dio_port_valid(PortId))
    {
        DIO_DET(DIO_API_ID_WRITE_PORT, DIO_E_PARAM_INVALID_PORT_ID);
        return;
    }
    dio_gpio()->DO[PortId].VALUE = (uint32)Level;
}

Dio_PortLevelType Dio_ReadChannelGroup(const Dio_ChannelGroupType *ChannelGroupIdPtr)
{
    uint32 value;

    if (ChannelGroupIdPtr == NULL_PTR)
    {
        DIO_DET(DIO_API_ID_READ_CHANNEL_GROUP, DIO_E_PARAM_POINTER);
        return 0u;
    }
    if ((!dio_port_valid(ChannelGroupIdPtr->port)) || (!dio_group_known(ChannelGroupIdPtr)) ||
        (ChannelGroupIdPtr->mask == 0u))
    {
        DIO_DET(DIO_API_ID_READ_CHANNEL_GROUP, DIO_E_PARAM_INVALID_GROUP);
        return 0u;
    }
    value = dio_gpio()->DI[ChannelGroupIdPtr->port].VALUE;
    value >>= ChannelGroupIdPtr->offset;
    return (Dio_PortLevelType)(value & ChannelGroupIdPtr->mask);
}

void Dio_WriteChannelGroup(const Dio_ChannelGroupType *ChannelGroupIdPtr, Dio_PortLevelType Level)
{
    uint32 groupMask;
    uint32 value;

    if (ChannelGroupIdPtr == NULL_PTR)
    {
        DIO_DET(DIO_API_ID_WRITE_CHANNEL_GROUP, DIO_E_PARAM_POINTER);
        return;
    }
    if ((!dio_port_valid(ChannelGroupIdPtr->port)) || (!dio_group_known(ChannelGroupIdPtr)) ||
        (ChannelGroupIdPtr->mask == 0u))
    {
        DIO_DET(DIO_API_ID_WRITE_CHANNEL_GROUP, DIO_E_PARAM_INVALID_GROUP);
        return;
    }
    groupMask = (ChannelGroupIdPtr->mask) << ChannelGroupIdPtr->offset;
    value = dio_gpio()->DO[ChannelGroupIdPtr->port].VALUE;
    value &= ~groupMask;
    value |= (((uint32)Level & ChannelGroupIdPtr->mask) << ChannelGroupIdPtr->offset);
    dio_gpio()->DO[ChannelGroupIdPtr->port].VALUE = value;
}

void Dio_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
    if (versioninfo == NULL_PTR)
    {
        DIO_DET(DIO_API_ID_GET_VERSION_INFO, DIO_E_PARAM_POINTER);
        return;
    }
    versioninfo->vendorID = DIO_VENDOR_ID;
    versioninfo->moduleID = DIO_MODULE_ID;
    versioninfo->sw_major_version = DIO_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = DIO_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = DIO_SW_PATCH_VERSION;
}

Dio_LevelType Dio_FlipChannel(Dio_ChannelType ChannelId)
{
    const Dio_ChannelConfigType *ch;

    if (!dio_channel_valid(ChannelId))
    {
        DIO_DET(DIO_API_ID_FLIP_CHANNEL, DIO_E_PARAM_INVALID_CHANNEL_ID);
        return (Dio_LevelType)STD_LOW;
    }
    ch = dio_channel(ChannelId);
    dio_gpio()->DO[ch->gpioPort].TOGGLE = (uint32)1u << ch->gpioPin;
    return Dio_ReadChannel(ChannelId);
}

void Dio_MaskedWritePort(Dio_PortType PortId, Dio_PortLevelType Level, Dio_PortLevelType Mask)
{
    uint32 value;

    if (!dio_port_valid(PortId))
    {
        DIO_DET(DIO_API_ID_MASKED_WRITE_PORT, DIO_E_PARAM_INVALID_PORT_ID);
        return;
    }
    value = dio_gpio()->DO[PortId].VALUE;
    value &= ~(uint32)Mask;
    value |= ((uint32)Level & (uint32)Mask);
    dio_gpio()->DO[PortId].VALUE = value;
}

/* ---------------------------------------------------------------- 扩展 API */

uint16 Dio_GetChannelCount(void)
{
    return Dio_Config.channelCount;
}

uint16 Dio_GetGroupCount(void)
{
    return Dio_Config.groupCount;
}

const Dio_ChannelGroupType *Dio_GetChannelGroup(uint16 index)
{
    if ((uint32)index >= (uint32)Dio_Config.groupCount)
    {
        return NULL_PTR;
    }
    return &Dio_Config.groups[index];
}
