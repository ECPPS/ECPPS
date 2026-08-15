#include "Diagnostics.h"
#include <array>
#include <print>
#include <stacktrace>

std::unordered_map<std::string, ecpps::Diagnostics*> ecpps::g_diagnosticsReferences{};

#ifdef _WIN32
#include <dbghelp.h>
#include <windows.h>

#pragma comment(lib, "dbghelp.lib")

#elifdef __linux__
#include <execinfo.h>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#endif

struct NativeStackFrame
{
     std::uintptr_t instruction{};
     std::uintptr_t moduleBase{};
     std::string module;
     std::string function;
     std::string sourceFile;
     std::uint32_t sourceLine{};
     std::uint64_t displacement{};
};

using NativeStackTrace = std::vector<NativeStackFrame>;

[[noreturn]] static void TerminatePostICE(void)
{
     std::fflush(stderr);
     std::exit(EXIT_FAILURE);
}
static void ReportICE(std::string_view message, const std::stacktrace& trace)
{
     std::println("\x1b[41mInternal Compiler Error:\x1b[0m {}", message);

     for (const auto& frame : trace) std::println("  {}", frame);

     std::print("\x1b[0m");
}
[[maybe_unused]] static void ReportICE(std::string_view message, const NativeStackTrace& trace)
{
     std::println("\x1b[41mInternal Compiler Error:\x1b[0m {}", message);

     for (const auto& frame : trace)
     {
          std::print("  0x{:016x}", frame.instruction);

          if (!frame.function.empty()) { std::print(" {}+0x{:x}", frame.function, frame.displacement); }
          if (!frame.sourceFile.empty()) { std::print(" ({}:{})", frame.sourceFile, frame.sourceLine); }
          if (!frame.module.empty()) { std::print(" [{}]", frame.module); }

          std::println("");
     }

     std::print("\x1b[0m");
}

#ifdef _WIN32

static HANDLE GetCurrentProcessHandle(void) noexcept { return ::GetCurrentProcess(); }

static bool InitialiseSymbols(void)
{
     static const bool isInitialised = []
     {
          const HANDLE process = GetCurrentProcessHandle();

          ::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS |
                          SYMOPT_NO_PROMPTS);

          return ::SymInitialize(process, nullptr, TRUE) != FALSE;
     }();

     return isInitialised;
}

constexpr DWORD GetMachineType(void) noexcept
{
#if defined(_M_X64) || defined(__x86_64__)
     return IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86)
     return IMAGE_FILE_MACHINE_I386;
#elif defined(_M_ARM64)
     return IMAGE_FILE_MACHINE_ARM64;
#else
#error Unsupported Windows architecture
#endif
}

static void InitialiseStackFrame(STACKFRAME64& frame, const CONTEXT& context) noexcept
{
     frame = {};

#if defined(_M_X64) || defined(__x86_64__)

     frame.AddrPC.Offset = context.Rip;
     frame.AddrPC.Mode = AddrModeFlat;
     frame.AddrStack.Offset = context.Rsp;
     frame.AddrStack.Mode = AddrModeFlat;
     frame.AddrFrame.Offset = context.Rbp;
     frame.AddrFrame.Mode = AddrModeFlat;

#elif defined(_M_IX86)

     frame.AddrPC.Offset = context.Eip;
     frame.AddrPC.Mode = AddrModeFlat;
     frame.AddrStack.Offset = context.Esp;
     frame.AddrStack.Mode = AddrModeFlat;
     frame.AddrFrame.Offset = context.Ebp;
     frame.AddrFrame.Mode = AddrModeFlat;

#elif defined(_M_ARM64)

     frame.AddrPC.Offset = context.Pc;
     frame.AddrPC.Mode = AddrModeFlat;
     frame.AddrStack.Offset = context.Sp;
     frame.AddrStack.Mode = AddrModeFlat;
     frame.AddrFrame.Offset = context.Fp;
     frame.AddrFrame.Mode = AddrModeFlat;

#endif
}

static NativeStackFrame ResolveSymbol(HANDLE process, DWORD64 address)
{
     NativeStackFrame frame{};

     frame.instruction = static_cast<std::uintptr_t>(address);

     std::array<std::byte, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> storage{};

     auto* symbol = reinterpret_cast<PSYMBOL_INFO>(storage.data());

     symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

     symbol->MaxNameLen = MAX_SYM_NAME;

     DWORD64 displacement = 0;

     if (::SymFromAddr(process, address, &displacement, symbol))
     {
          frame.function = symbol->Name;
          frame.displacement = displacement;
          frame.moduleBase = static_cast<std::uintptr_t>(symbol->ModBase);
     }

     IMAGEHLP_LINE64 line{};
     line.SizeOfStruct = sizeof(line);

     DWORD lineDisplacement = 0;

     if (::SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
     {
          if (line.FileName) frame.sourceFile = line.FileName;

          frame.sourceLine = line.LineNumber;
     }

     IMAGEHLP_MODULE64 module{};
     module.SizeOfStruct = sizeof(module);

     if (::SymGetModuleInfo64(process, address, &module))
     {
          frame.module = module.ImageName;

          if (frame.moduleBase == 0) frame.moduleBase = static_cast<std::uintptr_t>(module.BaseOfImage);
     }

     return frame;
}

static NativeStackTrace WalkContext(CONTEXT context)
{
     NativeStackTrace trace{};
     trace.reserve(64);

     if (!InitialiseSymbols()) return trace;

     const HANDLE process = GetCurrentProcessHandle();

     STACKFRAME64 frame{};
     InitialiseStackFrame(frame, context);

     constexpr std::size_t MaximumFrames = 128;

     for (std::size_t i = 0; i < MaximumFrames; i++)
     {
          const DWORD64 address = frame.AddrPC.Offset;

          if (address == 0) break;

          trace.emplace_back(ResolveSymbol(process, address));

          if (!::StackWalk64(GetMachineType(), process, ::GetCurrentThread(), &frame, &context, nullptr,
                             ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr))
               break;
     }

     return trace;
}

[[noreturn]] void ecpps::IssueICE(std::string_view message, void* implementationDefined)
{
     const auto* context = static_cast<const CONTEXT*>(implementationDefined);

     NativeStackTrace trace;

     if (context) trace = WalkContext(*context);

     ReportICE(message, trace);

     TerminatePostICE();
}

static void IssueDiagnostics(void)
{
     for (const auto& [sourceName, lpDiagnostics] : ecpps::g_diagnosticsReferences)
     {
          const auto& diagnostics = *lpDiagnostics;

          for (const auto& diag : diagnostics.diagnosticsList) ecpps::diagnostics::PrintDiagnostic(sourceName, diag);
     }
}

static LONG WINAPI WinExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
{
     IssueDiagnostics();

     switch (exceptionInfo->ExceptionRecord->ExceptionCode)
     {
     case EXCEPTION_ACCESS_VIOLATION:
     {
          void* faultingAddress = reinterpret_cast<void*>(exceptionInfo->ExceptionRecord->ExceptionInformation[1]);
          bool isWrite = exceptionInfo->ExceptionRecord->ExceptionInformation[0] == 1;
          std::string built;
          if (isWrite) built = std::format("Access violation writing to {}", faultingAddress);
          else
               built = std::format("Access violation reading {}", faultingAddress);

          ecpps::IssueICE(built, exceptionInfo->ContextRecord);
     }
     case EXCEPTION_INT_DIVIDE_BY_ZERO: ecpps::IssueICE("Divide by zero", exceptionInfo->ContextRecord);
     default:
          ecpps::IssueICE(
              std::format("Unhandled exception has occurred: {}", exceptionInfo->ExceptionRecord->ExceptionCode),
              exceptionInfo->ContextRecord);
     }

     return EXCEPTION_EXECUTE_HANDLER;
}

#elifdef __linux__ // ^^^ _WIN32 / __linux__ vvv

static void LinuxFatalSignalHandler([[maybe_unused]] int signalNumber, siginfo_t* action,
                                    [[maybe_unused]] void* oldAction) noexcept
{
     ecpps::IssueICE("Unhandled Linux signal", action);
}

[[noreturn]] void ecpps::IssueICE(std::string_view message, [[maybe_unused]] void* implementationDefined)
{
     ReportICE(message, std::stacktrace::current());
     TerminatePostICE();
}

#endif // ^^^ __linux__

ecpps::TracedException::TracedException(std::string message)
    : _message(std::move(message)), _trace(std::stacktrace::current(1))
{
}
ecpps::TracedException::TracedException(std::string message, const std::exception_ptr& inner)
    : _message(std::move(message)), _trace(std::stacktrace::current(1)), _inner(inner)
{
}
[[noreturn]] void ecpps::IssueICE(const TracedException& exception)
{
     ReportICE(exception.what(), exception.Trace());
     TerminatePostICE();
}

[[noreturn]] void ecpps::IssueICE(std::string_view message) { IssueICE(message, std::stacktrace::current(1)); }

[[noreturn]] void ecpps::IssueICE(std::string_view message, const std::stacktrace& trace)
{
     ReportICE(message, trace);
     TerminatePostICE();
}
#undef sa_sigaction
void ecpps::RegisterErrorCallbacks(void)
{
#ifdef _WIN32
     SetUnhandledExceptionFilter(WinExceptionHandler);
#elifdef __linux__
     struct sigaction action{}; // I hate that sigaction refers to both a function and a class...
     action.__sigaction_handler.sa_sigaction = &LinuxFatalSignalHandler;
     ::sigemptyset(&action.sa_mask);

     action.sa_flags = static_cast<int>(SA_SIGINFO | SA_RESETHAND);

     constexpr auto signals = std::to_array({SIGSEGV, SIGILL, SIGFPE, SIGABRT, SIGTRAP});

     for (const int signal : signals) ::sigaction(signal, &action, nullptr);
#endif
}
