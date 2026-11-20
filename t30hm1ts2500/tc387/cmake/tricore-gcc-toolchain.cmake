set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR tricore)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(AURIX_TOOLCHAIN_BIN "/opt/tricore-gcc-13.4.1/bin" CACHE PATH "TriCore GCC bin")
set(_p "${AURIX_TOOLCHAIN_BIN}/tricore-elf")

set(CMAKE_C_COMPILER   "${_p}-gcc")
set(CMAKE_ASM_COMPILER "${_p}-gcc")
set(CMAKE_AR           "${_p}-ar")
set(CMAKE_RANLIB       "${_p}-ranlib")
set(CMAKE_OBJCOPY      "${_p}-objcopy")
set(CMAKE_OBJDUMP      "${_p}-objdump")
set(CMAKE_SIZE         "${_p}-size")

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
