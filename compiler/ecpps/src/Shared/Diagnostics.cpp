#include "Diagnostics.h"
#ifdef _WIN32
#include <dbghelp.h>
#endif

#include <print>

void ecpps::IssueICE(const TracedException& ex)
{
#ifdef _WIN32
     HANDLE hProcess = GetCurrentProcess();
     SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
     SymInitialize(hProcess, nullptr, TRUE);

     void* storage = operator new(sizeof(SYMBOL_INFO) + MAX_PATH);
     std::println("\x1b[41mInternal Compiler Error:\x1b[0m {}", ex.what());
     for (auto* i : ex.trace)
     {
          auto address = reinterpret_cast<DWORD64>(i);
          DWORD64 displacement = 0;
          std::memset(storage, 0, sizeof(SYMBOL_INFO) + MAX_PATH);
          auto* symbol = new (storage) SYMBOL_INFO{};
          symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
          symbol->MaxNameLen = MAX_PATH;

          if (SymFromAddr(hProcess, address, &displacement, symbol) != 0)
          {
               IMAGEHLP_LINE64 line{};
               line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
               DWORD lineDisp = 0;
               if (SymGetLineFromAddr64(hProcess, address, &lineDisp, &line) != 0)
                    std::println("  at \x1b[34m{}:{}\x1b[0m <\x1b[32m{}\x1b[0m>", line.FileName, line.LineNumber,
                                 symbol->Name);
               else
                    std::println("  at \x1b[32m{}\x1b[0m @{:x}", symbol->Name, address);
          }
     }

     operator delete(storage);

     SymCleanup(hProcess);
     ExitProcess(-1);
#elifdef __linux__
     __fastfail(-1);
#endif
}

void ecpps::IssueICE(std::string_view message, platformlib::DebuggerContext* defaultContext)
{
     HANDLE hProcess = GetCurrentProcess();
     HANDLE hThread = GetCurrentThread();

     const auto stack = platformlib::debugger::WalkTrace(defaultContext);

     std::println("\x1b[41mInternal Compiler Error:\x1b[0m {}", message);
     for (const auto& i : stack)
     {
          const auto address = reinterpret_cast<DWORD64>(i);
          DWORD64 displacement = 0;
          void* symbolInfoStorage = operator new(sizeof(SYMBOL_INFO) + MAX_PATH);
          if (symbolInfoStorage == nullptr) break;
          auto* symbol = new (symbolInfoStorage) SYMBOL_INFO{};
          symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
          symbol->MaxNameLen = MAX_PATH;

          if (SymFromAddr(hProcess, address, &displacement, symbol) != 0)
          {
               IMAGEHLP_LINE64 line{};
               line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
               DWORD lineDisplacement = 0;
               auto* moduleHandle = reinterpret_cast<HMODULE>(SymGetModuleBase64(hProcess, address));
               std::string moduleName{};
               moduleName.resize(MAX_PATH);
               if (moduleHandle != nullptr) GetModuleFileNameA(moduleHandle, moduleName.data(), MAX_PATH);

               if (SymGetLineFromAddr64(hProcess, address, &lineDisplacement, &line) != 0)
               {
                    std::println("\x1b[37m[{:02}] \x1b[34m{}:{}\x1b[0m", i, line.FileName, line.LineNumber);
                    std::println("       [\x1b[35m{}+0x{:X}\x1b[0m <\x1b[32m{}\x1b[0m>] @\x1b[35m{:x}\x1b[0m",
                                 symbol->Name, displacement, moduleName[0] != '\0' ? moduleName : "?", line.Address);
               }
               else
               {
                    std::println("\x1b[37m[{:02}] \x1b[35m{}\x1b[0m", i, symbol->Name);
                    std::println("       +0x{:X} <\x1b[32m{}\x1b[0m> @\x1b[35m{:x}\x1b[0m", displacement,
                                 moduleName[0] == '\0' ? moduleName : "?", address);
               }
          }
          else
               std::println("\x1b[37m[{:02}] \x1b[35m{}", i, address);

          operator delete(symbolInfoStorage);
     }

     std::print("\x1b[0m");
     SymCleanup(hProcess);
     ExitProcess(-1);
}
