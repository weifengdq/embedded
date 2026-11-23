/*
 * Mcal_Version.h —— MCAL 版本号（Sw 版本 1.0.0，对应 AUTOSAR CP R21-11/R25-11 子集）
 */
#ifndef MCAL_VERSION_H
#define MCAL_VERSION_H

/* AUTOSAR 未分配 vendorID 时用 0xFFFF；正式发布需向 AUTOSAR 申请。 */
#define MCAL_VENDOR_ID                      ((uint16)0xFFFFu)

#define MCAL_AR_RELEASE_MAJOR_VERSION       (4u)
#define MCAL_AR_RELEASE_MINOR_VERSION       (4u)
#define MCAL_AR_RELEASE_REVISION_VERSION    (0u)

#define MCAL_SW_MAJOR_VERSION               (1u)
#define MCAL_SW_MINOR_VERSION               (0u)
#define MCAL_SW_PATCH_VERSION               (0u)

#define MCAL_VERSION_STRING                 "HPM5E31 MCAL 1.0.0 (AUTOSAR CP R21-11 subset)"

#endif /* MCAL_VERSION_H */
