# Run 'git describe' and capture its output.
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --long
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE git_description
                RESULT_VARIABLE result)
if(result AND NOT result EQUAL 0)
    message(FATAL_ERROR "'git describe' failed: ${result}")
endif()
# Strip trailing newline.
string(REPLACE "\n" "" git_description "${git_description}")
# Split the output into its fragments.
# The description is in the form "<tag>-<commits-since-tag>-<hash>[-dirty]".
# (Note that <tag> may have internal '-' characters'.)
string(REPLACE "-" ";" fragments "${git_description}")
# Even if the tag doesn't have any internal dashes and there is no dirty
# component, we should still have three fragments in the description.
list(LENGTH fragments fragment_count)
if(${fragment_count} LESS 3)
    message(FATAL_ERROR
        "unable to parse 'git describe' output:\n${git_description}")
endif()

# Now work backwards interpreting the parts...

# Check for the dirty flag.
set(is_dirty FALSE)
math(EXPR index "${fragment_count} - 1")
list(GET fragments ${index} tail)
if (tail STREQUAL "dirty")
    set(is_dirty TRUE)
    math(EXPR index "${index} - 1")
endif()
string(TOLOWER ${is_dirty} is_dirty)

# Get the commit hash.
list(GET fragments ${index} commit_hash)
math(EXPR index "${index} - 1")

# Get the commits since the tag.
list(GET fragments ${index} commits_since_tag)

# The rest should be the tag.
list(SUBLIST fragments 0 ${index} tag)
string(JOIN "-" tag "${tag}")

# Generate the C++ code to represent all this.
set(cpp_code "\
// AUTOMATICALLY GENERATED!! - See version.cmake.\n\
#include <cradle/inner/utilities/git.h>\n\
static cradle::repository_info const version_info{\n\
  \"${commit_hash}\", ${is_dirty}, \"${tag}\", ${commits_since_tag} };\n\
")

# Generate the header file.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/internally_generated/src/cradle/")
set(header_file
    "${CMAKE_CURRENT_BINARY_DIR}/internally_generated/src/cradle/version_info.h")

if(EXISTS "${header_file}")
    file(READ "${header_file}" old_cpp_code)
    if("${cpp_code}" STREQUAL "${old_cpp_code}")
        message(VERBOSE "Keeping ${header_file}")
        return()
    endif()
endif()

message(VERBOSE "Generating ${header_file}")
file(WRITE "${header_file}" "${cpp_code}")
