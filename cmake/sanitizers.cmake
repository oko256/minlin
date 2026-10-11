# This function sets up requested sanitizers for the given target.
function(minlin_enable_sanitizers
        target
        enable_sanitizer_address
        enable_sanitizer_leak
        enable_sanitizer_undefined
        enable_sanitizer_thread)

    set(sanitizers_list "")
    if(enable_sanitizer_address)
        list(APPEND sanitizers_list "address")
    endif()
    if(enable_sanitizer_undefined)
        list(APPEND sanitizers_list "undefined")
    endif()
    if(enable_sanitizer_leak)
        list(APPEND sanitizers_list "leak")
    endif()
    if(enable_sanitizer_thread)
        # ThreadSanitizer cannot be combined with Leak or Address!
        if("address" IN_LIST sanitizers_list OR "leak" IN_LIST sanitizers_list)
            message(FATAL_ERROR
                "[minlin] ThreadSanitizer does not work together with "
                "AddressSanitizer or LeakSanitizer.")
        else()
            list(APPEND sanitizers_list "thread")
        endif()
    endif()

    # Combine sanitizers into a single string and enable for target if needed.
    list(JOIN sanitizers_list "," combined_sanitizers)
    if(combined_sanitizers AND NOT "${combined_sanitizers}" STREQUAL "")
        message(STATUS
            "[minlin] Enabling sanitizers [${combined_sanitizers}] for target: ${target}")
        target_compile_options(${target} PRIVATE -fsanitize=${combined_sanitizers})
        target_link_options(${target} PRIVATE -fsanitize=${combined_sanitizers})
        if(enable_sanitizer_undefined)
            # Make UndefinedBehaviorSanitizer findings fatal instead of recoverable.
            target_compile_options(${target} PRIVATE -fno-sanitize-recover=undefined)
            target_link_options(${target} PRIVATE -fno-sanitize-recover=undefined)
        endif()
        # Build-only: keeps this out of the install export set of static libraries.
        target_link_libraries(${target} PRIVATE $<BUILD_INTERFACE:minlin_sanitizer_defaults>)
    endif()
endfunction()

if(NOT TARGET minlin_sanitizer_defaults)
    # Create a small source file that provides default options for sanitizers.
    # Most of the built-in options are sane defaults, so only those that we want to differ are specified here.
    file(WRITE "${CMAKE_BINARY_DIR}/sanitizer_defaults.c" [=[
/*
 * Default options for sanitizer runtimes.
 *
 * These functions are called by the respective sanitizer runtimes at startup.
 * Environment variables (e.g. UBSAN_OPTIONS) can be used to override these defaults.
 */

/*** UndefinedBehaviorSanitizer ***/
const char *__ubsan_default_options(void) {
    return "print_stacktrace=1";
}

]=])
    add_library(minlin_sanitizer_defaults OBJECT "${CMAKE_BINARY_DIR}/sanitizer_defaults.c")
    set_target_properties(minlin_sanitizer_defaults PROPERTIES SKIP_LINTING ON)
endif()
