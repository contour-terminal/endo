module;

#include <crispy/logstore.h>

#include <fmt/core.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <sys/wait.h>

#include <unistd.h>

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/ExecutorProcessControl.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Utils.h>

using std::unique_ptr;

using namespace llvm;
using namespace llvm::orc;

export module LLVMBackend;

auto inline llvmLog = logstore::category("llvm", "Debug log", logstore::category::state::Enabled);

class TestJIT
{
  private:
    std::unique_ptr<ExecutionSession> ES;

    DataLayout DL;
    MangleAndInterner Mangle;

    RTDyldObjectLinkingLayer ObjectLayer;
    IRCompileLayer CompileLayer;

    JITDylib& MainJD;

  public:
    TestJIT(std::unique_ptr<ExecutionSession> ES, JITTargetMachineBuilder JTMB, DataLayout DL):
        ES(std::move(ES)),
        DL(std::move(DL)),
        Mangle(*this->ES, this->DL),
        ObjectLayer(*this->ES, []() { return std::make_unique<SectionMemoryManager>(); }),
        CompileLayer(*this->ES, ObjectLayer, std::make_unique<ConcurrentIRCompiler>(std::move(JTMB))),
        MainJD(this->ES->createBareJITDylib("<main>"))
    {
        MainJD.addGenerator(
            cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(DL.getGlobalPrefix())));
        if (JTMB.getTargetTriple().isOSBinFormatCOFF())
        {
            ObjectLayer.setOverrideObjectFlagsWithResponsibilityFlags(true);
            ObjectLayer.setAutoClaimResponsibilityForObjectSymbols(true);
        }
    }

    ~TestJIT()
    {
        if (auto Err = ES->endSession())
            ES->reportError(std::move(Err));
    }

    static Expected<std::unique_ptr<TestJIT>> Create()
    {
        auto EPC = SelfExecutorProcessControl::Create();
        if (!EPC)
            return EPC.takeError();

        auto ES = std::make_unique<ExecutionSession>(std::move(*EPC));

        JITTargetMachineBuilder JTMB(ES->getExecutorProcessControl().getTargetTriple());

        auto DL = JTMB.getDefaultDataLayoutForTarget();
        if (!DL)
            return DL.takeError();

        return std::make_unique<TestJIT>(std::move(ES), std::move(JTMB), std::move(*DL));
    }

    const DataLayout& getDataLayout() const { return DL; }

    JITDylib& getMainJITDylib() { return MainJD; }

    Error addModule(ThreadSafeModule TSM, ResourceTrackerSP RT = nullptr)
    {
        if (!RT)
            RT = MainJD.getDefaultResourceTracker();
        return CompileLayer.add(RT, std::move(TSM));
    }

    Expected<ExecutorSymbolDef> lookup(StringRef Name) { return ES->lookup({ &MainJD }, Mangle(Name.str())); }
};

std::vector<std::string> tokenizer(const std::string& p_pcstStr, char delim)
{
    std::vector<std::string> tokens;
    std::stringstream mySstream(p_pcstStr);
    std::string temp;

    while (getline(mySstream, temp, delim))
    {
        tokens.push_back(temp);
    }

    return tokens;
}

extern "C" void execute(const char* prog, const char* params, int stdinFd, int stdoutFd)
{
    std::vector<const char*> argv;
    argv.push_back(prog);
    if (strlen(params) > 0)
        argv.push_back(params);
    argv.push_back(nullptr);

    fmt::print("program to execute {} with args: {} \n ", prog, params);

    pid_t const pid = fork();

    switch (pid)
    {
        case -1: llvmLog()("Failed to fork(): {} \n", strerror(errno)); return;
        case 0: {
            // child process
            if (stdinFd != STDIN_FILENO)
                dup2(stdinFd, STDIN_FILENO);
            if (stdoutFd != STDOUT_FILENO)
                dup2(stdoutFd, STDOUT_FILENO);
            execvp(prog, const_cast<char* const*>(argv.data()));
        }
        default: {
            // parent process
            int wstatus = 0;
            waitpid(pid, &wstatus, 0);
            if (WIFSIGNALED(wstatus))
                llvmLog()("child process exited with signal {} \n", WTERMSIG(wstatus));
            else if (WIFEXITED(wstatus))
                llvmLog()("child process exited with code {} \n", WEXITSTATUS(wstatus));
            else if (WIFSTOPPED(wstatus))
                llvmLog()("child process stopped with signal {} \n", WSTOPSIG(wstatus));
            else
                llvmLog()("child process exited with unknown status {} \n", wstatus);
            break;
        }
    }
}

std::tuple<std::string, std::string> SeparateProg(const std::string input)
{

    std::stringstream inStream(input);
    std::string program;
    std::string args;
    getline(inStream, program, ' ');
    getline(inStream, args, '\n');

    return { "/usr/bin/" + program, args };
}

export class LLVMBackend
{

  public:
    LLVMBackend()
    {
        InitializeNativeTarget();
        InitializeNativeTargetAsmPrinter();
        InitializeNativeTargetAsmParser();
        // Create an LLJIT instance.
        _jit = ExitOnErr(TestJIT::Create());
    }

    void initializeModule()
    {
        _context = std::make_unique<LLVMContext>();
        _builder = std::make_unique<IRBuilder<>>(*_context);

        _module = std::make_unique<Module>("test", *_context);
        _module->setDataLayout(_jit->getDataLayout());
    }

    void HandleExternalCall(std::string const& input, const int stdinFd, const int stdoutFd)
    {

        auto byteptr = _builder->getPtrTy();
        auto inttype = _builder->getInt64Ty();
        // llvm::Type::getInt8Ty(*Context)->getPo;
        _execvpFunctionType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*_context), { byteptr, byteptr, inttype, inttype }, false);

        _mainFunctionType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*_context), { llvm::Type::getVoidTy(*_context) }, false);

        auto fun = _module->getOrInsertFunction("execute", _execvpFunctionType);

        Function* F =
            Function::Create(_mainFunctionType, Function::ExternalLinkage, "__anon_expr", _module.get());

        // Add a basic block to the function. As before, it automatically inserts
        // because of the last argument.
        BasicBlock* BB = BasicBlock::Create(*_context, "EntryBlock", F);

        _builder->SetInsertPoint(BB);

        // Get pointers to the constant `1'.

        const auto [prog, args] = SeparateProg(input);

        auto CalRes = _builder->CreateCall(fun,
                                           { _builder->CreateGlobalString(prog),
                                             _builder->CreateGlobalString(args),
                                             _builder->getInt64(stdinFd),
                                             _builder->getInt64(stdoutFd) });

        _builder->CreateRet(_builder->getInt32(1));
    }

    auto exec(std::string const& input, const int stdinFd, const int stdoutFd)
    {
        initializeModule();

        HandleExternalCall(input, stdinFd, stdoutFd);

        auto RT = _jit->getMainJITDylib().createResourceTracker();

        auto M = ThreadSafeModule(std::move(_module), std::move(_context));

        M.getModuleUnlocked()->print(llvm::outs(), nullptr);

        ExitOnErr(_jit->addModule(std::move(M), RT));
        // Look up the JIT'd function, cast it to a function pointer, then call it.
        auto ExprSymbol = ExitOnErr(_jit->lookup("__anon_expr"));
        (ExprSymbol.getAddress().toPtr<void (*)()>())();

        // Delete the anonymous expression module from the JIT.
        ExitOnErr(RT->remove());
    }

    std::unique_ptr<LLVMContext> _context;
    std::unique_ptr<IRBuilder<>> _builder;
    std::unique_ptr<TestJIT> _jit;
    std::unique_ptr<Module> _module;
    ExitOnError ExitOnErr;

    // types of some functions
    llvm::FunctionType* _execvpFunctionType;
    llvm::FunctionType* _mainFunctionType;

    ~LLVMBackend() { llvm::llvm_shutdown(); }
};
