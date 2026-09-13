# SPDX-License-Identifier: Apache-2.0
#
# Runs endo-windows-dialog-canary once per way of failing that can raise a Windows dialog, and requires
# each run to EXIT non-zero within a bound. A run that is still waiting when the bound expires is waiting
# on a dialog nobody will click, and is the defect cmake/WindowsDialogs.cmake exists for.
#
# Why each run checks what it checks:
# - a TIMEOUT is refused by name: execute_process reports it as a string, not a number, and read as a
#   status it would be neither pass nor fail;
# - a zero exit is refused: every mode fails on purpose, so success means the failure never happened;
# - the assert run must print the assertion's own text on stderr, which is where the suppression sends a
#   CRT report -- a non-zero exit alone would also be what a crashed canary gives;
# - a mode the build does not compile (assert under NDEBUG) exits 77 and is reported as not exercised,
#   never as a pass.
#
# And the PRODUCT variant, both ways, without raising the dialog it keeps: PRODUCT_CANARY is the same
# program linked as a product, and its `probe` run prints what was installed before main(). With
# ENDO_SUPPRESS_WINDOWS_DIALOGS unset it must report nothing suppressed -- a user's shell keeps its
# dialogs -- and with it set, everything suppressed. The ordinary canary must report suppressed with the
# variable unset, which is what "unconditionally" means.
#
# And the ENVIRONMENT half, over the whole registration rather than one test: CTEST and BUILD_DIR name
# the ctest that runs this, whose `--show-only=json-v1` lists every registered test with its properties.
# Each must carry ENDO_SUPPRESS_WINDOWS_DIALOGS=set:1, since a test running the product binary without it
# is a test that can hang on an assert -- check-endo-formatting and check-doc-snippets both run endo.exe.
#
# Usage: cmake -DCANARY=<path> -DPRODUCT_CANARY=<path> -DCTEST=<ctest> -DBUILD_DIR=<dir>
#              [-DBOUND_SECONDS=20] -P cmake/tests/TestWindowsDialogCanary.cmake

cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

if(NOT DEFINED CANARY OR NOT EXISTS "${CANARY}")
    message(FATAL_ERROR "TestWindowsDialogCanary: CANARY (${CANARY}) is not set or does not exist.")
endif()
if(NOT DEFINED PRODUCT_CANARY OR NOT EXISTS "${PRODUCT_CANARY}")
    message(FATAL_ERROR "TestWindowsDialogCanary: PRODUCT_CANARY (${PRODUCT_CANARY}) is not set or does not exist.")
endif()
if(NOT DEFINED CTEST OR NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "TestWindowsDialogCanary: CTEST and BUILD_DIR must be set.")
endif()
if(NOT DEFINED BOUND_SECONDS)
    set(BOUND_SECONDS 20)
endif()

# "<mode>|<phrase stderr must contain, or empty>"
set(modes
    "assert|windows-dialog-canary asserts on purpose"
    "abort|"
    "invalid-parameter|"
    "access-violation|"
)

set(failures "")
set(exercised 0)
set(notExercised "")

execute_process(
    COMMAND "${CTEST}" --show-only=json-v1
    WORKING_DIRECTORY "${BUILD_DIR}"
    TIMEOUT ${BOUND_SECONDS}
    RESULT_VARIABLE listed
    OUTPUT_VARIABLE registration
    ERROR_VARIABLE registrationErrors)
if(NOT listed STREQUAL "0")
    message(FATAL_ERROR
        "windows-dialog-canary: `ctest --show-only=json-v1` in ${BUILD_DIR} did not answer (${listed}): "
        "${registrationErrors}. Which tests carry the environment cannot be read, which is not the same as "
        "all of them carrying it.")
endif()
string(JSON testCount LENGTH "${registration}" tests)
set(unmarked "")
math(EXPR lastTest "${testCount} - 1")
foreach(index RANGE ${lastTest})
    string(JSON testName GET "${registration}" tests ${index} name)
    string(JSON propertyCount ERROR_VARIABLE noProperties LENGTH "${registration}" tests ${index} properties)
    set(marked FALSE)
    if(NOT noProperties)
        math(EXPR lastProperty "${propertyCount} - 1")
        foreach(propertyIndex RANGE ${lastProperty})
            string(JSON propertyName GET "${registration}" tests ${index} properties ${propertyIndex} name)
            if(propertyName STREQUAL "ENVIRONMENT_MODIFICATION")
                string(JSON propertyValue GET "${registration}" tests ${index} properties ${propertyIndex} value)
                if(propertyValue MATCHES "ENDO_SUPPRESS_WINDOWS_DIALOGS=set:1")
                    set(marked TRUE)
                endif()
            endif()
        endforeach()
    endif()
    if(NOT marked)
        list(APPEND unmarked "${testName}")
    endif()
endforeach()
if(testCount LESS 2)
    list(APPEND failures "registration: ctest lists ${testCount} test(s), fewer than this one and one other, so the walk reached nothing")
elseif(unmarked)
    list(APPEND failures "registration: ${testCount} test(s) listed, and these do not set ENDO_SUPPRESS_WINDOWS_DIALOGS: ${unmarked}")
else()
    message(STATUS "windows-dialog-canary: all ${testCount} registered test(s) set ENDO_SUPPRESS_WINDOWS_DIALOGS")
endif()

# "<label>|<canary>|<environment: unset or set>|<the probe's expected line, fault half>|<assert half>"
#
# The product's fault half is not asserted with the variable unset, because it is not this program's to
# decide: SetErrorMode is INHERITED, and ctest runs its children with SEM_NOGPFAULTERRORBOX already set
# (measured: the product canary reported it set before anything of endo's could have set it). That
# inheritance is also why a crash under ctest never showed Windows Error Reporting while the CRT assert
# dialog, which no error mode reaches, held endo-test for 58 minutes.
set(probes
    "test executable, variable unset|${CANARY}|unset|fault-dialog-suppressed=1|assert-report-to-file=1"
    "product, variable unset|${PRODUCT_CANARY}|unset||assert-report-to-file=0"
    "product, variable set|${PRODUCT_CANARY}|set|fault-dialog-suppressed=1|assert-report-to-file=1"
)
foreach(row IN LISTS probes)
    string(REPLACE "|" ";" fields "${row}")
    list(GET fields 0 label)
    list(GET fields 1 program)
    list(GET fields 2 environment)
    list(GET fields 3 faultExpected)
    list(GET fields 4 assertExpected)
    if(environment STREQUAL "set")
        set(envArgument "ENDO_SUPPRESS_WINDOWS_DIALOGS=1")
    else()
        set(envArgument "--unset=ENDO_SUPPRESS_WINDOWS_DIALOGS")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "${envArgument}" "${program}" probe
        TIMEOUT ${BOUND_SECONDS}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(NOT result STREQUAL "0")
        list(APPEND failures "probe (${label}): did not answer (${result}): ${out}${err}")
        continue()
    endif()
    if(NOT faultExpected STREQUAL "" AND NOT out MATCHES "${faultExpected}")
        list(APPEND failures "probe (${label}): expected ${faultExpected}, the program printed: ${out}")
    endif()
    if(out MATCHES "debug-crt=1")
        if(NOT out MATCHES "${assertExpected}")
            list(APPEND failures "probe (${label}): expected ${assertExpected}, the program printed: ${out}")
        endif()
    else()
        list(APPEND notExercised "assert report mode (${label}), not a Debug CRT")
    endif()
    message(STATUS "windows-dialog-canary: probe (${label}): ${out}")
endforeach()
foreach(row IN LISTS modes)
    string(FIND "${row}" "|" bar)
    string(SUBSTRING "${row}" 0 ${bar} mode)
    math(EXPR phraseAt "${bar} + 1")
    string(SUBSTRING "${row}" ${phraseAt} -1 phrase)

    execute_process(
        COMMAND "${CANARY}" "${mode}"
        TIMEOUT ${BOUND_SECONDS}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)

    if(result MATCHES "timeout")
        list(APPEND failures
            "${mode}: did not exit within ${BOUND_SECONDS} s (${result}) -- a process waiting on a dialog nobody will click")
    elseif(NOT result MATCHES "^-?[0-9]+$" AND NOT mode STREQUAL "access-violation")
        list(APPEND failures "${mode}: did not run to an exit status (${result})")
    elseif(NOT result MATCHES "^-?[0-9]+$")
        # CMake names an exception exit rather than numbering it, and a fault is what this mode raises.
        math(EXPR exercised "${exercised} + 1")
        message(STATUS "windows-dialog-canary: ${mode} ended by `${result}` within ${BOUND_SECONDS} s, no dialog")
    elseif(result EQUAL 77)
        list(APPEND notExercised "${mode}")
    elseif(result EQUAL 0)
        list(APPEND failures "${mode}: exited 0, so the failure it exists to raise never happened")
    elseif(NOT phrase STREQUAL "" AND NOT err MATCHES "${phrase}")
        list(APPEND failures
            "${mode}: exited ${result} without printing `${phrase}` on stderr, so the report went somewhere else. stderr: ${err}")
    else()
        math(EXPR exercised "${exercised} + 1")
        message(STATUS "windows-dialog-canary: ${mode} exited ${result} within ${BOUND_SECONDS} s, no dialog")
    endif()
endforeach()

if(failures)
    string(REPLACE ";" "\n  " printable "${failures}")
    message(FATAL_ERROR "windows-dialog-canary:\n  ${printable}")
endif()
if(exercised EQUAL 0)
    message(FATAL_ERROR "windows-dialog-canary: no mode was exercised, so nothing was shown to exit without a dialog.")
endif()
message(STATUS "windows-dialog-canary: ${exercised} failure mode(s) exited without a dialog; not exercised in this build: ${notExercised}")
