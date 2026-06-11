# Copyright 2023-2025 HPMicro
# SPDX-License-Identifier: BSD-3-Clause

sdk_compile_definitions_ifdef(BUILD_FOR_SECONDARY_CORE BOARD_RUNNING_CORE=HPM_CORE1)

sdk_inc(.)
sdk_src(pinmux.c)
sdk_src(board.c)

if(APP_USE_ENET_PORT_COUNT EQUAL 1)
    if(APP_USE_ENET_ITF_RGMII)
        set(ENV{CONFIG_ENET_PHY_88Q2112} 1)
        sdk_compile_definitions(-DRGMII=1 -D__USE_88Q2112=1)
    elseif(APP_USE_ENET_ITF_RMII)
        set(ENV{CONFIG_ENET_PHY_JL1111} 1)
        sdk_compile_definitions(-DRMII=1)
    elseif(APP_USE_ENET_ITF_MII)
        set(ENV{CONFIG_ENET_PHY_JL1111} 1)
        sdk_compile_definitions(-DMII=1)
    else()
        set(ENV{CONFIG_ENET_PHY_88Q2112} 1)
        sdk_compile_definitions(-DRGMII=1 -D__USE_88Q2112=1)
    endif()
endif()
