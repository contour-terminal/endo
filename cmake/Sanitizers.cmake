## Adds sanitizer support to a target.

function(enable_sanitizers target)
    if(EMSCRIPTEN)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        return()
    endif()

    option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
    option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
    option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

    set(SANITIZER_FLAGS "")

    if(ENABLE_ASAN)
        list(APPEND SANITIZER_FLAGS "-fsanitize=address" "-fno-omit-frame-pointer")
    endif()

    if(ENABLE_UBSAN)
        list(APPEND SANITIZER_FLAGS "-fsanitize=undefined")
    endif()

    if(ENABLE_TSAN)
        list(APPEND SANITIZER_FLAGS "-fsanitize=thread")
    endif()

    if(SANITIZER_FLAGS)
        # Scoped to CXX: target_compile_options applies to EVERY language of the
        # target, and endo-bin carries an RC source on Windows. clang-cl matches
        # the Clang guard above, so an unscoped -fsanitize=address would reach
        # rc.exe, which treats an unknown switch as a fatal RC1106.
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SANITIZER_FLAGS}>)
        target_link_options(${target} PRIVATE ${SANITIZER_FLAGS})
    endif()
endfunction()
