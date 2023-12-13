
add_library(InputEditor STATIC)

target_sources(InputEditor
    PRIVATE
        InputEditor.cpp
    PUBLIC
      FILE_SET CXX_MODULES FILES
        InputEditor.cppm
    )

target_link_libraries(InputEditor PUBLIC fmt::fmt-header-only vtparser crispy::core boxed-cpp)
target_include_directories(InputEditor PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/include>
)
