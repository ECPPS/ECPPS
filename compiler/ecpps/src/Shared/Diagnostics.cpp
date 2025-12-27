#include "Diagnostics.h"
#include <dbghelp.h>
#include <print>

void ecpps::IssueICE(const TracedException& ex)
{
     HANDLE process = GetCurrentProcess();
     SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
     SymInitialize(process, nullptr, TRUE);

     std::println("\x1b[41mInternal Compiler Error:\x1b[0m {}", ex.what());
     for (auto* i : ex.trace)
     {
          auto address = reinterpret_cast<DWORD64>(i);
          DWORD64 displacement = 0;
          void* storage = operator new(sizeof(SYMBOL_INFO) + MAX_PATH);
          std::memset(storage, 0, sizeof(SYMBOL_INFO) + MAX_PATH);
          auto* symbol = new (storage) SYMBOL_INFO{};
          symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
          symbol->MaxNameLen = MAX_PATH;

          if (SymFromAddr(process, address, &displacement, symbol) != 0)
          {
               IMAGEHLP_LINE64 line{};
               line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
               DWORD lineDisp = 0;
               if (SymGetLineFromAddr64(process, address, &lineDisp, &line) != 0)
                    std::println("  at \x1b[34m{}:{}\x1b[0m <\x1b[32m{}\x1b[0m>", line.FileName, line.LineNumber,
                                 symbol->Name);
               else
                    std::println("  at \x1b[32m{}\x1b[0m @{:x}", symbol->Name, address);
          }

          operator delete(storage);
     }

     SymCleanup(process);
     ExitProcess(-1);
}

void ecpps::IssueICE(std::string_view message, CONTEXT* defaultContext)
{
     HANDLE process = GetCurrentProcess();
     HANDLE thread = GetCurrentThread();

     SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
     SymInitialize(process, nullptr, TRUE);

     CONTEXT context{};
     if (defaultContext != nullptr) context = *defaultContext;
     else
          RtlCaptureContext(&context);

     STACKFRAME64 frame{};
     frame.AddrPC.Offset = context.Rip;
     frame.AddrFrame.Offset = context.Rbp;
     frame.AddrStack.Offset = context.Rsp;

     frame.AddrPC.Mode = AddrModeFlat;
     frame.AddrFrame.Mode = AddrModeFlat;
     frame.AddrStack.Mode = AddrModeFlat;

     std::vector<void*> stack;
     while (StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                        SymGetModuleBase64, nullptr) != 0)
     {
          stack.push_back(reinterpret_cast<void*>(frame.AddrPC.Offset));
     }

     std::println("\x1b[41mInternal Compiler Error:\x1b[0m {}", message);
     for (auto& i : stack)
     {
          const auto address = reinterpret_cast<DWORD64>(i);
          DWORD64 displacement = 0;
          void* symbolInfoStorage = operator new(sizeof(SYMBOL_INFO) + MAX_PATH);
          if (symbolInfoStorage == nullptr) break;
          auto* symbol = new (symbolInfoStorage) SYMBOL_INFO{};
          symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
          symbol->MaxNameLen = MAX_PATH;

          if (SymFromAddr(process, address, &displacement, symbol) != 0)
          {
               IMAGEHLP_LINE64 line{};
               line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
               DWORD lineDisplacement = 0;
               auto* moduleHandle = reinterpret_cast<HMODULE>(SymGetModuleBase64(process, address));
               std::string moduleName{};
               moduleName.resize(MAX_PATH);
               if (moduleHandle != nullptr) GetModuleFileNameA(moduleHandle, moduleName.data(), MAX_PATH);

               if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != 0)
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
     SymCleanup(process);
     ExitProcess(-1);
}
