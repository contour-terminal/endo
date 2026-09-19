# SPDX-License-Identifier: Apache-2.0
#
# Desktop integration on freedesktop platforms, as one concern: the icon theme,
# and a desktop entry that opens endo in a terminal, as other shells ship. An ELF
# executable has nowhere to carry an icon itself; this is where launchers and
# application menus find it.

# The directories the rules below install into that the system already owns --
# filesystem and hicolor-icon-theme do -- relative to the install prefix.
# cmake/Packaging.cmake keeps the RPM from claiming them, or it would conflict
# with those packages on install. Derived from the icon theme on disk, so a new
# size needs no edit here.
set(ENDO_FREEDESKTOP_SYSTEM_DIRS
    ${CMAKE_INSTALL_DATADIR}/applications
    ${CMAKE_INSTALL_DATADIR}/icons
    ${CMAKE_INSTALL_DATADIR}/icons/hicolor)
file(GLOB _endo_icon_sizes RELATIVE "${ENDO_PRODUCT_ICON_THEME_DIR}" "${ENDO_PRODUCT_ICON_THEME_DIR}/*")
foreach(_size IN LISTS _endo_icon_sizes ITEMS scalable)
    list(APPEND ENDO_FREEDESKTOP_SYSTEM_DIRS
        ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}
        ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}/apps)
endforeach()

## @brief Installs the icon theme and a desktop entry for @p target.
##
## The endo.desktop.in template is read from @p target's own source directory, so
## this works from anywhere. A no-op off freedesktop platforms, so call sites need
## no guard of their own.
##
## @param target The executable the desktop entry launches.
function(enable_freedesktop_integration target)
    if(NOT UNIX OR APPLE OR EMSCRIPTEN)
        return()
    endif()

    get_target_property(_source_dir ${target} SOURCE_DIR)
    set(_desktop "${CMAKE_CURRENT_BINARY_DIR}/${ENDO_PRODUCT_NAME}.desktop")
    configure_file("${_source_dir}/endo.desktop.in" "${_desktop}" @ONLY)
    install(FILES "${_desktop}" DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    install(DIRECTORY "${ENDO_PRODUCT_ICON_THEME_DIR}/"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor)
    install(FILES "${ENDO_PRODUCT_ICON_SVG}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME ${ENDO_PRODUCT_NAME}.svg)
endfunction()
