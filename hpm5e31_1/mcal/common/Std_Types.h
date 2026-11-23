/*
 * Std_Types.h —— AUTOSAR 标准类型
 *
 * 依据：AUTOSAR_CP_SWS_StandardTypes（R21-11 起内容稳定）。
 * 该文件必须位于所有 MCAL 头文件的最上层依赖中。
 */
#ifndef STD_TYPES_H
#define STD_TYPES_H

#include "Platform_Types.h"
#include "Compiler.h"

#define STD_TYPES_AR_RELEASE_MAJOR_VERSION    (4u)
#define STD_TYPES_AR_RELEASE_MINOR_VERSION    (4u)
#define STD_TYPES_AR_RELEASE_REVISION_VERSION (0u)
#define STD_TYPES_SW_MAJOR_VERSION            (1u)
#define STD_TYPES_SW_MINOR_VERSION            (0u)
#define STD_TYPES_SW_PATCH_VERSION            (0u)

/* ---- 返回值 ---- */
#ifndef STATUSTYPEDEFINED
#define STATUSTYPEDEFINED
#define STATUS_OK 0x00u
typedef unsigned char StatusType;   /* OSEK 兼容 */
#endif

typedef uint8 Std_ReturnType;
#define E_OK       ((Std_ReturnType)0x00u)
#define E_NOT_OK   ((Std_ReturnType)0x01u)

/* ---- 电平/开关 ---- */
#define STD_HIGH      0x01u
#define STD_LOW       0x00u
#define STD_ACTIVE    0x01u
#define STD_IDLE      0x00u
#define STD_ON        0x01u
#define STD_OFF       0x00u

/* ---- 布尔 ---- */
#ifndef TRUE
#define TRUE  1u
#endif
#ifndef FALSE
#define FALSE 0u
#endif

#ifndef NULL_PTR
#define NULL_PTR ((void *)0)
#endif

/* ---- 版本信息（所有 MCAL 模块的 GetVersionInfo 共用） ---- */
typedef struct
{
    uint16 vendorID;
    uint16 moduleID;
    uint8  sw_major_version;
    uint8  sw_minor_version;
    uint8  sw_patch_version;
} Std_VersionInfoType;

#endif /* STD_TYPES_H */
