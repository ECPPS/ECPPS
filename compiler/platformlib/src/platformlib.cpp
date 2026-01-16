#include <platformlib.h>

#include "../../utilitieslib/include/utilitieslib/Assert.h"
#ifdef _WIN32
#include <Windows.h>

#include <DbgHelp.h>
struct ContextWrapper : ecpps::platformlib::DebuggerContext
{
     CONTEXT value;
};
template <> struct ecpps::platformlib::PointerInterconvertibility<ecpps::platformlib::DebuggerContext, CONTEXT>
{
     constexpr static bool IsValid = true;
};
#elifdef __linux__

#endif

#include <cstdint>
#include <vector>

ecpps::platformlib::DebuggerContext& ecpps::platformlib::debugger::New(void)
{
#ifdef _WIN32
     CONTEXT* lpContext = new CONTEXT();
     return reinterpret_cast<DebuggerContext&>(*reinterpret_cast<ContextWrapper*>(lpContext));
#elifdef __linux__
     throw nullptr;
#endif
}

std::size_t ecpps::platformlib::debugger::GetRegisterValue(DebuggerContext& context, const std::string& name)
{
#ifdef _WIN32
     CONTEXT& ctx = context.As<CONTEXT>();

     if (name == "rip") return ctx.Rip;
     if (name == "rsp") return ctx.Rsp;
     if (name == "rbp") return ctx.Rbp;
     if (name == "rax") return ctx.Rax;
     if (name == "rbx") return ctx.Rbx;
     if (name == "rcx") return ctx.Rcx;
     if (name == "rdx") return ctx.Rdx;
     if (name == "rsi") return ctx.Rsi;
     if (name == "rdi") return ctx.Rdi;
     if (name == "r8") return ctx.R8;
     if (name == "r9") return ctx.R9;
     if (name == "r10") return ctx.R10;
     if (name == "r11") return ctx.R11;
     if (name == "r12") return ctx.R12;
     if (name == "r13") return ctx.R13;
     if (name == "r14") return ctx.R14;
     if (name == "r15") return ctx.R15;
#elifndef __linux__
#endif

     return SIZE_MAX;
}
std::vector<void*> ecpps::platformlib::debugger::WalkTrace(DebuggerContext* defaultContext)
{
#ifdef _WIN32
     HANDLE hProcess = GetCurrentProcess();
     HANDLE hThread = GetCurrentThread();

     SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
     SymInitialize(hProcess, nullptr, TRUE);

     std::vector<void*> stack;
     CONTEXT* context{};
     if (defaultContext != nullptr) context = &defaultContext->As<CONTEXT>();
     else
     {
          context = new CONTEXT();
          RtlCaptureContext(context);
     }

     STACKFRAME64 frame{};
     frame.AddrPC.Offset = context->Rip;
     frame.AddrFrame.Offset = context->Rbp;
     frame.AddrStack.Offset = context->Rsp;

     frame.AddrPC.Mode = AddrModeFlat;
     frame.AddrFrame.Mode = AddrModeFlat;
     frame.AddrStack.Mode = AddrModeFlat;

     while (StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, &frame, &context, nullptr,
                        SymFunctionTableAccess64, SymGetModuleBase64, nullptr) != 0)
     {
          stack.push_back(reinterpret_cast<void*>(frame.AddrPC.Offset));
     }
     return stack;
#elifdef __linux__
     return {};
#endif
}
