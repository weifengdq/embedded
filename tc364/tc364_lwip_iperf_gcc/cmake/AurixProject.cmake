function(aurix_normalize_path input_path output_var)
    file(TO_CMAKE_PATH "${input_path}" normalized_path)
    string(REGEX REPLACE "/+$" "" normalized_path "${normalized_path}")
    set(${output_var} "${normalized_path}" PARENT_SCOPE)
endfunction()

function(aurix_path_is_excluded candidate_path excluded_dirs excluded_files result_var)
    aurix_normalize_path("${candidate_path}" normalized_candidate)
    set(is_excluded FALSE)

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

        if(IS_DIRECTORY "${entry}")
            aurix_collect_directory_entries("${entry}" "${excluded_dirs}" "${excluded_files}" child_sources child_headers child_include_dirs)
            list(APPEND local_sources ${child_sources})
            list(APPEND local_headers ${child_headers})
            list(APPEND local_include_dirs ${child_include_dirs})
        else()
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

function(aurix_collect_project_files)
    set(options)
    set(one_value_args ROOT_DIR OUT_SOURCES OUT_HEADERS OUT_INCLUDE_DIRS)
    set(multi_value_args EXCLUDED_DIRS EXCLUDED_FILES)
    cmake_parse_arguments(AURIX "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

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

function(aurix_extract_cproject_excluded_dirs)
    set(options)
    set(one_value_args CPROJECT_FILE ROOT_DIR OUT_EXCLUDED_DIRS)
    cmake_parse_arguments(AURIX "${options}" "${one_value_args}" "" ${ARGN})

    if(NOT AURIX_CPROJECT_FILE OR NOT EXISTS "${AURIX_CPROJECT_FILE}")
        set(${AURIX_OUT_EXCLUDED_DIRS} "" PARENT_SCOPE)
        return()
    endif()

    file(READ "${AURIX_CPROJECT_FILE}" cproject_content)
    string(REGEX MATCH "excluding=\"([^\"]+)\"" exclude_match "${cproject_content}")

    if(NOT exclude_match)
        set(${AURIX_OUT_EXCLUDED_DIRS} "" PARENT_SCOPE)
        return()
    endif()

    set(raw_excludes "${CMAKE_MATCH_1}")
    string(REPLACE "|" ";" exclude_tokens "${raw_excludes}")

    set(excluded_dirs)
    foreach(exclude_token IN LISTS exclude_tokens)
        if(exclude_token STREQUAL "")
            continue()
        endif()

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
