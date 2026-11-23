# mcal.cmake —— HPM5E31 AUTOSAR MCAL 构建辅助（被各 MCAL 例程 CMakeLists.txt include）
#
# 用法（例程 CMakeLists.txt）：
#   find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
#   project(hpm5e31_1_mcal_mcu_clock)
#   sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
#   include(${CMAKE_CURRENT_LIST_DIR}/../../../mcal/mcal.cmake)
#   mcal_add(Mcu Port Dio)
#   sdk_app_src(src/main.c)
#
# mcal_add(<模块目录名> ...) 会：
#   1) 加入 mcal/common 头文件与 Det/Dem 源文件；
#   2) 对每个模块加入 include/、cfg/ 头文件目录，src/*.c、cfg/*.c 源文件。
# 模块目录名大小写必须与 mcal/ 下一致（Linux 区分大小写）。

if(NOT DEFINED MCAL_ROOT)
  get_filename_component(MCAL_ROOT "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
endif()

set(MCAL_ALL_MODULES Mcu Port Dio Gpt Pwm Icu Adc Spi I2c Can Wdg Mem Crc Eth EthTrcv)

function(mcal_add)
  sdk_inc(${MCAL_ROOT}/common)
  file(GLOB _mcal_common_srcs ${MCAL_ROOT}/common/*.c)
  if(_mcal_common_srcs)
    sdk_app_src(${_mcal_common_srcs})
  endif()

  foreach(_m ${ARGN})
    if(NOT _m IN_LIST MCAL_ALL_MODULES)
      message(FATAL_ERROR "mcal_add: 未知 MCAL 模块 '${_m}'，可选: ${MCAL_ALL_MODULES}")
    endif()
    set(_dir ${MCAL_ROOT}/${_m})
    if(NOT EXISTS ${_dir})
      message(FATAL_ERROR "mcal_add: 模块目录不存在 ${_dir}")
    endif()
    sdk_inc(${_dir}/include ${_dir}/cfg)
    file(GLOB _mcal_srcs ${_dir}/src/*.c ${_dir}/cfg/*.c)
    if(NOT _mcal_srcs)
      message(FATAL_ERROR "mcal_add: 模块 ${_m} 没有源文件")
    endif()
    sdk_app_src(${_mcal_srcs})
  endforeach()

  sdk_compile_definitions(-DHPM_MCAL_ENABLE=1)
endfunction()
