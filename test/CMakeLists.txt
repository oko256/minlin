# Fetch Unity unit testing framework
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.7.0
)
set(FETCHCONTENT_UPDATES_DISCONNECTED_unity ON)
FetchContent_MakeAvailable(unity)

# List all test sources for the test executable here:
add_executable(minlin-test
    minlin.test.c
    minlin_hw.test.c
)
# List possible extra include directories for the test executable here:
target_include_directories(minlin-test PRIVATE
    .
)

set_property(TARGET minlin-test PROPERTY OUTPUT_NAME test_minlin)
target_compile_features(minlin-test PUBLIC c_std_11)
target_link_libraries(minlin-test PRIVATE minlin::minlin)
minlin_apply_options(minlin-test)
minlin_apply_test_target_properties(minlin-test)
minlin_set_code_coverage_target(minlin-test coverage)
# Link with unity
target_link_libraries(minlin-test PRIVATE unity::framework)
add_test(
    NAME minlin-tests
    COMMAND minlin-test
)
