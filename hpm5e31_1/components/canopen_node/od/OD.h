/*******************************************************************************
    CANopen Object Dictionary definition for CANopenNode V4

    本文件由 components/canopen_node/tools/gen_od.py 生成，请勿手工修改。
    Single source of truth: tools/gen_od.py 中的 OBJECTS 表。
    Upstream format: CANopenEditor v4 (https://github.com/CANopenNode/CANopenEditor)

    Device: HPM5E31 CANopen node (CiA 301 communication profile)
    Vendor-ID 0x0000A531  Product 0x0000E531  Revision 0x00010000
*******************************************************************************/

#ifndef OD_H
#define OD_H

/*******************************************************************************
    Counters of OD objects
*******************************************************************************/
#define OD_CNT_NMT 1
#define OD_CNT_EM 1
#define OD_CNT_EM_PROD 1
#define OD_CNT_SYNC 1
#define OD_CNT_SYNC_PROD 1
#define OD_CNT_TIME 1
#define OD_CNT_HB_CONS 1
#define OD_CNT_HB_PROD 1
#define OD_CNT_SDO_SRV 1
#define OD_CNT_SDO_CLI 0
#define OD_CNT_RPDO 2
#define OD_CNT_TPDO 2
#define OD_CNT_LEDS 1
#define OD_CNT_STORAGE 0

#define OD_CNT_ARR_1003 16
#define OD_CNT_ARR_1016 8

/*******************************************************************************
    Sizes of OD arrays
*******************************************************************************/
/*******************************************************************************
    OD data declaration of all groups
*******************************************************************************/
typedef struct {
    uint32_t x1000_deviceType;
    uint32_t x1005_COB_ID_SYNCMessage;
    uint32_t x1006_communicationCyclePeriod;
    uint32_t x1007_synchronousWindowLength;
    uint8_t x1008_manufacturerDeviceName[32];
    uint8_t x1009_manufacturerHardwareVersion[16];
    uint8_t x100A_manufacturerSoftwareVersion[16];
    uint32_t x1012_COB_IDTimeStampObject;
    uint32_t x1014_COB_ID_EMCY;
    uint16_t x1015_inhibitTimeEMCY;
    uint8_t x1016_consumerHeartbeatTime_sub0;
    uint32_t x1016_consumerHeartbeatTime[8];
    uint16_t x1017_producerHeartbeatTime;
    struct {
        uint8_t highestSub_indexSupported;
        uint32_t vendor_ID;
        uint32_t productCode;
        uint32_t revisionNumber;
        uint32_t serialNumber;
    } x1018_identity;
    uint8_t x1019_synchronousCounterOverflowValue;
    struct {
        uint8_t highestSub_indexSupported;
        uint32_t COB_IDUsedByRPDO;
        uint8_t transmissionType;
        uint16_t eventTimer;
    } x1400_RPDOCommunicationParameter;
    struct {
        uint8_t highestSub_indexSupported;
        uint32_t COB_IDUsedByRPDO;
        uint8_t transmissionType;
        uint16_t eventTimer;
    } x1401_RPDOCommunicationParameter;
    struct {
        uint8_t numberOfMappedApplicationObjectsInPDO;
        uint32_t applicationObject1;
        uint32_t applicationObject2;
        uint32_t applicationObject3;
        uint32_t applicationObject4;
        uint32_t applicationObject5;
        uint32_t applicationObject6;
        uint32_t applicationObject7;
        uint32_t applicationObject8;
    } x1600_RPDOMappingParameter;
    struct {
        uint8_t numberOfMappedApplicationObjectsInPDO;
        uint32_t applicationObject1;
        uint32_t applicationObject2;
        uint32_t applicationObject3;
        uint32_t applicationObject4;
        uint32_t applicationObject5;
        uint32_t applicationObject6;
        uint32_t applicationObject7;
        uint32_t applicationObject8;
    } x1601_RPDOMappingParameter;
    struct {
        uint8_t highestSub_indexSupported;
        uint32_t COB_IDUsedByTPDO;
        uint8_t transmissionType;
        uint16_t inhibitTime;
        uint16_t eventTimer;
        uint8_t SYNCStartValue;
    } x1800_TPDOCommunicationParameter;
    struct {
        uint8_t highestSub_indexSupported;
        uint32_t COB_IDUsedByTPDO;
        uint8_t transmissionType;
        uint16_t inhibitTime;
        uint16_t eventTimer;
        uint8_t SYNCStartValue;
    } x1801_TPDOCommunicationParameter;
    struct {
        uint8_t numberOfMappedApplicationObjectsInPDO;
        uint32_t applicationObject1;
        uint32_t applicationObject2;
        uint32_t applicationObject3;
        uint32_t applicationObject4;
        uint32_t applicationObject5;
        uint32_t applicationObject6;
        uint32_t applicationObject7;
        uint32_t applicationObject8;
    } x1A00_TPDOMappingParameter;
    struct {
        uint8_t numberOfMappedApplicationObjectsInPDO;
        uint32_t applicationObject1;
        uint32_t applicationObject2;
        uint32_t applicationObject3;
        uint32_t applicationObject4;
        uint32_t applicationObject5;
        uint32_t applicationObject6;
        uint32_t applicationObject7;
        uint32_t applicationObject8;
    } x1A01_TPDOMappingParameter;
} OD_PERSIST_COMM_t;

typedef struct {
    uint8_t x1001_errorRegister;
    uint8_t x1003_pre_definedErrorField_sub0;
    uint32_t x1003_pre_definedErrorField[16];
    struct {
        uint8_t highestSub_indexSupported;
        uint32_t COB_IDClientToServerRx;
        uint32_t COB_IDServerToClientTx;
    } x1200_SDOServerParameter;
    uint8_t x2000_vendorArray1k_sub0;
    uint64_t x2000_vendorArray1k[128];
    uint8_t x2001_vendorDomain1k[1024];
    uint8_t x2002_vendorDomain4k[4096];
    uint16_t x2100_ledState;
    uint16_t x2101_adcSample;
    uint32_t x2102_pdoCounter;
    uint32_t x2103_loopCounter;
    uint16_t x2104_syncCounter;
    uint16_t x2105_lastSdoIndex;
    uint32_t x2106_emcyTrigger;
    uint32_t x2107_emcyCount;
} OD_RAM_t;

#ifndef OD_ATTR_PERSIST_COMM
#define OD_ATTR_PERSIST_COMM
#endif
extern OD_ATTR_PERSIST_COMM OD_PERSIST_COMM_t OD_PERSIST_COMM;

#ifndef OD_ATTR_RAM
#define OD_ATTR_RAM
#endif
extern OD_ATTR_RAM OD_RAM_t OD_RAM;

#ifndef OD_ATTR_OD
#define OD_ATTR_OD
#endif
extern OD_ATTR_OD OD_t *OD;

/*******************************************************************************
    Object dictionary entries - shortcuts
*******************************************************************************/
#define OD_ENTRY_H1000 &OD->list[0]
#define OD_ENTRY_H1001 &OD->list[1]
#define OD_ENTRY_H1003 &OD->list[2]
#define OD_ENTRY_H1005 &OD->list[3]
#define OD_ENTRY_H1006 &OD->list[4]
#define OD_ENTRY_H1007 &OD->list[5]
#define OD_ENTRY_H1008 &OD->list[6]
#define OD_ENTRY_H1009 &OD->list[7]
#define OD_ENTRY_H100A &OD->list[8]
#define OD_ENTRY_H1012 &OD->list[9]
#define OD_ENTRY_H1014 &OD->list[10]
#define OD_ENTRY_H1015 &OD->list[11]
#define OD_ENTRY_H1016 &OD->list[12]
#define OD_ENTRY_H1017 &OD->list[13]
#define OD_ENTRY_H1018 &OD->list[14]
#define OD_ENTRY_H1019 &OD->list[15]
#define OD_ENTRY_H1200 &OD->list[16]
#define OD_ENTRY_H1400 &OD->list[17]
#define OD_ENTRY_H1401 &OD->list[18]
#define OD_ENTRY_H1600 &OD->list[19]
#define OD_ENTRY_H1601 &OD->list[20]
#define OD_ENTRY_H1800 &OD->list[21]
#define OD_ENTRY_H1801 &OD->list[22]
#define OD_ENTRY_H1A00 &OD->list[23]
#define OD_ENTRY_H1A01 &OD->list[24]
#define OD_ENTRY_H2000 &OD->list[25]
#define OD_ENTRY_H2001 &OD->list[26]
#define OD_ENTRY_H2002 &OD->list[27]
#define OD_ENTRY_H2100 &OD->list[28]
#define OD_ENTRY_H2101 &OD->list[29]
#define OD_ENTRY_H2102 &OD->list[30]
#define OD_ENTRY_H2103 &OD->list[31]
#define OD_ENTRY_H2104 &OD->list[32]
#define OD_ENTRY_H2105 &OD->list[33]
#define OD_ENTRY_H2106 &OD->list[34]
#define OD_ENTRY_H2107 &OD->list[35]

/*******************************************************************************
    Object dictionary entries - shortcuts with names
*******************************************************************************/
#define OD_ENTRY_H1000_deviceType &OD->list[0]
#define OD_ENTRY_H1001_errorRegister &OD->list[1]
#define OD_ENTRY_H1003_pre_definedErrorField &OD->list[2]
#define OD_ENTRY_H1005_COB_ID_SYNCMessage &OD->list[3]
#define OD_ENTRY_H1006_communicationCyclePeriod &OD->list[4]
#define OD_ENTRY_H1007_synchronousWindowLength &OD->list[5]
#define OD_ENTRY_H1008_manufacturerDeviceName &OD->list[6]
#define OD_ENTRY_H1009_manufacturerHardwareVersion &OD->list[7]
#define OD_ENTRY_H100A_manufacturerSoftwareVersion &OD->list[8]
#define OD_ENTRY_H1012_COB_IDTimeStampObject &OD->list[9]
#define OD_ENTRY_H1014_COB_ID_EMCY &OD->list[10]
#define OD_ENTRY_H1015_inhibitTimeEMCY &OD->list[11]
#define OD_ENTRY_H1016_consumerHeartbeatTime &OD->list[12]
#define OD_ENTRY_H1017_producerHeartbeatTime &OD->list[13]
#define OD_ENTRY_H1018_identity &OD->list[14]
#define OD_ENTRY_H1019_synchronousCounterOverflowValue &OD->list[15]
#define OD_ENTRY_H1200_SDOServerParameter &OD->list[16]
#define OD_ENTRY_H1400_RPDOCommunicationParameter &OD->list[17]
#define OD_ENTRY_H1401_RPDOCommunicationParameter &OD->list[18]
#define OD_ENTRY_H1600_RPDOMappingParameter &OD->list[19]
#define OD_ENTRY_H1601_RPDOMappingParameter &OD->list[20]
#define OD_ENTRY_H1800_TPDOCommunicationParameter &OD->list[21]
#define OD_ENTRY_H1801_TPDOCommunicationParameter &OD->list[22]
#define OD_ENTRY_H1A00_TPDOMappingParameter &OD->list[23]
#define OD_ENTRY_H1A01_TPDOMappingParameter &OD->list[24]
#define OD_ENTRY_H2000_vendorArray1k &OD->list[25]
#define OD_ENTRY_H2001_vendorDomain1k &OD->list[26]
#define OD_ENTRY_H2002_vendorDomain4k &OD->list[27]
#define OD_ENTRY_H2100_ledState &OD->list[28]
#define OD_ENTRY_H2101_adcSample &OD->list[29]
#define OD_ENTRY_H2102_pdoCounter &OD->list[30]
#define OD_ENTRY_H2103_loopCounter &OD->list[31]
#define OD_ENTRY_H2104_syncCounter &OD->list[32]
#define OD_ENTRY_H2105_lastSdoIndex &OD->list[33]
#define OD_ENTRY_H2106_emcyTrigger &OD->list[34]
#define OD_ENTRY_H2107_emcyCount &OD->list[35]

#endif /* OD_H */
