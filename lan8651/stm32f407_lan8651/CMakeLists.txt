cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32f407_lan8651)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    LAN8651/lan8651.c
    lwip/port/ethernetif.c
    lwip/port/sys_arch.c
    lwip/src/core/init.c
    lwip/src/core/def.c
    lwip/src/core/dns.c
    lwip/src/core/inet_chksum.c
    lwip/src/core/ip.c
    lwip/src/core/mem.c
    lwip/src/core/memp.c
    lwip/src/core/netif.c
    lwip/src/core/pbuf.c
    lwip/src/core/raw.c
    lwip/src/core/stats.c
    lwip/src/core/sys.c
    lwip/src/core/altcp.c
    lwip/src/core/altcp_alloc.c
    lwip/src/core/altcp_tcp.c
    lwip/src/core/tcp.c
    lwip/src/core/tcp_in.c
    lwip/src/core/tcp_out.c
    lwip/src/core/timeouts.c
    lwip/src/core/udp.c
    lwip/src/core/ipv4/autoip.c
    lwip/src/core/ipv4/dhcp.c
    lwip/src/core/ipv4/etharp.c
    lwip/src/core/ipv4/icmp.c
    lwip/src/core/ipv4/igmp.c
    lwip/src/core/ipv4/ip4.c
    lwip/src/core/ipv4/ip4_addr.c
    lwip/src/core/ipv4/ip4_frag.c
    lwip/src/core/ipv4/acd.c
    lwip/src/netif/ethernet.c
    lwip/src/apps/lwiperf/lwiperf.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    LAN8651
    lwip/port
    lwip/src/include
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
