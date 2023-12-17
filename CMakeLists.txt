cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(endo VERSION "0.0.0" LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")


# setting defaults
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


option(ENDO_TRACE "Enables debug output" ON)
option(ENDO_TRACE_PARSER "Enables debug output for Parser" ON)
option(ENDO_TRACE_LEXER "Enables debug output for Lexer" ON)

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
message(" flags ")
message(" ENDO_TRACE :  ${ENDO_TRACE}")
message(" ENDO_TRACE_PARSER :  ${ENDO_TRACE_PARSER}")
message(" ENDO_TRACE_LEXER :  ${ENDO_TRACE_LEXER}")
