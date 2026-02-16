cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

project(endo VERSION "0.0.0" LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")


# setting defaults
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


if(NOT DEFINED ENDO_TRACE_VM)
    option(ENDO_TRACE_VM "Enables debug output" OFF)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(ENDO_TRACE_VM ON)
    endif()
endif()

if(NOT DEFINED ENDO_TRACE_PARSER)
    option(ENDO_TRACE_PARSER "Enables debug output for Parser" OFF)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(ENDO_TRACE_PARSER ON)
    endif()
endif()


if(NOT DEFINED ENDO_TRACE_LEXER)
    option(ENDO_TRACE_LEXER "Enables debug output for Lexer" OFF)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(ENDO_TRACE_LEXER ON)
    endif()
endif()


include(EnableCcache)
include(StaticLinking)
include(EndoThirdParties)
include(PedanticCompiler)
include(Sanitizers)

if(NOT EMSCRIPTEN)
    check_static_system_requirements()
endif()

if((CMAKE_CXX_COMPILER_ID STREQUAL "Clang") OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    set(CMAKE_CXX_FLAGS_DEBUG "-Og -g")
endif()

if(NOT WIN32 AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build mode." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

# Enables STL container checker if not building a release.
if(NOT EMSCRIPTEN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-D_GLIBCXX_ASSERTIONS)
endif()

enable_testing()

# Apply Emscripten compatibility patches to vendored sources.
# crispy is symlinked from contour, so we use sed for in-place patching
# (GNU patch does not follow symlinked directories).
if(EMSCRIPTEN)
    file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/crispy/BufferObject.h" _BUFOBJ_H)
    if(EXISTS "${_BUFOBJ_H}")
        file(READ "${_BUFOBJ_H}" _BUFOBJ_CONTENT)
        string(FIND "${_BUFOBJ_CONTENT}" "buffer_object_ptr<T> buffer_object<T>::create(" _PATCH_POS)
        if(NOT _PATCH_POS EQUAL -1)
            string(REPLACE
                "buffer_object_ptr<T> buffer_object<T>::create(size_t capacity, buffer_object_release<T> release)"
                "auto buffer_object<T>::create(size_t capacity, buffer_object_release<T> release) -> buffer_object_ptr<T>"
                _BUFOBJ_CONTENT "${_BUFOBJ_CONTENT}")
            file(WRITE "${_BUFOBJ_H}" "${_BUFOBJ_CONTENT}")
            message(STATUS "Applied Emscripten patch to BufferObject.h (trailing return type)")
        else()
            message(STATUS "BufferObject.h already patched or signature changed")
        endif()
    endif()
endif()

add_subdirectory(src)
EndoThirdPartiesSummary2()
message(STATUS "==============================================================================")
message(STATUS "          build flags")
message(STATUS "------------------------------------------------------------------------------")
message(STATUS "endo trace vm       ${ENDO_TRACE_VM}")
message(STATUS "endo trace parser   ${ENDO_TRACE_PARSER}")
message(STATUS "endo trace lexer    ${ENDO_TRACE_LEXER}")
message(STATUS "Static linking      ${ENABLE_STATIC_LINKING}")
if(NOT EMSCRIPTEN)
    message(STATUS "Address Sanitizer   ${ENABLE_ASAN}")
    message(STATUS "UB Sanitizer        ${ENABLE_UBSAN}")
    message(STATUS "Thread Sanitizer    ${ENABLE_TSAN}")
endif()
message(STATUS "------------------------------------------------------------------------------")
