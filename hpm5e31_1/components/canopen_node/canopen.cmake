# CANopenNode 组件源文件清单（各例程 include 后使用 ${CANOPEN_SRCS} / ${CANOPEN_INCS}）
#
# 用法（在例程 CMakeLists.txt 中）：
#   set(CANOPEN_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../components/canopen_node)
#   include(${CANOPEN_DIR}/canopen.cmake)
#   sdk_inc(${CANOPEN_INCS})
#   sdk_app_src(src/main.c ${CANOPEN_SRCS})
#
# 只编译需要的对象：NMT/心跳、EMCY、SDO server、PDO、SYNC、TIME、LEDs。
# 未编译（默认关闭）：SDO client、LSS、SRDO/GFC、网关、Trace、storage。

if(NOT CANOPEN_DIR)
    set(CANOPEN_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()

set(CANOPEN_INCS
    ${CANOPEN_DIR}
    ${CANOPEN_DIR}/port
    ${CANOPEN_DIR}/od
)

set(CANOPEN_SRCS
    ${CANOPEN_DIR}/CANopen.c
    ${CANOPEN_DIR}/canopen_node.c
    ${CANOPEN_DIR}/port/CO_driver.c
    ${CANOPEN_DIR}/od/OD.c
    ${CANOPEN_DIR}/301/CO_Emergency.c
    ${CANOPEN_DIR}/301/CO_HBconsumer.c
    ${CANOPEN_DIR}/301/CO_NMT_Heartbeat.c
    ${CANOPEN_DIR}/301/CO_ODinterface.c
    ${CANOPEN_DIR}/301/CO_PDO.c
    ${CANOPEN_DIR}/301/CO_SDOserver.c
    ${CANOPEN_DIR}/301/CO_SYNC.c
    ${CANOPEN_DIR}/301/CO_TIME.c
    ${CANOPEN_DIR}/303/CO_LEDs.c
)
