# ------------------------------------------------------------------
# TriCore GCC 链接包装脚本
# ------------------------------------------------------------------
# 用途：
#   tricore-elf 工具链自带的 ld.exe 不支持 @response 文件，而 CMake 的 Ninja
#   生成器在链接可执行文件时总会把对象列表写入 CMakeFiles/<target>.rsp，
#   并以 "@<target>.rsp" 形式传给 gcc，最终导致：
#       ld.exe: cannot find @C:\WINDOWS\TEMP\ccXXXXXX: Invalid argument
#
#   该行为无法通过 CMAKE_*_USE_RESPONSE_FILE_FOR_OBJECTS 关闭
#   （Ninja 生成器对链接规则硬编码使用 RSP_FILE）。
#
# 做法：
#   把真正的链接命令交给本脚本执行。脚本先把命令行中所有 "@xxx.rsp" 参数
#   就地展开成文件内容里的对象列表，再调用 gcc，从而绕开 response file。
#
# 调用方式（由 CMAKE_C_LINK_EXECUTABLE 组装）：
#   cmake -DGCC=<编译器> -DARGS=<以 | 分隔的参数列表> -P gcc-link-wrapper.cmake
# ------------------------------------------------------------------

if(NOT DEFINED GCC)
    message(FATAL_ERROR "gcc-link-wrapper: GCC not specified")
endif()

# ARGS 中 CMake 占位符（<FLAGS>/<OBJECTS> 等）展开后本身是"空格分隔"的多个参数，
# 各占位符之间用 '|' 连接。这里先把 '|' 转成空格，再统一按空白切分。
# 本工程路径不含空格，按空白切分是安全的。
string(REPLACE "|" " " _args_flat "${ARGS}")
string(REGEX REPLACE "[\r\n\t]+" " " _args_flat "${_args_flat}")
string(REGEX REPLACE " +" ";" _raw_args "${_args_flat}")

set(_final_args)
foreach(_arg IN LISTS _raw_args)
    if(_arg STREQUAL "")
        continue()
    endif()

    # 识别 @response 文件参数并展开
    string(SUBSTRING "${_arg}" 0 1 _first_char)
    if(_first_char STREQUAL "@")
        string(SUBSTRING "${_arg}" 1 -1 _rsp_path)

        if(NOT EXISTS "${_rsp_path}")
            message(FATAL_ERROR "gcc-link-wrapper: response file not found: ${_rsp_path}")
        endif()

        file(READ "${_rsp_path}" _rsp_content)
        # 换行统一成空格后按空白切分
        string(REGEX REPLACE "[\r\n]+" " " _rsp_content "${_rsp_content}")
        string(REGEX REPLACE " +" ";" _rsp_items "${_rsp_content}")

        foreach(_item IN LISTS _rsp_items)
            if(NOT _item STREQUAL "")
                list(APPEND _final_args "${_item}")
            endif()
        endforeach()
    else()
        list(APPEND _final_args "${_arg}")
    endif()
endforeach()

execute_process(
    COMMAND "${GCC}" ${_final_args}
    RESULT_VARIABLE _link_result
)

if(NOT _link_result EQUAL 0)
    message(FATAL_ERROR "gcc-link-wrapper: link failed with exit code ${_link_result}")
endif()
