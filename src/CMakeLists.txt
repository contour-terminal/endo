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

add_subdirectory(coro)
add_subdirectory(CoreVM)
add_subdirectory(crispy)
set_target_properties(crispy-core PROPERTIES SYSTEM TRUE)
add_subdirectory(endo-language)
add_subdirectory(endo-test)

if(NOT EMSCRIPTEN)
    add_subdirectory(vtparser)
    set_target_properties(vtparser PROPERTIES SYSTEM TRUE)
    add_subdirectory(platform)
    add_subdirectory(tui)
    add_subdirectory(net)
    add_subdirectory(http)
    if(ENDO_ENABLE_AGENT)
        add_subdirectory(agent)
    endif()
    add_subdirectory(editor-protocol)
    add_subdirectory(lsp)
    add_subdirectory(dap)
    add_subdirectory(shell)
endif()
