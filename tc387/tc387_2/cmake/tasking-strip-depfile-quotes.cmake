# TASKING cctc 的 --dep-file 输出会给路径加双引号，
# ninja >= 1.13 会把引号当作路径的一部分记录进 .ninja_deps，
# 启动时批量 stat 目录 (FindFirstFileExA) 直接报错:
#   ninja: error: FindFirstFileExA("C:/...): syntax is incorrect
# 此脚本在每次编译后去除 dep 文件中的引号（本工程路径不含空格，安全）。
if(NOT DEFINED DEP_FILE)
    message(FATAL_ERROR "DEP_FILE not defined")
endif()
if(EXISTS "${DEP_FILE}")
    file(READ "${DEP_FILE}" _content)
    string(REPLACE "\"" "" _content "${_content}")
    file(WRITE "${DEP_FILE}" "${_content}")
endif()
