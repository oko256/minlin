# This function creates a new target specified with COVERAGE_TARGET argument, which generates
# code coverage report using fastcov for executable target EXEC_TARGET (can also be executable
# path). Additional arguments for executable target can be provided with EXEC_ARGS argument.
# EXCLUDE argument can be used to provide list of glob patterns to exclude from the report.
#
# More excludes can also be provided globally with GLOBAL_COVERAGE_EXCLUDES global property.
# You can add global exclude glob patterns wherever like this:
# set_property(GLOBAL APPEND PROPERTY GLOBAL_COVERAGE_EXCLUDES "*/ignore-these/*")
# Or specific files like this (e.g. in src/CMakeLists.txt file):
# set_property(GLOBAL APPEND PROPERTY GLOBAL_COVERAGE_EXCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/ignore-this-file.cpp")
#
# Base directory defaults to PROJECT_BINARY_DIR, but can be changed with BASE_DIR argument.
function(minlin_setup_coverage_target_fastcov)
    cmake_parse_arguments(
        arg
        "" # Options
        "COVERAGE_TARGET;BASE_DIR;EXEC_TARGET" # One-value arguments
        "EXEC_ARGS;EXCLUDE" # Multi-value arguments
        ${ARGN}
    )

    find_program(FASTCOV_PROGRAM NAMES fastcov fastcov.py)
    if(NOT FASTCOV_PROGRAM)
        message(FATAL_ERROR
            "[minlin] Fastcov is required for code coverage but was not found in PATH."
        )
    endif()
    find_program(GCOV_PROGRAM NAMES gcov)
    if(NOT GCOV_PROGRAM)
        message(FATAL_ERROR
            "[minlin] gcov is required for code coverage but was not found in PATH."
        )
    endif()
    find_program(GENHTML_PROGRAM NAMES genhtml genhtml.perl genhtml.bat)
    if(NOT GENHTML_PROGRAM)
        message(FATAL_ERROR
            "[minlin] genhtml is required for code coverage but was not found in PATH."
        )
    endif()

    if(arg_BASE_DIR)
        set(_base_dir "${arg_BASE_DIR}")
    else()
        set(_base_dir "${PROJECT_BINARY_DIR}")
    endif()

    set(_excludes "")
    get_property(_excludes_from_global GLOBAL PROPERTY GLOBAL_COVERAGE_EXCLUDES)
    foreach(i_exclude ${arg_EXCLUDE} ${_excludes_from_global})
        list(APPEND _excludes "${i_exclude}")
    endforeach()
    list(REMOVE_DUPLICATES _excludes)

    set(_genhtml_extra_args "--ignore-errors;inconsistent,unsupported;--demangle-cpp")

    set(_zero_cmd ${FASTCOV_PROGRAM}
        --branch-coverage
        --gcov ${GCOV_PROGRAM}
        --search-directory ${_base_dir}
        --zerocounters
    )
    set(_exec_cmd ${arg_EXEC_TARGET} ${arg_EXEC_ARGS})
    set(_capture_cmd ${FASTCOV_PROGRAM}
        --branch-coverage
        --gcov ${GCOV_PROGRAM}
        --search-directory ${_base_dir}
        --process-gcno
        --output ${arg_COVERAGE_TARGET}-fastcov.json
        --exclude-glob ${_excludes}
    )
    set(_convert_cmd ${FASTCOV_PROGRAM}
        --branch-coverage
        -C ${arg_COVERAGE_TARGET}-fastcov.json
        --lcov
        --output ${arg_COVERAGE_TARGET}-fastcov.info
    )
    set(_genhtml_cmd ${GENHTML_PROGRAM}
        --branch-coverage
        ${_genhtml_extra_args}
        -o ${arg_COVERAGE_TARGET}-fastcov
        ${arg_COVERAGE_TARGET}-fastcov.info
    )

    add_custom_target(${arg_COVERAGE_TARGET}
        COMMAND ${_zero_cmd}
        COMMAND ${_exec_cmd}
        COMMAND ${_capture_cmd}
        COMMAND ${_convert_cmd}
        COMMAND ${_genhtml_cmd}
        BYPRODUCTS
            ${arg_COVERAGE_TARGET}-fastcov.json
            ${arg_COVERAGE_TARGET}-fastcov.info
            ${arg_COVERAGE_TARGET}-fastcov/index.html
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        DEPENDS ${arg_EXEC_TARGET}
        VERBATIM
        COMMENT
        "Generating code coverage report '${arg_COVERAGE_TARGET}-fastcov' for target '${arg_EXEC_TARGET}'"
    )

    add_custom_command(TARGET ${arg_COVERAGE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo
        "Code coverage report generated: "
        "${arg_COVERAGE_TARGET}-fastcov.json ${arg_COVERAGE_TARGET}-fastcov.info ${arg_COVERAGE_TARGET}-fastcov/index.html"
        COMMAND ${CMAKE_COMMAND} -E echo
            "Open in browser: file://${PROJECT_BINARY_DIR}/${arg_COVERAGE_TARGET}-fastcov/index.html"
    )
endfunction()
