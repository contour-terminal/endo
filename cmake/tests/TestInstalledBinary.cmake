# SPDX-License-Identifier: Apache-2.0
#
# Smoke test for the installed endo executable. Installs the project into a
# throwaway prefix and runs `endo --version` there, so that the dynamic loader
# has to resolve every shared library from the executable's own RPATH.
#
# This is what catches a dependency living outside the loader's search path
# (binaryen on Fedora, for instance): the build-tree binary starts fine because
# CMake gives it an automatic RPATH, while the installed one dies with
# "error while loading shared libraries" -- a failure no other test sees.
#
# Run via:
#   cmake -DBUILD_DIR=<dir> [-DCONFIG=<cfg>] -P cmake/tests/TestInstalledBinary.cmake

if(NOT BUILD_DIR)
    message(FATAL_ERROR "installed-binary: BUILD_DIR must be set.")
endif()

set(_prefix "${BUILD_DIR}/install-smoke-test")
file(REMOVE_RECURSE "${_prefix}")

set(_install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${_prefix}")
if(CONFIG)
    list(APPEND _install_command --config "${CONFIG}")
endif()

execute_process(COMMAND ${_install_command} RESULT_VARIABLE _result OUTPUT_QUIET ERROR_VARIABLE _stderr)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "installed-binary: install into '${_prefix}' failed (${_result}):\n${_stderr}")
endif()

find_program(_endo endo PATHS "${_prefix}/bin" NO_DEFAULT_PATH REQUIRED)

# Clear the library search-path overrides: a developer's environment may point
# at the build tree, which would mask exactly the breakage under test. The
# installed binary must stand on its own, as it does under a login shell.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH --unset=DYLD_LIBRARY_PATH
            "${_endo}" --version
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "installed-binary: '${_endo} --version' failed (${_result}).\n"
        "The installed executable cannot resolve its shared libraries; a "
        "dependency outside the loader's search path is likely missing from "
        "its INSTALL_RPATH.\n${_stdout}${_stderr}")
endif()

string(STRIP "${_stdout}" _stdout)
if(NOT _stdout)
    message(FATAL_ERROR "installed-binary: '${_endo} --version' printed nothing.")
endif()

file(REMOVE_RECURSE "${_prefix}")
message(STATUS "installed-binary: ${_stdout}")
