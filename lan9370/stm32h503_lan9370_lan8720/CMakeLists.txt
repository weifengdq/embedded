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
set(CMAKE_PROJECT_NAME stm32h503_lan9370_lan8720)

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
    Core/Src/app_extensions.c

    # LAN9370 Driver
    LAN9370/lan9370_spi.c
    LAN9370/mdio_bitbang.c
    LAN9370/lan9370_driver.c
    LAN9370/lan9370_persist.c
    LAN9370/lan8720_driver.c
    
    # Shell wrapper + letter-shell core
    Shell/shell_port.c
    Shell/letter-shell/src/shell.c
    Shell/letter-shell/src/shell_cmd_list.c
    Shell/letter-shell/src/shell_ext.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    LAN9370
    Shell
    Shell/letter-shell/src
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    SHELL_CFG_USER="shell_cfg_user.h"
    ENABLE_LWIP=0
    ENABLE_FREERTOS=0
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
