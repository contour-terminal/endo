# src/{crispy,coro,net,vtparser} were fetched from contour into this directory and gitignored, and
# src/tui was endo's own: core-cpp provides all of it now. A working tree from before the move can
# still hold them -- an ignored directory survives the checkout -- and building beside a stale copy
# is exactly the drift the move ended, so refuse one by name.
foreach(_stale IN ITEMS crispy coro net vtparser tui)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_stale}")
        message(FATAL_ERROR
            "src/${_stale} is a stale copy from before endo moved onto core-cpp; delete it. "
            "Its code comes from core-cpp now (cmake/EndoThirdParties.cmake).")
    endif()
endforeach()

add_subdirectory(CoreVM)
add_subdirectory(endo-language)
add_subdirectory(endo-test)

if(NOT EMSCRIPTEN)
    add_subdirectory(platform)
    add_subdirectory(http)
    if(ENDO_ENABLE_AGENT)
        add_subdirectory(agent)
    endif()
    add_subdirectory(editor-protocol)
    add_subdirectory(lsp)
    add_subdirectory(dap)
    add_subdirectory(shell)
endif()
