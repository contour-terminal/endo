add_library(Shell STATIC
    AST.h
    Visitor.h
)


target_sources(Shell
  PUBLIC
    FILE_SET CXX_MODULES FILES
      Lexer.cpp
      UnixPipe.cpp
      TTY.cpp
      Shell.cpp
      Prompt.cpp
      ASTPrinter.cpp
      IRGenerator.cpp
      Parser.cpp
      llvm_executor.cpp
)

find_package(Threads REQUIRED)
# util for pty.h

# see https://github.com/llvm/llvm-project/issues/75108
# and https://github.com/ericniebler/range-v3/blob/f013aef2ae81f3661a560e7922a968665bedebff/include/meta/meta_fwd.hpp#L37
target_compile_definitions(Shell PUBLIC META_WORKAROUND_LLVM_28385)
set(shell_libs
  fmt::fmt-header-only
  vtparser
  CoreVM
  crispy::core
  InputEditor
  Threads::Threads
  util)


if(ENDO_USE_LLVM)
  find_package(LLVM)
  if(LLVM_FOUND)

    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    include("${LLVM_DIR}/AddLLVM.cmake")

    include_directories(${LLVM_INCLUDE_DIRS})
    add_definitions(${LLVM_DEFINITIONS})
    llvm_map_components_to_libnames(llvm_libs analysis core executionengine instcombine object orcjit runtimedyld scalaropts support native)
    # Link against LLVM libraries
    set(shell_libs
      ${shell_libs}
      ${llvm_libs})

    target_compile_definitions(Shell PUBLIC ENDO_USE_LLVM)
  endif()
else()
endif()


if(ENDO_TRACE_VM)
  target_compile_definitions(Shell PUBLIC ENDO_TRACE_VM)
endif()
if(ENDO_TRACE_PARSER)
  target_compile_definitions(Shell PUBLIC ENDO_TRACE_PARSER)
endif()
if(ENDO_TRACE_LEXER)
  target_compile_definitions(Shell PUBLIC ENDO_TRACE_LEXER)
endif()

target_link_libraries(Shell PUBLIC ${shell_libs})

add_executable(endo
    main.cpp
)
target_link_libraries(endo PUBLIC Shell)

add_executable(test-endo
    test_main.cpp
    Lexer_test.cpp
    Shell_test.cpp
)
target_link_libraries(test-endo Shell Catch2::Catch2)

add_test(NAME test-endo COMMAND test-endo)
