if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/crispy)
    message(FATAL_ERROR "src/crispy directory found. Symlink it to this location from a clone of Contour Terminal")
endif()

if (NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vtparser)
    message(FATAL_ERROR "src/vtparser directory found. Symlink it to this location from a clone of Contour Terminal")
endif()

add_subdirectory(CoreVM)
add_subdirectory(crispy)
add_subdirectory(vtparser)
add_subdirectory(InputEditor)
add_subdirectory(shell)
