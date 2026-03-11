# 把任意输入路径统一转换成 CMake 使用的正斜杠风格，并移除尾部多余的 '/'.
# 这样后续做字符串比较时，不会因为 Windows/Unix 路径分隔符差异或者尾随斜杠而误判。
function(aurix_normalize_path input_path output_var)
    file(TO_CMAKE_PATH "${input_path}" normalized_path)
    string(REGEX REPLACE "/+$" "" normalized_path "${normalized_path}")
    set(${output_var} "${normalized_path}" PARENT_SCOPE)
endfunction()

# 判断某个路径是否应被排除。
# 逻辑分成两层：
# 1. 先和排除目录列表比对，如果候选路径等于某个目录或位于该目录下，则排除；
# 2. 如果不属于排除目录，再和排除文件列表做精确匹配。
function(aurix_path_is_excluded candidate_path excluded_dirs excluded_files result_var)
    aurix_normalize_path("${candidate_path}" normalized_candidate)
    set(is_excluded FALSE)

    # 目录排除不仅要匹配“完全相等”，还要匹配“是否位于该目录树下”。
    foreach(excluded_dir IN LISTS excluded_dirs)
        if(excluded_dir STREQUAL "")
            continue()
        endif()

        aurix_normalize_path("${excluded_dir}" normalized_dir)

        if(normalized_candidate STREQUAL normalized_dir)
            set(is_excluded TRUE)
            break()
        endif()

        string(FIND "${normalized_candidate}/" "${normalized_dir}/" prefix_index)
        if(prefix_index EQUAL 0)
            set(is_excluded TRUE)
            break()
        endif()
    endforeach()

    # 文件排除只做精确匹配，不做前缀判断，避免误排除同名但不同路径的文件。
    if(NOT is_excluded)
        foreach(excluded_file IN LISTS excluded_files)
            if(excluded_file STREQUAL "")
                continue()
            endif()

            aurix_normalize_path("${excluded_file}" normalized_file)
            if(normalized_candidate STREQUAL normalized_file)
                set(is_excluded TRUE)
                break()
            endif()
        endforeach()
    endif()

    set(${result_var} ${is_excluded} PARENT_SCOPE)
endfunction()

# 递归扫描一个目录，把其中的 .c、.h 与 include 目录全部收集出来。
# 设计特点：
# - 只识别 .c 和 .h；
# - 每到一个子目录，默认把该目录本身加入 include dirs；
# - 排除规则在进入目录前就生效，避免不必要的递归。
function(aurix_collect_directory_entries current_dir excluded_dirs excluded_files out_sources out_headers out_include_dirs)
    file(GLOB directory_entries CONFIGURE_DEPENDS "${current_dir}/*")

    set(local_sources)
    set(local_headers)
    set(local_include_dirs "${current_dir}")

    foreach(entry IN LISTS directory_entries)
        aurix_path_is_excluded("${entry}" "${excluded_dirs}" "${excluded_files}" is_excluded)
        if(is_excluded)
            continue()
        endif()

        # 目录递归时，把子目录采集到的源文件、头文件、include 目录向上合并。
        if(IS_DIRECTORY "${entry}")
            aurix_collect_directory_entries("${entry}" "${excluded_dirs}" "${excluded_files}" child_sources child_headers child_include_dirs)
            list(APPEND local_sources ${child_sources})
            list(APPEND local_headers ${child_headers})
            list(APPEND local_include_dirs ${child_include_dirs})
        else()
            # 文件类型判断统一转成小写，避免 .C / .H 之类大小写差异带来的遗漏。
            get_filename_component(entry_ext "${entry}" EXT)
            string(TOLOWER "${entry_ext}" entry_ext)

            if(entry_ext STREQUAL ".c")
                list(APPEND local_sources "${entry}")
            elseif(entry_ext STREQUAL ".h")
                list(APPEND local_headers "${entry}")
            endif()
        endif()
    endforeach()

    set(${out_sources} "${local_sources}" PARENT_SCOPE)
    set(${out_headers} "${local_headers}" PARENT_SCOPE)
    set(${out_include_dirs} "${local_include_dirs}" PARENT_SCOPE)
endfunction()

# 工程级文件收集入口。
# 这个函数只是薄封装，主要职责是：
# - 解析参数；
# - 调用递归扫描；
# - 对结果去重、排序、补根目录。
function(aurix_collect_project_files)
    set(options)
    set(one_value_args ROOT_DIR OUT_SOURCES OUT_HEADERS OUT_INCLUDE_DIRS)
    set(multi_value_args EXCLUDED_DIRS EXCLUDED_FILES)
    cmake_parse_arguments(AURIX "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    # ROOT_DIR 是递归扫描的起点，没有它就无法建立完整工程视图。
    if(NOT AURIX_ROOT_DIR)
        message(FATAL_ERROR "aurix_collect_project_files requires ROOT_DIR")
    endif()

    aurix_collect_directory_entries(
        "${AURIX_ROOT_DIR}"
        "${AURIX_EXCLUDED_DIRS}"
        "${AURIX_EXCLUDED_FILES}"
        filtered_sources
        filtered_headers
        include_dirs)

    # 再次补上 ROOT_DIR 本身，确保根目录下直接包含头文件时也能被找到。
    # 去重和排序有两个目的：
    # 1. 让目标属性更稳定，减少无意义的重新配置；
    # 2. 让 message/debug 输出更可读。
    list(APPEND include_dirs "${AURIX_ROOT_DIR}")
    list(REMOVE_DUPLICATES filtered_sources)
    list(REMOVE_DUPLICATES filtered_headers)
    list(REMOVE_DUPLICATES include_dirs)
    list(SORT filtered_sources)
    list(SORT filtered_headers)
    list(SORT include_dirs)

    set(${AURIX_OUT_SOURCES} "${filtered_sources}" PARENT_SCOPE)
    set(${AURIX_OUT_HEADERS} "${filtered_headers}" PARENT_SCOPE)
    set(${AURIX_OUT_INCLUDE_DIRS} "${include_dirs}" PARENT_SCOPE)
endfunction()

# 从 ADS 生成的 .cproject 中提取 excluding="..." 规则。
# 这一步很关键，因为很多 AURIX 工程原本是从 ADS 导出的：
# ADS 里哪些目录参与编译，CMake 侧最好同步，否则递归扫描容易把参考代码、SCR 目录、
# 其他 CPU 工程目录等一并扫进来。
function(aurix_extract_cproject_excluded_dirs)
    set(options)
    set(one_value_args CPROJECT_FILE ROOT_DIR OUT_EXCLUDED_DIRS)
    cmake_parse_arguments(AURIX "${options}" "${one_value_args}" "" ${ARGN})

    # .cproject 不存在时，不报错，直接返回空排除集，保证函数可选使用。
    if(NOT AURIX_CPROJECT_FILE OR NOT EXISTS "${AURIX_CPROJECT_FILE}")
        set(${AURIX_OUT_EXCLUDED_DIRS} "" PARENT_SCOPE)
        return()
    endif()

    # 当前实现读取第一个 excluding="..." 字段。
    # 对本仓库这类 ADS 导出工程已经足够；如果将来一个文件里出现多组 sourceEntries，
    # 再考虑扩展成提取所有匹配项并合并。
    file(READ "${AURIX_CPROJECT_FILE}" cproject_content)
    string(REGEX MATCH "excluding=\"([^\"]+)\"" exclude_match "${cproject_content}")

    if(NOT exclude_match)
        set(${AURIX_OUT_EXCLUDED_DIRS} "" PARENT_SCOPE)
        return()
    endif()

    # ADS 使用 '|' 分隔多个排除目录，这里把它转成 CMake 列表分隔符 ';'.
    set(raw_excludes "${CMAKE_MATCH_1}")
    string(REPLACE "|" ";" exclude_tokens "${raw_excludes}")

    set(excluded_dirs)
    foreach(exclude_token IN LISTS exclude_tokens)
        if(exclude_token STREQUAL "")
            continue()
        endif()

        # ADS 里既可能给绝对路径，也可能给相对根目录的路径。
        # 统一转成绝对路径后，再交给上层排除逻辑处理。
        if(IS_ABSOLUTE "${exclude_token}")
            set(excluded_path "${exclude_token}")
        else()
            set(excluded_path "${AURIX_ROOT_DIR}/${exclude_token}")
        endif()

        list(APPEND excluded_dirs "${excluded_path}")
    endforeach()

    list(REMOVE_DUPLICATES excluded_dirs)
    set(${AURIX_OUT_EXCLUDED_DIRS} "${excluded_dirs}" PARENT_SCOPE)
endfunction()