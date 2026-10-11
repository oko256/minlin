cmake_minimum_required(VERSION 3.28)
include(cmake/build-guard.cmake)

project(
    minlin
    DESCRIPTION "Deliberately minimalistic LIN bus library"
    HOMEPAGE_URL "https://github.com/oko256/minlin"
    LANGUAGES C
    # By default, the version is set from Git tags, but you can override it here:
    # VERSION 1.2.3
)
set(CPACK_PACKAGE_VENDOR "oko256")

include(cmake/version.cmake)
if(NOT PROJECT_VERSION)
    # Set the default release branch of your project here (builds from other branches will be
    # noted in the version info output):
    minlin_set_project_version_from_git_tag(DEFAULT_BRANCH "main")
endif()
message(STATUS
    "[minlin] Project version: ${PROJECT_VERSION_FULL_STRING} "
    "(numeric: ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})"
)

if(PROJECT_IS_TOP_LEVEL)
    # Set ON if you want this project to build possible libraries as shared libraries by default,
    # and set OFF if you prefer static libraries instead.
    option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
endif()

include(cmake/options.cmake)
include(cmake/target-properties.cmake)
minlin_setup_options()
minlin_apply_options_before_dependencies()
include(dependencies.cmake)

# Project Sources #############################################################

add_subdirectory(src/minlin)

# Documentation ###############################################################

if(PROJECT_IS_TOP_LEVEL)
    find_package(Doxygen QUIET)
    if(Doxygen_FOUND)
        set(DOXYGEN_FILE_PATTERNS *.c *.h *.md)
        set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${PROJECT_SOURCE_DIR}/README.md")
        set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
        set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
        set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
        doxygen_add_docs(doc
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/test
            ${PROJECT_SOURCE_DIR}/README.md
            COMMENT "Generate documentation with Doxygen"
        )
    endif()
endif()

###############################################################################

if(PROJECT_IS_TOP_LEVEL)
    include(CPack)
else()
    # Bail out here before even looking at tests if used as a subproject.
    return()
endif()

include(cmake/tests.cmake)
minlin_setup_tests()
