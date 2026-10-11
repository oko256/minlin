include(CMakeDependentOption)

# This macro sets up all configurable options for the project.
macro(minlin_setup_options)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        message(STATUS "[minlin] No build type selected, defaulting to Debug")
        set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
    endif()

    if(PROJECT_IS_TOP_LEVEL)
        option(minlin_DEVELOPER_MODE "Enable developer mode" OFF)

        # compile_commands.json is required for external tools
        set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
            "Generate compile_commands.json for use by external tools" FORCE)

        # GCC passes module-related flags (e.g. -fdeps-format, -fmodule-mapper, -fmodules-ts)
        # that clang-tidy does not recognize and cannot suppress. Disable module scanning with
        # GCC for now to avoid breaking clang-tidy support.
        # TODO: If ever want to make GCC + clang-tidy combination work, then maybe create
        # a wrapper script that is called instead of clang-tidy that filters out the
        # module-related flags first (or some other fix).
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
        endif()
    endif()

    option(minlin_HARDENING "Enable hardening compiler options" ON)
    cmake_dependent_option(
        minlin_HARDENING_GLOBALLY
        "Attempt to push hardening options to built dependencies"
        ON
        minlin_HARDENING
        OFF
    )

    # Default CXX extensions to OFF, but allow toggling via presets or by user.
    # Applied to targets with functions in target-properties.cmake.
    option(minlin_CXX_EXTENSIONS "Enable compiler specific C++ extensions (like gnu++)" OFF)

    # Default IPO/LTO to OFF for now as it can increase build times significantly and may cause
    # issues with some dependencies. Can be enabled manually if desired.
    option(minlin_IPO "Enable interprocedural optimization (IPO/LTO)" OFF)

    # Sanitizer options
    option(minlin_SANITIZER_ADDRESS    "Enable AddressSanitizer"                   OFF)
    option(minlin_SANITIZER_LEAK       "Enable LeakSanitizer"                      OFF)
    option(minlin_SANITIZER_UNDEFINED  "Enable UndefinedBehaviorSanitizer"         OFF)
    option(minlin_SANITIZER_THREAD     "Enable ThreadSanitizer"                    OFF)

    # These options only appear in developer mode:
    cmake_dependent_option(minlin_WARNINGS_AS_ERRORS "Treat all warnings as errors"
        ON minlin_DEVELOPER_MODE OFF)
    cmake_dependent_option(minlin_SA_CLANG_TIDY      "Enable clang-tidy static analysis"
        ON minlin_DEVELOPER_MODE OFF)
    cmake_dependent_option(minlin_SA_CPPCHECK        "Enable cppcheck static analysis"
        ON minlin_DEVELOPER_MODE OFF)
    cmake_dependent_option(minlin_SA_CPPCHECK_EXH    "Enable cppcheck's exhaustive check level"
        OFF minlin_DEVELOPER_MODE OFF)
    cmake_dependent_option(minlin_SA_IWYU            "Enable include-what-you-use static analysis"
        OFF minlin_DEVELOPER_MODE OFF)

    # Enable Ccache in developer mode if Ccache is available.
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        cmake_dependent_option(minlin_CCACHE "Enable Ccache for faster rebuilds"
            ON minlin_DEVELOPER_MODE OFF)
    endif()

    option(minlin_UNITY_BUILD "Enable unity build" OFF)

    option(minlin_COVERAGE "Enable code coverage reporting" OFF)
    set(minlin_COVERAGE_EXCLUDES
        "*/test/*;/usr/*;*/autogen-version/*;*/src/main.cpp;*/_deps/*"
        CACHE STRING
        "Glob patterns to exclude from code coverage, separated by ';'")

    if(NOT PROJECT_IS_TOP_LEVEL)
        # When used as a subproject, mark most options as advanced to reduce clutter.
        mark_as_advanced(
            minlin_CXX_EXTENSIONS
            minlin_IPO
            minlin_SANITIZER_ADDRESS
            minlin_SANITIZER_LEAK
            minlin_SANITIZER_UNDEFINED
            minlin_SANITIZER_THREAD
            minlin_WARNINGS_AS_ERRORS
            minlin_SA_CLANG_TIDY
            minlin_SA_CPPCHECK
            minlin_SA_CPPCHECK_EXH
            minlin_SA_IWYU
            minlin_CCACHE
        )
    endif()
endmacro()

# This macro applies options that need to be set before dependencies are built.
# These are options that affect the build as a whole (i.e. globally set options).
macro(minlin_apply_options_before_dependencies)
    # Enable global compiler hardening if requested.
    if(minlin_HARDENING AND minlin_HARDENING_GLOBALLY)
        include("${PROJECT_SOURCE_DIR}/cmake/hardening.cmake")
        minlin_enable_hardening(unused ON)
    endif()

    # Enable global IPO/LTO if requested.
    if(minlin_IPO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_check_output)
        if(ipo_supported)
            message(STATUS "[minlin] IPO/LTO enabled")
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        else()
            message(FATAL_ERROR
                "[minlin] IPO/LTO requested, but not supported: ${ipo_check_output}")
        endif()
    endif()

    # Validate code coverage option if requested.
    if(minlin_COVERAGE)
        if(NOT CMAKE_BUILD_TYPE MATCHES "Debug")
            message(FATAL_ERROR
                "[minlin] Code coverage can only be enabled for Debug builds since it "
                "is the most accurate without optimizations and with debug symbols.")
        endif()
        message(STATUS
            "[minlin] Code coverage enabled (disables optimization and forces debug symbols)")
    endif()
endmacro()

# This function applies various options to the given target. This should be called for all
# targets of this project that is desired to follow the custom project options.
function(minlin_apply_options target)
    message(STATUS "[minlin] Applying options for target: ${target}")
    include("${PROJECT_SOURCE_DIR}/cmake/warnings.cmake")
    minlin_enable_warnings(${target} ${minlin_WARNINGS_AS_ERRORS})

    # Enable compiler hardening for local targets if requested.
    if(minlin_HARDENING AND NOT minlin_HARDENING_GLOBALLY)
        include("${PROJECT_SOURCE_DIR}/cmake/hardening.cmake")
        minlin_enable_hardening(${target} OFF)
    endif()

    include("${PROJECT_SOURCE_DIR}/cmake/sanitizers.cmake")
    minlin_enable_sanitizers(
        ${target}
        ${minlin_SANITIZER_ADDRESS}
        ${minlin_SANITIZER_LEAK}
        ${minlin_SANITIZER_UNDEFINED}
        ${minlin_SANITIZER_THREAD}
    )

    if(minlin_SA_CLANG_TIDY)
        message(STATUS "[minlin] - Enabling clang-tidy static analysis")
        # This is actually configured per-target. See target-properties.cmake.
    endif()
    if(minlin_SA_CPPCHECK)
        message(STATUS "[minlin] - Enabling cppcheck static analysis")
        # This is actually configured per-target. See target-properties.cmake.
    endif()

    if(minlin_CCACHE)
        find_program(CCACHE_PROGRAM ccache)
        if(CCACHE_PROGRAM)
            message(STATUS "[minlin] - Ccache enabled")
            set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
            set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        else()
            message(FATAL_ERROR
                "[minlin] Ccache option is enabled but Ccache could not be found!")
        endif()
    endif()

    # Enable code coverage instrumentation for this target only, not for dependencies.
    if(minlin_COVERAGE)
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        target_link_options(${target} PRIVATE --coverage)
    endif()

    set_target_properties(${target} PROPERTIES UNITY_BUILD ${minlin_UNITY_BUILD})
    if(minlin_UNITY_BUILD)
        message(STATUS "[minlin] - Unity build enabled")
    endif()
endfunction()

# This function uses given 'test_target' as the executable target to generate code coverage.
# Code coverage is provided as a separate target named 'coverage_target'.
# Example: minlin_set_code_coverage_target(my-program-test coverage)
#          $ make coverage
function(minlin_set_code_coverage_target test_target coverage_target)
    if(minlin_COVERAGE)
        include("${PROJECT_SOURCE_DIR}/cmake/coverage.cmake")
        message(STATUS
            "[minlin] Target '${test_target}' provides code coverage target '${coverage_target}'")
        minlin_setup_coverage_target_fastcov(
            COVERAGE_TARGET ${coverage_target}
            EXEC_TARGET ${test_target}
            EXCLUDE "${minlin_COVERAGE_EXCLUDES}"
        )
    endif()
endfunction()
