# SPDX-License-Identifier: Apache-2.0
#
# No executable ctest can start may stop at a Windows dialog.
#
# A CRT assert, abort() or crash in a Windows Debug build opens a modal dialog and WAITS. Under ctest
# nobody is there to click it: endo-test tripped an assert, and the run sat for 58 minutes until the
# owner dismissed the dialog by hand. testing::suppressWindowsDialogs() existed, and every Catch2
# test_main.cpp called it -- endo-test's main() did not. A call each main() has to remember is the
# defect, so this module does not ask for one.
#
# ## The mechanism
#
# `endo_install_windows_dialog_suppression()` runs DEFERRED, after every directory has declared its
# targets and tests, and:
#
# 1. links src/testing/SuppressWindowsDialogsAtStartup.cpp into EVERY executable target the build
#    declares -- derived by walking the directories, never listed, so a test executable added tomorrow
#    is covered without an edit, including the contour-fetched ones endo does not own;
# 2. sets ENDO_SUPPRESS_WINDOWS_DIALOGS in the environment of EVERY test ctest registers, which reaches
#    any process a test starts, including the product binary a Python script launches.
#
# A PRODUCT executable, one a user runs, links the variant that suppresses only when that variable is
# present: an interactive user keeps the CRT's dialogs and Windows Error Reporting exactly as before,
# which is the behaviour a debugger attaches through, and only a process started under ctest reports to
# stderr and exits. Every other executable -- tests, tools, canaries -- suppresses unconditionally.
#
# The table below is the only list, and a row naming no executable target is refused as stale.
#
# ## What proves it
#
# `ctest -R windows-dialog-canary` runs a program that asserts, aborts, passes an invalid parameter and
# faults, one per run, and requires each to EXIT non-zero within a bound. With this module's link step
# removed it hangs on the first dialog instead, which is how it was watched failing.

# Executables a user runs, as "<target>|<why it is a product>". Read by the function below when it
# runs, deferred -- so a row a configuration adds only where its target exists (the product canary, on
# Windows with testing on) is appended where that target is declared. The shell's row carries the
# condition src/CMakeLists.txt adds the shell under: a WebAssembly build declares no endo-bin.
set(EndoWindowsDialogProductExecutables "")
if(NOT EMSCRIPTEN)
    list(APPEND EndoWindowsDialogProductExecutables
        "endo-bin|the shell users run, which must keep its assert dialog and crash reporting when started by hand")
endif()

## @brief Every directory at and below @p dir, into @p out.
function(endo_windows_dialogs_directories dir out)
    set(found "${dir}")
    get_property(children DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        endo_windows_dialogs_directories("${child}" childFound)
        list(APPEND found ${childFound})
    endforeach()
    set(${out} "${found}" PARENT_SCOPE)
endfunction()

## @brief Links the startup suppression into every executable and marks every test's environment.
##
## Called deferred from the top-level CMakeLists.txt, so it sees every target and test.
function(endo_install_windows_dialog_suppression)
    set(source "${CMAKE_SOURCE_DIR}/src/testing/SuppressWindowsDialogsAtStartup.cpp")
    add_library(endo-windows-dialogs-always OBJECT "${source}")
    target_compile_definitions(endo-windows-dialogs-always PRIVATE ENDO_WINDOWS_DIALOGS_ALWAYS)
    add_library(endo-windows-dialogs-under-test OBJECT "${source}")
    foreach(variant IN ITEMS endo-windows-dialogs-always endo-windows-dialogs-under-test)
        target_include_directories(${variant} PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_features(${variant} PRIVATE cxx_std_23)
    endforeach()

    set(productNames "")
    foreach(row IN LISTS EndoWindowsDialogProductExecutables)
        string(FIND "${row}" "|" bar)
        if(bar LESS 1)
            message(FATAL_ERROR
                "WindowsDialogs: the product row `${row}` is not `<target>|<why it is a product>`. "
                "A product keeps its dialogs for a user, so the reason is what a reader checks that "
                "claim against.")
        endif()
        string(SUBSTRING "${row}" 0 ${bar} productName)
        list(APPEND productNames "${productName}")
    endforeach()

    endo_windows_dialogs_directories("${CMAKE_SOURCE_DIR}" directories)
    set(executables "")
    set(tests "")
    foreach(dir IN LISTS directories)
        get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            get_target_property(type ${target} TYPE)
            if(type STREQUAL "EXECUTABLE")
                list(APPEND executables "${target}")
            endif()
        endforeach()
        get_property(dirTests DIRECTORY "${dir}" PROPERTY TESTS)
        foreach(test IN LISTS dirTests)
            set_property(TEST "${test}" DIRECTORY "${dir}"
                APPEND PROPERTY ENVIRONMENT_MODIFICATION "ENDO_SUPPRESS_WINDOWS_DIALOGS=set:1")
            list(APPEND tests "${test}")
        endforeach()
    endforeach()

    # A walk that found nothing is this function having stopped working, never a build with nothing to
    # protect: every configuration declares the shell.
    if(NOT executables)
        message(FATAL_ERROR
            "WindowsDialogs: walking the build's directories found no executable target, so the dialog "
            "suppression reached nothing. That is the walk broken, not a build without executables.")
    endif()
    if(ENDO_TESTING AND NOT tests)
        message(FATAL_ERROR
            "WindowsDialogs: ENDO_TESTING is on and walking the build's directories found no test, so "
            "no test's environment was marked. That is the walk broken, not a build without tests.")
    endif()

    set(products 0)
    foreach(target IN LISTS executables)
        if(target IN_LIST productNames)
            target_sources(${target} PRIVATE $<TARGET_OBJECTS:endo-windows-dialogs-under-test>)
            math(EXPR products "${products} + 1")
        else()
            target_sources(${target} PRIVATE $<TARGET_OBJECTS:endo-windows-dialogs-always>)
        endif()
    endforeach()
    foreach(productName IN LISTS productNames)
        if(NOT productName IN_LIST executables)
            message(FATAL_ERROR
                "WindowsDialogs: the product row `${productName}` names no executable target of this "
                "build. A stale row exempts nothing and hides that the list stopped describing the "
                "tree; remove it or correct the name.")
        endif()
    endforeach()

    list(LENGTH executables executableCount)
    list(LENGTH tests testCount)
    message(STATUS
        "Windows dialogs: suppressed at startup in ${executableCount} executable(s), ${products} of them "
        "products that suppress only under test; ENDO_SUPPRESS_WINDOWS_DIALOGS set on ${testCount} test(s)")
endfunction()
