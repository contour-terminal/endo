
add_library(InputEditor STATIC
    InputEditor.cpp
    InputEditor.h
)
target_link_libraries(InputEditor PUBLIC fmt::fmt-header-only vtparser crispy::core boxed-cpp)
target_include_directories(InputEditor PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/include>
)
