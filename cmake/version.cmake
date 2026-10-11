# This function parses the project version from a Git tag, if matching tag exists.
# Tag should be in MAJOR.MINOR.PATCH format, optionally prefixed with "v" or "V".
# DEFAULT_BRANCH argument can be provided to specify which branch to consider
# as the main release branch (default branch is not mentioned in version info).
function(minlin_set_project_version_from_git_tag)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "DEFAULT_BRANCH" "")

    find_package(Git REQUIRED)

    # Resolve canonical git paths to set as dependencies to provide automatic reconfiguration
    # whenever git state of the repository changes.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --git-path HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_head_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _res1
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --git-path index
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_index_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _res2
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --git-path packed-refs
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_packed_refs_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _res3
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --git-path refs
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_refs_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _res4
    )
    if(_res1 EQUAL 0)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_head_path}")
    endif()
    if(_res2 EQUAL 0)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_index_path}")
    endif()
    if(_res3 EQUAL 0)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_packed_refs_path}")
    endif()
    if(_res4 EQUAL 0)
        # This glob makes CMake reconfigure when any ref changes (branches and tags).
        file(GLOB_RECURSE CONFIGURE_DEPENDS _git_ref_files
            "${PROJECT_SOURCE_DIR}/${git_refs_path}/*"
        )
    endif()

    # Get the latest tag, and number of commits since that tag with short hash if not exact match.
    # For example: v1.6.0-1-gbc018c7
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _git_describe_result
    )
    if(NOT _git_describe_result EQUAL 0)
        message(WARNING "Failed to run 'git describe'. Project version will be wrong.")
    endif()

    # Get current branch.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} branch --show-current
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_branch
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _git_branch_result
    )
    if(NOT _git_branch_result EQUAL 0)
        message(WARNING "Failed to run 'git branch'. Branch in project version will be wrong.")
        set(git_branch "unknown")
    endif()
    if(arg_DEFAULT_BRANCH AND git_branch STREQUAL arg_DEFAULT_BRANCH)
        set(git_branch "")
    endif()
    set(PROJECT_VERSION_BRANCH "${git_branch}" PARENT_SCOPE)

    # Get commit years for copyright purposes.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log --format=%ad --date=format:%Y
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE git_commit_years
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _git_years_result
    )
    if(_git_years_result EQUAL 0)
        # Convert newline-separated years into a CMake list.
        string(REPLACE "\n" ";" git_commit_years_list "${git_commit_years}")
        # Newest is the first line and oldest is the last line.
        list(GET git_commit_years_list 0 newest_year)
        list(GET git_commit_years_list -1 oldest_year)
        set(PROJECT_NEWEST_COMMIT_YEAR "${newest_year}" PARENT_SCOPE)
        set(PROJECT_OLDEST_COMMIT_YEAR "${oldest_year}" PARENT_SCOPE)
    else()
        message(WARNING "Failed to run 'git log' for commit years. Using current year only.")
    endif()

    # Default to 0.0.0 if unable to parse better result from next steps.
    set(_major 0)
    set(_minor 0)
    set(_patch 0)
    set(_full_str "0.0.0-unknown")
    set(_dev_build NO)

    string(REGEX REPLACE "^(v|V)" "" cleaned_tag "${git_describe}")
    if(cleaned_tag MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-.*)?$")
        # Tag matches MAJOR.MINOR.PATCH format
        set(_major ${CMAKE_MATCH_1})
        set(_minor ${CMAKE_MATCH_2})
        set(_patch ${CMAKE_MATCH_3})
        set(_full_str "${cleaned_tag}")
        # Check if this tag is not exact match (i.e. has additional suffix) and mark as dev build.
        if(NOT cleaned_tag STREQUAL "${_major}.${_minor}.${_patch}")
            set(_dev_build YES)
        endif()
    elseif(cleaned_tag)
        # Tag exists but does not match expected format, only populate version full string
        set(_full_str "${cleaned_tag}")
        message(WARNING
            "Git tag '${cleaned_tag}' does not match MAJOR.MINOR.PATCH format. "
            "Defaulting project version to ${_major}.${_minor}.${_patch}."
        )
    else()
        # No tag at all, use default values
        message(WARNING "No git tag found. Using default version values.")
    endif()

    message(STATUS "[minlin] Project version configured from git: ${cleaned_tag}")

    set(PROJECT_VERSION_MAJOR ${_major} PARENT_SCOPE)
    set(PROJECT_VERSION_MINOR ${_minor} PARENT_SCOPE)
    set(PROJECT_VERSION_PATCH ${_patch} PARENT_SCOPE)
    set(PROJECT_VERSION "${_major}.${_minor}.${_patch}" PARENT_SCOPE)
    set(PROJECT_VERSION_FULL_STRING "${_full_str}" PARENT_SCOPE)
    set(PROJECT_VERSION_IS_DEV_BUILD "${_dev_build}" PARENT_SCOPE)
endfunction()

# This function autogenerates version header and source files for the given 'target', that contain
# various version information functions with project name prefix.
# The output files are C-style so they can be used in both C and C++ targets.
function(minlin_add_version_info target)
    find_package(Git REQUIRED)

    # Setup script replaces this string with valid C-style identifier based on project name.
    set(VERSION_PREFIX "minlin")

    if(PROJECT_VERSION_BRANCH)
        set(VERSION_BRANCH "${PROJECT_VERSION_BRANCH}")
    else()
        set(VERSION_BRANCH "")
    endif()
    if(VERSION_BRANCH)
        set(VERSION_BRANCH_WITH_DELIM "${VERSION_BRANCH}:")
    else()
        set(VERSION_BRANCH_WITH_DELIM "")
    endif()

    if(PROJECT_OLDEST_COMMIT_YEAR)
        set(OLDEST_COMMIT_YEAR "${PROJECT_OLDEST_COMMIT_YEAR}")
    else()
        # Fallback to current year
        string(TIMESTAMP current_year "%Y")
        set(OLDEST_COMMIT_YEAR "${current_year}")
    endif()
    if(PROJECT_NEWEST_COMMIT_YEAR)
        set(NEWEST_COMMIT_YEAR "${PROJECT_NEWEST_COMMIT_YEAR}")
    else()
        # Fallback to current year
        string(TIMESTAMP current_year "%Y")
        set(NEWEST_COMMIT_YEAR "${current_year}")
    endif()
    if(OLDEST_COMMIT_YEAR EQUAL NEWEST_COMMIT_YEAR)
        set(COMMIT_YEARS_STRING "${OLDEST_COMMIT_YEAR}")
    else()
        set(COMMIT_YEARS_STRING "${OLDEST_COMMIT_YEAR}-${NEWEST_COMMIT_YEAR}")
    endif()

    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/version.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/autogen-version/${VERSION_PREFIX}_version.h"
        @ONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/version.c.in"
        "${CMAKE_CURRENT_BINARY_DIR}/autogen-version/${VERSION_PREFIX}_version.c"
        @ONLY
    )
    set_property(TARGET ${target} APPEND PROPERTY SOURCES
        "${CMAKE_CURRENT_BINARY_DIR}/autogen-version/${VERSION_PREFIX}_version.h"
        "${CMAKE_CURRENT_BINARY_DIR}/autogen-version/${VERSION_PREFIX}_version.c"
    )
    target_include_directories(${target} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/autogen-version>
    )
endfunction()

# Generate project version string if we have PROJECT_VERSION but no PROJECT_VERSION_FULL_STRING.
# This can happen if user sets VERSION in project() call manually and doesn't want to use git.
if(PROJECT_VERSION AND NOT PROJECT_VERSION_FULL_STRING)
    set(PROJECT_VERSION_FULL_STRING "${PROJECT_VERSION}")
endif()
