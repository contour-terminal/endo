cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(endo VERSION "0.0.0")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")


# setting defaults
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENDO_USE_LLVM "Use llvm as a backend for the shell" ON)

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
include(EndoThirdParties)
include(PedanticCompiler)

if((CMAKE_CXX_COMPILER_ID STREQUAL "Clang") OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    set(CMAKE_CXX_FLAGS_DEBUG "-Og -g")
endif()

if(NOT WIN32 AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build mode." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

# Enables STL container checker if not building a release.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-D_GLIBCXX_ASSERTIONS)
endif()

enable_testing()

add_subdirectory(src)
EndoThirdPartiesSummary2()
message(STATUS "--------------------------------")
message(STATUS "          output flags          ")
message(STATUS "--------------------------------")
message(STATUS "endo trace vm       ${ENDO_TRACE_VM}")
message(STATUS "endo trace parser   ${ENDO_TRACE_PARSER}")
message(STATUS "endo trace lexer    ${ENDO_TRACE_LEXER}")
message(STATUS "--------------------------------")
