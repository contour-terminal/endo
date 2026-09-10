# SPDX-License-Identifier: Apache-2.0
#
# Asserts that the OS-level version metadata is actually IN the built binary --
# not merely that CMake computed the right strings.
#
# All three mechanisms (a Win32 resource, a Mach-O section, an ELF note) are
# applied by the LINKER, so a wrong flag produces a perfectly green build and a
# silent 0.0.0.0. Only reading the value back out of the binary catches that,
# which is exactly the regression this test exists for.
#
# Run via:
#   cmake -DBINARY=<exe> -DTARGET_SYSTEM=<CMAKE_SYSTEM_NAME>
#         -DEXPECT_FILE_VERSION=0.1.0.312 -DEXPECT_HUMAN_VERSION=0.1.0-312-g...
#         -DHAVE_VERSION_RESOURCE=<bool> -DHAVE_PACKAGE_METADATA=<bool>
#         -P cmake/tests/TestBinaryVersionMetadata.cmake
#
# Exits 77 (ctest SKIP_RETURN_CODE) whenever the platform has no probe, its gate
# is off, or the probe tool is not installed: a developer without `readelf` is not
# a failing build, and passing silently would be worse than either.

cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

foreach(_required BINARY TARGET_SYSTEM EXPECT_FILE_VERSION EXPECT_HUMAN_VERSION)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "binary-metadata: ${_required} must be set.")
    endif()
endforeach()

if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "binary-metadata: '${BINARY}' does not exist.")
endif()

# One row per platform, keyed by CMAKE_SYSTEM_NAME so no lookup is needed.
# Adding a platform is a row, not a branch -- including its skip condition, which
# is the _gate field rather than an if() of its own.
#   _probe_<system>_what   human name of the metadata under test
#   _probe_<system>_gate   name of a variable that must be true, or "" for always
#   _probe_<system>_tools  candidate programs, in preference order
#   _probe_<system>_args   arguments; @BINARY@ is substituted
#   _probe_<system>_expect substrings the output must ALL contain
#   _probe_<system>_reject substrings the output must NOT contain

# FileVersionRaw as well as FileVersion: the string block and the binary
# FILEVERSION are separate fields of a VERSIONINFO and can disagree.
set(_probe_Windows_what "Win32 VERSIONINFO resource")
set(_probe_Windows_gate "HAVE_VERSION_RESOURCE")
set(_probe_Windows_tools "pwsh;powershell")
# No ';' anywhere in the -Command payload: these argument strings are CMake
# lists, and a semicolon would split the command in half.
set(_probe_Windows_args
    "-NoProfile;-NonInteractive;-Command;(Get-Item '@BINARY@').VersionInfo | ForEach-Object { \"$($_.FileVersion) $($_.ProductVersion) $($_.FileVersionRaw)\" }")
set(_probe_Windows_expect "${EXPECT_FILE_VERSION};${EXPECT_HUMAN_VERSION}")
# The original bug, asserted directly.
set(_probe_Windows_reject "0[.]0[.]0[.]0")

# `otool -P` prints the __TEXT,__info_plist section as text; `otool -s` would
# print a hex dump this script would then have to decode.
set(_probe_Darwin_what "embedded __TEXT,__info_plist section")
set(_probe_Darwin_gate "")
set(_probe_Darwin_tools "otool")
set(_probe_Darwin_args "-P;@BINARY@")
set(_probe_Darwin_expect "CFBundleShortVersionString;CFBundleIdentifier;${EXPECT_FILE_VERSION}")
set(_probe_Darwin_reject "")

# `readelf -p <section>` string-dumps the section whether or not that binutils
# recognises note type 0xcafe1a7e, which `readelf -n` older than 2.39 does not.
set(_probe_Linux_what ".note.package ELF note")
set(_probe_Linux_gate "HAVE_PACKAGE_METADATA")
set(_probe_Linux_tools "readelf;eu-readelf")
set(_probe_Linux_args "-p;.note.package;@BINARY@")
set(_probe_Linux_expect "\"version\";${EXPECT_HUMAN_VERSION}")
set(_probe_Linux_reject "")

if(NOT DEFINED _probe_${TARGET_SYSTEM}_tools)
    message(STATUS "binary-metadata: no probe for system '${TARGET_SYSTEM}'; skipping.")
    cmake_language(EXIT 77)
endif()

set(_what "${_probe_${TARGET_SYSTEM}_what}")

# The mechanism may have been switched off at configure time -- by a linker that
# does not know --package-metadata, or by a build with no RC language. Either was
# reported then; repeating it as a failure here would be noise.
#
# Nested rather than one `if(_gate AND DEFINED ${_gate} AND ...)`: a row with no
# gate leaves _gate empty, `${_gate}` then expands to nothing, and CMake sees a
# dangling DEFINED with no argument and aborts with "Unknown arguments
# specified". Only the gate-less row hits it, so it passes everywhere a gate is
# set and fails on macOS alone.
set(_gate "${_probe_${TARGET_SYSTEM}_gate}")
if(_gate)
    if(DEFINED ${_gate} AND NOT ${_gate})
        message(STATUS "binary-metadata: ${_gate} is off, so no ${_what} was embedded; skipping.")
        cmake_language(EXIT 77)
    endif()
endif()

find_program(_tool NAMES ${_probe_${TARGET_SYSTEM}_tools})
if(NOT _tool)
    message(STATUS
        "binary-metadata: none of '${_probe_${TARGET_SYSTEM}_tools}' is installed, so the "
        "${_what} cannot be read back; skipping.")
    cmake_language(EXIT 77)
endif()

set(_args "")
foreach(_arg IN LISTS _probe_${TARGET_SYSTEM}_args)
    string(REPLACE "@BINARY@" "${BINARY}" _arg "${_arg}")
    list(APPEND _args "${_arg}")
endforeach()

execute_process(
    COMMAND "${_tool}" ${_args}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "binary-metadata: reading the ${_what} from '${BINARY}' failed "
        "(${_result}):\n${_stdout}${_stderr}")
endif()

# message(SEND_ERROR) is what makes `cmake -P` exit non-zero; the trailing
# FATAL_ERROR only adds the context a bare "does not contain" line lacks.
set(_failed FALSE)
foreach(_want IN LISTS _probe_${TARGET_SYSTEM}_expect)
    if(NOT _stdout MATCHES "${_want}")
        message(SEND_ERROR "binary-metadata: the ${_what} does not contain '${_want}'.")
        set(_failed TRUE)
    endif()
endforeach()
foreach(_unwanted IN LISTS _probe_${TARGET_SYSTEM}_reject)
    if(_stdout MATCHES "${_unwanted}")
        message(SEND_ERROR "binary-metadata: the ${_what} still reports '${_unwanted}'.")
        set(_failed TRUE)
    endif()
endforeach()

if(_failed)
    string(STRIP "${_stdout}" _stdout)
    message(FATAL_ERROR
        "binary-metadata: '${BINARY}' should report file version "
        "'${EXPECT_FILE_VERSION}' and human version '${EXPECT_HUMAN_VERSION}'.\n"
        "${_tool} said:\n${_stdout}")
endif()

message(STATUS "binary-metadata: ${_what} reports ${EXPECT_FILE_VERSION}.")
