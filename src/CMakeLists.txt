# Auto-fetch the vendored contour sources if missing. Which directories those are
# is declared in scripts/contour-pin.json and read back here, so the pin file stays
# the single source of truth rather than being duplicated in this list.
find_program(_Python3 NAMES python3 python REQUIRED)
execute_process(
    COMMAND "${_Python3}" "${CMAKE_SOURCE_DIR}/scripts/get_contour_dirs.py" --print-directories
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _contour_dirs
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _list_result
)
if(NOT _list_result EQUAL 0)
    message(FATAL_ERROR "Failed to read scripts/contour-pin.json (exit ${_list_result}).")
endif()

set(_need_fetch FALSE)
foreach(_contour_dir IN LISTS _contour_dirs)
    # vtparser is not built under Emscripten, so its absence must not force a fetch.
    if(EMSCRIPTEN AND _contour_dir STREQUAL "vtparser")
        continue()
    endif()
    if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_contour_dir}")
        set(_need_fetch TRUE)
    endif()
endforeach()

# coro and net used to be first-party endo sources. A working tree from before
# they were vendored still has them, and they are gitignored now, so the check
# above would happily build against the stale copies. EventLoop.hpp exists only
# upstream, so its absence identifies such a tree and forces a refetch.
if(NOT EMSCRIPTEN
   AND IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/net"
   AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/net/EventLoop.hpp")
    message(STATUS "Stale pre-vendoring src/net detected; refetching.")
    set(_need_fetch TRUE)
endif()

if(_need_fetch)
    message(STATUS "Fetching vendored contour sources via get_contour_dirs.py ...")
    execute_process(
        COMMAND "${_Python3}" "${CMAKE_SOURCE_DIR}/scripts/get_contour_dirs.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE _fetch_result
    )
    if(NOT _fetch_result EQUAL 0)
        message(FATAL_ERROR "Failed to fetch vendored contour sources (exit ${_fetch_result}). Run scripts/get_contour_dirs.py manually.")
    endif()
endif()

add_subdirectory(CoreVM)
add_subdirectory(crispy)
set_target_properties(crispy-core PROPERTIES SYSTEM TRUE)
add_subdirectory(endo-language)
add_subdirectory(endo-test)

if(NOT EMSCRIPTEN)
    add_subdirectory(vtparser)
    set_target_properties(vtparser PROPERTIES SYSTEM TRUE)
    add_subdirectory(platform)
    # Only net and tui link coro, and both are native-only. Configuring it under
    # Emscripten would buy nothing and cost the build: coro fails configure
    # outright when the standard library has no <stop_token>, which is exactly
    # what Emscripten's libc++ reports.
    add_subdirectory(coro)
    set_target_properties(coro PROPERTIES SYSTEM TRUE)
    # net sits below tui now: it drives its own net::EventLoop reactor rather than
    # borrowing the TUI runtime's, so it depends on coro alone.
    add_subdirectory(net)
    set_target_properties(net PROPERTIES SYSTEM TRUE)
    add_subdirectory(tui)
    add_subdirectory(http)
    if(ENDO_ENABLE_AGENT)
        add_subdirectory(agent)
    endif()
    add_subdirectory(editor-protocol)
    add_subdirectory(lsp)
    add_subdirectory(dap)
    add_subdirectory(shell)
endif()
