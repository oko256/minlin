# This function enables various hardening compiler options.
# If enable_globally is OFF, then hardening flags are only applied to the given target.
# If enable_globally is ON, then hardening flags are applied to the full build so that it also
# affects all targets from dependencies etc. In this case target parameter is not considered.
function(minlin_enable_hardening
        target
        enable_globally)

    if(enable_globally)
        message(STATUS "[minlin] Enabled compiler hardening options globally")
    else()
        message(STATUS "[minlin] Enabled compiler hardening options for target: ${target}")
    endif()

    # Common hardening compile options for both GCC and Clang:
    list(APPEND hard_compile_options
        -D_GLIBCXX_ASSERTIONS           # Enable libc++ assertions.
        -fstrict-flex-arrays=3          # Enforce strict flex array semantics.
        -fstack-clash-protection        # Runtime checks for variable-size stack allocations.
        -fstack-protector-strong        # Runtime checks for stack-based buffer overflows.
    )

    # Common hardening link options for both GCC and Clang:
    list(APPEND hard_link_options
        -Wl,-z,nodlopen                 # Restrict dlopen to shared objects.
        -Wl,-z,noexecstack              # Data exec prevention by marking stack non-executable.
        -Wl,-z,relro                    # Read-only relocations.
        -Wl,-z,now                      # Bind symbols at program startup (slower startup).
        -Wl,--as-needed                 # Link only the actually used shared libraries.
        -Wl,--no-copy-dt-needed-entries # Do not copy unnecessary dynamic table entries.
    )

    if(NOT CMAKE_BUILD_TYPE MATCHES "Debug")
        # _FORTIFY_SOURCE=3 requires at least -O2 optimization level, so only add it for
        # non-Debug builds.
        list(APPEND hard_compile_options -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3)
    else()
        message(STATUS "[minlin] - Skipping _FORTIFY_SOURCE=3 hardening for Debug build.")
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES ".*x86_64.*")
        # Enable control-flow protection against ROP and JOP attacks on x86_64.
        list(APPEND hard_compile_options -fcf-protection=full)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES ".*aarch64.*"
            OR CMAKE_SYSTEM_PROCESSOR MATCHES ".*armv8.*"
            OR CMAKE_SYSTEM_PROCESSOR MATCHES ".*arm64.*")
        # Enable branch protection against ROP and JOP attacks on aarch64.
        list(APPEND hard_compile_options -mbranch-protection=standard)
    endif()

    # Extra options for executables:
    list(APPEND hard_compile_options_exe -fPIE)  # Position Independent Executable.
    list(APPEND hard_link_options_exe -pie)      # Position Independent Executable.

    # Extra options for shared libraries:
    list(APPEND hard_compile_options_shared -fPIC)  # Position Independent Code.
    list(APPEND hard_link_options_shared -shared)   # Shared library.

    message(VERBOSE "[minlin] - Compile options: ${hard_compile_options}")
    message(VERBOSE "[minlin] - + for executables: ${hard_compile_options_exe}")
    message(VERBOSE "[minlin] - + for shared libraries: ${hard_compile_options_shared}")
    message(VERBOSE "[minlin] - Link options: ${hard_link_options}")
    message(VERBOSE "[minlin] - + for executables: ${hard_link_options_exe}")
    message(VERBOSE "[minlin] - + for shared libraries: ${hard_link_options_shared}")

    if(enable_globally)
        add_compile_options(
            ${hard_compile_options}
            $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${hard_compile_options_exe}>
            $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${hard_compile_options_shared}>
        )
        add_link_options(
            ${hard_link_options}
            $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${hard_link_options_exe}>
            $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${hard_link_options_shared}>
        )
    else()
        target_compile_options(${target} PRIVATE
            ${hard_compile_options}
            $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${hard_compile_options_exe}>
            $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${hard_compile_options_shared}>
        )
        target_link_options(${target} PRIVATE
            ${hard_link_options}
            $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${hard_link_options_exe}>
            $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${hard_link_options_shared}>
        )
    endif()
endfunction()
