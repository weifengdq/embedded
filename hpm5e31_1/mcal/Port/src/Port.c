/*
 * Port.c —— AUTOSAR CP Port Driver（HPM5E31 / RV32，寄存器级）
 *
 * 参考：AUTOSAR_CP_SWS_PortDriver R25-11 §7.2/§8.3
 *       HPM5E31 RM：IOC PADx.FUNC_CTL（ALT_SELECT/ANALOG）、PAD_CTL（PE/PS/PRS/OD/SR/SPD/DS/HYS）、
 *                   GPIO OE/DO/DI
 * 例程：samples/mcal/port_pin_config
 */
#include "Port.h"
#include "Port_PBcfg.h"
#include "Det.h"
#include "hpm_soc.h"

#if (PORT_DEV_ERROR_DETECT == STD_ON)
#define PORT_DET(api, err)  MCAL_DET_REPORT(PORT_MODULE_ID, 0u, (api), (err))
#else
#define PORT_DET(api, err)  ((void)0)
#endif

static const Port_ConfigType *s_cfg = NULL_PTR;
static boolean s_initialized = FALSE;

/* ---------------------------------------------------------------- 内部工具 */

static GPIO_Type *port_gpio(const Port_PinConfigType *pin)
{
    return (GPIO_Type *)pin->gpioBase;
}

/* 配完 PAD_CTL：上下拉/开漏/驱动强度/摆率/施密特 */
static void port_write_pad_ctl(const Port_PinConfigType *pin)
{
    IOC_Type *ioc = (IOC_Type *)pin->iocBase;
    uint32 ctl = 0u;
    uint32 pe = 0u;
    uint32 ps = 0u;

    if (pin->pull == PORT_PIN_PULL_UP)
    {
        pe = 1u;
        ps = 1u;
    }
    else if (pin->pull == PORT_PIN_PULL_DOWN)
    {
        pe = 1u;
        ps = 0u;
    }
    else
    {
        /* 无上下拉 */
    }

    ctl |= IOC_PAD_PAD_CTL_PE_SET(pe);
    ctl |= IOC_PAD_PAD_CTL_PS_SET(ps);
    if (pe != 0u)
    {
        ctl |= IOC_PAD_PAD_CTL_PRS_SET(1u);   /* 47 kOhm 档 */
    }
    if (pin->openDrain != 0u)
    {
        ctl |= IOC_PAD_PAD_CTL_OD_SET(1u);
    }
    ctl |= IOC_PAD_PAD_CTL_DS_SET(pin->driveStrength);
    ctl |= IOC_PAD_PAD_CTL_SR_SET(pin->slewRate);
    ctl |= IOC_PAD_PAD_CTL_SPD_SET(pin->speed);
    ctl |= IOC_PAD_PAD_CTL_HYS_SET(pin->hysteresis);

    ioc->PAD[pin->padIndex].PAD_CTL = ctl;
}

/* 配 FUNC_CTL：GPIO(ALT0) / ALTn / ANALOG */
static void port_write_func_ctl(const Port_PinConfigType *pin, Port_PinModeType mode)
{
    IOC_Type *ioc = (IOC_Type *)pin->iocBase;
    uint32 func;

    if (mode == PORT_PIN_MODE_ANALOG)
    {
        func = IOC_PAD_FUNC_CTL_ANALOG_MASK;
    }
    else
    {
        func = IOC_PAD_FUNC_CTL_ALT_SELECT_SET((uint32)mode);
        if (mode == PORT_PIN_MODE_GPIO)
        {
            /*
             * HPM 的 GPIO 输出默认不把 pad 电平送回输入总线，DI 读回恒 0。
             * RM IOC PADx.FUNC_CTL.LOOP_BACK=1 "force input on" 才能让 DI 反映引脚真实电平，
             * 满足 SWS_Dio_00083（读函数对输出通道返回真实引脚电平）。
             */
            func |= IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
        }
    }
    ioc->PAD[pin->padIndex].FUNC_CTL = func;
}

/* 配方向：先写输出锁存（SWS_Port_00055），再改 OE，保证切换瞬间无毛刺 */
static void port_write_direction(const Port_PinConfigType *pin, Port_PinDirectionType dir)
{
    GPIO_Type *gpio = port_gpio(pin);
    uint32 mask = (uint32)1u << pin->gpioPin;

    if (dir == PORT_PIN_OUT)
    {
        if (pin->initialLevel != 0u)
        {
            gpio->DO[pin->gpioPort].SET = mask;
        }
        else
        {
            gpio->DO[pin->gpioPort].CLEAR = mask;
        }
        gpio->OE[pin->gpioPort].SET = mask;
    }
    else
    {
        gpio->OE[pin->gpioPort].CLEAR = mask;
    }
}

static boolean port_pin_is_valid(Port_PinType pin)
{
    return ((s_cfg != NULL_PTR) && ((uint32)pin < (uint32)s_cfg->pinCount)) ? TRUE : FALSE;
}

/* ---------------------------------------------------------------- API 实现 */

void Port_Init(const Port_ConfigType *ConfigPtr)
{
    uint16 i;

    if ((ConfigPtr == NULL_PTR) || (ConfigPtr->pins == NULL_PTR) || (ConfigPtr->pinCount == 0u))
    {
        /* SWS_Port_00041 + 表 7.1：Port_Init 参数错报 PORT_E_INIT_FAILED */
        PORT_DET(PORT_API_ID_INIT, PORT_E_INIT_FAILED);
        return;
    }

    s_cfg = ConfigPtr;
    s_initialized = FALSE;

    for (i = 0u; i < ConfigPtr->pinCount; i++)
    {
        const Port_PinConfigType *pin = &ConfigPtr->pins[i];
        /* 顺序：PAD_CTL -> FUNC_CTL -> 输出锁存 -> OE（避免毛刺） */
        port_write_pad_ctl(pin);
        port_write_func_ctl(pin, (Port_PinModeType)pin->mode);
        port_write_direction(pin, (Port_PinDirectionType)pin->direction);
    }

    s_initialized = TRUE;
}

void Port_SetPinDirection(Port_PinType Pin, Port_PinDirectionType Direction)
{
    const Port_PinConfigType *pin;

    if (!s_initialized)
    {
        PORT_DET(PORT_API_ID_SET_PIN_DIRECTION, PORT_E_UNINIT);
        return;
    }
    if (!port_pin_is_valid(Pin))
    {
        PORT_DET(PORT_API_ID_SET_PIN_DIRECTION, PORT_E_PARAM_PIN);
        return;
    }
    if ((Direction != PORT_PIN_IN) && (Direction != PORT_PIN_OUT))
    {
        PORT_DET(PORT_API_ID_SET_PIN_DIRECTION, PORT_E_PARAM_INVALID_DIRECTION);
        return;
    }
    pin = &s_cfg->pins[Pin];
    if (pin->directionChangeable == 0u)
    {
        PORT_DET(PORT_API_ID_SET_PIN_DIRECTION, PORT_E_DIRECTION_UNCHANGEABLE);
        return;
    }
    port_write_direction(pin, Direction);
}

void Port_RefreshPortDirection(void)
{
    uint16 i;

    if (!s_initialized)
    {
        PORT_DET(PORT_API_ID_REFRESH_PORT_DIRECTION, PORT_E_UNINIT);
        return;
    }
    for (i = 0u; i < s_cfg->pinCount; i++)
    {
        const Port_PinConfigType *pin = &s_cfg->pins[i];
        /* SWS_Port_00061：运行时可变方向的引脚不刷新 */
        if (pin->directionChangeable == 0u)
        {
            port_write_direction(pin, (Port_PinDirectionType)pin->direction);
        }
    }
}

void Port_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
    if (versioninfo == NULL_PTR)
    {
        PORT_DET(PORT_API_ID_GET_VERSION_INFO, PORT_E_PARAM_POINTER);
        return;
    }
    versioninfo->vendorID = PORT_VENDOR_ID;
    versioninfo->moduleID = PORT_MODULE_ID;
    versioninfo->sw_major_version = PORT_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = PORT_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = PORT_SW_PATCH_VERSION;
}

void Port_SetPinMode(Port_PinType Pin, Port_PinModeType Mode)
{
    const Port_PinConfigType *pin;

    if (!s_initialized)
    {
        PORT_DET(PORT_API_ID_SET_PIN_MODE, PORT_E_UNINIT);
        return;
    }
    if (!port_pin_is_valid(Pin))
    {
        PORT_DET(PORT_API_ID_SET_PIN_MODE, PORT_E_PARAM_PIN);
        return;
    }
    if ((Mode != PORT_PIN_MODE_GPIO) && (Mode != PORT_PIN_MODE_ALT1) && (Mode != PORT_PIN_MODE_ALT2) &&
        (Mode != PORT_PIN_MODE_ALT3) && (Mode != PORT_PIN_MODE_ALT4) && (Mode != PORT_PIN_MODE_ANALOG))
    {
        PORT_DET(PORT_API_ID_SET_PIN_MODE, PORT_E_PARAM_INVALID_MODE);
        return;
    }
    pin = &s_cfg->pins[Pin];
    if (pin->modeChangeable == 0u)
    {
        PORT_DET(PORT_API_ID_SET_PIN_MODE, PORT_E_MODE_UNCHANGEABLE);
        return;
    }
    port_write_func_ctl(pin, Mode);
}

/* ---------------------------------------------------------------- 扩展 API */

uint16 Port_GetPinCount(void)
{
    return (s_cfg != NULL_PTR) ? s_cfg->pinCount : 0u;
}

const Port_PinConfigType *Port_GetPinConfig(Port_PinType Pin)
{
    if ((s_cfg == NULL_PTR) || ((uint32)Pin >= (uint32)s_cfg->pinCount))
    {
        return NULL_PTR;
    }
    return &s_cfg->pins[Pin];
}

boolean Port_IsInitialized(void)
{
    return s_initialized;
}

void Port_SetPinPull(Port_PinType Pin, uint8 Pull)
{
    const Port_PinConfigType *pin;
    Port_PinConfigType tmp;

    if (!s_initialized)
    {
        PORT_DET(PORT_API_ID_SET_PIN_MODE, PORT_E_UNINIT);
        return;
    }
    if (!port_pin_is_valid(Pin))
    {
        PORT_DET(PORT_API_ID_SET_PIN_MODE, PORT_E_PARAM_PIN);
        return;
    }
    if ((Pull != PORT_PIN_PULL_NONE) && (Pull != PORT_PIN_PULL_UP) && (Pull != PORT_PIN_PULL_DOWN))
    {
        PORT_DET(PORT_API_ID_SET_PIN_MODE, PORT_E_PARAM_INVALID_MODE);
        return;
    }
    pin = &s_cfg->pins[Pin];
    tmp = *pin;
    tmp.pull = Pull;
    port_write_pad_ctl(&tmp);
}
