# Auto-fetch crispy/vtparser from contour-terminal if missing
set(_need_fetch FALSE)
if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/crispy")
    set(_need_fetch TRUE)
endif()
if(NOT EMSCRIPTEN AND NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vtparser")
    set(_need_fetch TRUE)
endif()

if(_need_fetch)
    find_program(_Python3 NAMES python3 python REQUIRED)
    message(STATUS "Fetching crispy/vtparser via get_contour_dirs.py ...")
    execute_process(
        COMMAND "${_Python3}" "${CMAKE_SOURCE_DIR}/scripts/get_contour_dirs.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE _fetch_result
    )
    if(NOT _fetch_result EQUAL 0)
        message(FATAL_ERROR "Failed to fetch crispy/vtparser (exit ${_fetch_result}). Run scripts/get_contour_dirs.py manually.")
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
    add_subdirectory(http)
    if(ENDO_ENABLE_AGENT)
        add_subdirectory(agent)
    endif()
    add_subdirectory(editor-protocol)
    add_subdirectory(lsp)
    add_subdirectory(dap)
    add_subdirectory(shell)
endif()
