# This macro is only called if we are building the project as the top-level project.
# It sets up the building of tests.
macro(minlin_setup_tests)
    # Building tests is enabled by default if either global BUILD_TESTING=ON or
    # we are in developer mode.
    if(BUILD_TESTING OR minlin_DEVELOPER_MODE)
        set(minlin_BUILD_TESTING_DEFAULT ON)
    else()
        set(minlin_BUILD_TESTING_DEFAULT OFF)
    endif()
    option(minlin_BUILD_TESTING "Build tests for minlin" ${minlin_BUILD_TESTING_DEFAULT})
    if(minlin_BUILD_TESTING)
        message(STATUS "[minlin] Tests enabled")
        include(CTest)
        add_subdirectory(test)
    endif()
endmacro()
