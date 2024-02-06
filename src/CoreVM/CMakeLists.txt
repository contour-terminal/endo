option(COREVM_DIRECT_THREADED_VM "CoreVM using direct threaded mode [default: OFF]" OFF)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sysconfig.h.cmake
               ${CMAKE_CURRENT_BINARY_DIR}/sysconfig.h)

add_library(CoreVM STATIC
)

target_sources(CoreVM
    PRIVATE
    Diagnostics.cpp
    LiteralType.cpp
    MatchClass.cpp
    Signature.cpp
    SourceLocation.cpp
    TargetCodeGenerator.cpp
    NativeCallback.cpp
    ir/BasicBlock.cpp
    ir/Constant.cpp
    ir/ConstantArray.cpp
    ir/ConstantValue.cpp
    ir/IRBuilder.cpp
    ir/IRHandler.cpp
    ir/IRProgram.cpp
    ir/Instr.cpp
    ir/InstructionVisitor.cpp
    ir/Instructions.cpp
    ir/PassManager.cpp
    ir/Value.cpp
    transform/EmptyBlockElimination.cpp
    transform/InstructionElimination.cpp
    transform/MergeBlockPass.cpp
    transform/UnusedBlockPass.cpp
    util/Cidr.cpp
    util/RegExp.cpp
    vm/ConstantPool.cpp
    vm/Handler.cpp
    vm/Instruction.cpp
    vm/Match.cpp
    vm/Program.cpp
    vm/Runner.cpp
    vm/Runtime.cpp
    PUBLIC
      FILE_SET CXX_MODULES FILES
        CoreVM.cppm
        util.cppm
        enums.cppm
  )

target_compile_definitions(CoreVM PUBLIC
    $<$<BOOL:${COREVM_DIRECT_THREADED_VM}>:COREVM_DIRECT_THREADED_VM>
)

target_compile_options(CoreVM PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-Wno-gnu-label-as-value>
    $<$<CXX_COMPILER_ID:GCC>:-Wno-gnu-label-as-value>
)

target_include_directories(CoreVM PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(CoreVM PUBLIC fmt::fmt-header-only range-v3::range-v3 crispy::core)
