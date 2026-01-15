#include "Win64.h"
#include <Windows.h>

#include <DbgHelp.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>

enum struct PromptResult : std::uint_fast8_t
{
     Continue,
     Exit,
     None
};

static std::uint64_t ResolveRegister(const CONTEXT& ctx, const std::string& name) { return 0; }

static std::string ResolveSymbol(HANDLE process, std::uintptr_t addr)
{
     std::string buffer{};
     buffer.resize(sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
     auto* symbol = reinterpret_cast<PSYMBOL_INFO>(buffer.data());
     symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
     symbol->MaxNameLen = MAX_SYM_NAME;

     IMAGEHLP_MODULE64 modInfo{};
     modInfo.SizeOfStruct = sizeof(modInfo);
     std::string moduleName;

     DWORD64 modBase = SymGetModuleBase64(process, addr);
     if (modBase != 0 && SymGetModuleInfo64(process, modBase, &modInfo) != 0) { moduleName = modInfo.ModuleName; }

     if (SymFromAddr(process, addr, nullptr, symbol) != 0)
     {
          return std::format("{}!{}+0x{:X}", moduleName.empty() ? "?" : moduleName, symbol->Name,
                             addr - symbol->Address);
     }

     if (!moduleName.empty()) { return std::format("{}!0x{:016X}", moduleName, addr); }

     return std::format("0x{:016X}", addr);
}

static void PrintStackTrace(HANDLE process, HANDLE thread, const CONTEXT& ctx)
{
     STACKFRAME64 frame{};
     frame.AddrPC.Offset = ctx.Rip;
     frame.AddrFrame.Offset = ctx.Rbp;
     frame.AddrStack.Offset = ctx.Rsp;
     frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

     std::println("Stack trace:");

     for (int frameIndex = 0; frameIndex < 64; ++frameIndex)
     {
          if (StackWalk64(
                  IMAGE_FILE_MACHINE_AMD64, process, thread, &frame,
                  reinterpret_cast<PVOID>(const_cast<CONTEXT*>(&ctx)), // NOLINT(cppcoreguidelines-pro-type-const-cast)
                  nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) == 0)
               break;

          if (frame.AddrPC.Offset == 0) break;
          std::println("  {}", ResolveSymbol(process, static_cast<std::uintptr_t>(frame.AddrPC.Offset)));
     }
}
static void Disassemble(HANDLE process, std::uintptr_t address, std::size_t size = 16)
{
     std::array<std::byte, 32> bytes{};
     SIZE_T bytesRead{};
     if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), bytes.data(), size, &bytesRead) == 0)
     {
          std::println("Failed to read instructions at 0x{:016X} (error {})", address, GetLastError());
          return;
     }

     std::println("At {}:", ResolveSymbol(process, address));
     std::print("  ");
     for (std::size_t i = 0; i < bytesRead; i++) { std::print("{:02X} ", std::to_integer<unsigned char>(bytes[i])); }
     std::println("; <no disasm>");
}

static PromptResult PromptLoop(HANDLE process, HANDLE thread)
{
     CONTEXT ctx{};
     ctx.ContextFlags = CONTEXT_FULL;
     if (GetThreadContext(thread, &ctx) != 0)
     {
          std::println("Failed to get thread context (error {})", GetLastError());
     }

     std::string input;
     std::print("> ");
     if (!std::getline(std::cin, input)) return PromptResult::Exit;

     std::istringstream stream(input);
     std::string command;
     stream >> command;

     if (command == "c" || command == "continue") return PromptResult::Continue;
     if (command == "exit") return PromptResult::Exit;
     if (command == "print")
     {
          std::string queryValue;
          std::uint64_t value{};
          stream >> queryValue;
          if (queryValue.starts_with('$'))
          {
               auto regName = queryValue.substr(1);
               value = ResolveRegister(ctx, regName);
          }
          std::println("{} = {} (0x{:x})", queryValue, value, value);
          return PromptResult::None;
     }
     if (command == "di")
     {
          std::uintptr_t addr = ctx.Rip;
          Disassemble(process, addr);
          return PromptResult::None;
     }
     if (command == "stack")
     {
          PrintStackTrace(process, thread, ctx);
          return PromptResult::None;
     }
     if (command == "dq")
     {
          std::string addressToken;
          std::size_t count = 1;
          std::string type = "qword";
          stream >> addressToken >> count >> type;

          std::uintptr_t address = 0;
          if (addressToken.starts_with('$'))
          {
               auto regName = addressToken.substr(1);
               address = ResolveRegister(ctx, regName);
               if (address == 0)
               {
                    std::println("Unknown or zero register '{}'", regName);
                    return PromptResult::None;
               }
          }
          else
               std::from_chars(addressToken.data(), addressToken.data() + addressToken.size(), address, 16);

          SIZE_T bytesRead = 0;
          std::vector<std::byte> buffer(count * 8);
          if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer.data(), buffer.size(),
                                &bytesRead) != 0)
          {
               std::println("Failed to read memory at 0x{:016x} (error {})", address, GetLastError());
               return PromptResult::None;
          }

          if (type == "qword" || type == "q")
          {
               for (std::size_t i = 0; i < bytesRead / 8; ++i)
               {
                    auto value = std::bit_cast<std::uint64_t*>(buffer.data())[i];
                    std::println("{:016x}: 0x{:016x}", address + (i * 8uz), value);
               }
          }
          else if (type == "dword" || type == "d")
          {
               for (std::size_t i = 0; i < bytesRead / 4; ++i)
               {
                    auto value = std::bit_cast<std::uint32_t*>(buffer.data())[i];
                    std::println("{:016x}: 0x{:08x}", address + (i * 4uz), value);
               }
          }
          else if (type == "word" || type == "w")
          {
               for (std::size_t i = 0; i < bytesRead / 2; ++i)
               {
                    auto value = std::bit_cast<std::uint16_t*>(buffer.data())[i];
                    std::println("{:016x}: 0x{:04x}", address + (i * 2uz), value);
               }
          }
          else if (type == "byte" || type == "b")
          {
               for (std::size_t i = 0; i < bytesRead; ++i)
               {
                    std::println("{:016x}: 0x{:02x}", address + i, static_cast<std::uint32_t>(buffer[i]));
               }
          }
          else if (type == "ascii" || type == "a")
          {
               std::print("{:016x}: ", address);
               for (std::size_t i = 0; i < bytesRead; ++i)
               {
                    char c = static_cast<char>(buffer[i]);
                    std::print("{}", std::isprint(static_cast<unsigned char>(c)) != 0 ? c : '.');
               }
               std::println("");
          }
          else
               std::println("Unknown type '{}'", type);

          return PromptResult::None;
     }
     std::println("Unknown command '{}'", command);
     return PromptResult::None;
}

int ecpps::debugging::Win64Debugger::Debug([[maybe_unused]] CompilerConfig& configuration,
                                           std::filesystem::path program) const
{
     std::wstring cmd = program.wstring();
     if (cmd.empty()) return -1;

     std::vector<wchar_t> commandLineBuffer(cmd.begin(), cmd.end());
     commandLineBuffer.emplace_back(0);
     LPWSTR commandLine = commandLineBuffer.data();

     STARTUPINFOW si{};
     si.cb = sizeof(si);
     PROCESS_INFORMATION pi{};
     DWORD creationFlags = DEBUG_ONLY_THIS_PROCESS;

     if (CreateProcessW(nullptr, commandLine, nullptr, nullptr, FALSE, creationFlags, nullptr, nullptr, &si, &pi) != 0)
     {
          return static_cast<int>(GetLastError());
     }

     SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

     // std::string symbolPath = "srv*C:\\Symbols*https://msdl.microsoft.com/download/symbols";
     if (SymInitialize(pi.hProcess, nullptr, TRUE) == 0)
     {
          std::println("SymInitialize failed ({:x})", GetLastError());
     }

     DWORD exitCode = 0; // NOLINT
     DEBUG_EVENT dbgEvent{};
     bool running = true;

     while (running)
     {
          if (!WaitForDebugEvent(&dbgEvent, INFINITE))
          {
               TerminateProcess(pi.hProcess, 1);
               exitCode = 1;
               break;
          }

          DWORD continueStatus = DBG_CONTINUE;

          switch (dbgEvent.dwDebugEventCode)
          {
          case EXCEPTION_DEBUG_EVENT:
               std::println("# Exception 0x{:08x} at 0x{:016x}", dbgEvent.u.Exception.ExceptionRecord.ExceptionCode,
                            reinterpret_cast<std::uintptr_t>(dbgEvent.u.Exception.ExceptionRecord.ExceptionAddress));
               {
                    while (true)
                    {
                         const auto result = PromptLoop(pi.hProcess, pi.hThread);
                         if (result == PromptResult::Continue) break;
                         if (result == PromptResult::Exit)
                         {
                              TerminateProcess(pi.hProcess, 1);
                              running = false;
                              break;
                         }
                    }
               }
               break;

          case CREATE_PROCESS_DEBUG_EVENT:
          {
               auto base = reinterpret_cast<DWORD64>(dbgEvent.u.CreateProcessInfo.lpBaseOfImage);
               HANDLE file = dbgEvent.u.CreateProcessInfo.hFile;
               SymLoadModuleEx(pi.hProcess, file, nullptr, nullptr, base, 0, nullptr, 0);
               std::println("[+] Loaded main module at 0x{:016X}", base);
               if (file) CloseHandle(file);
               break;
          }

          case EXIT_PROCESS_DEBUG_EVENT:
               exitCode = dbgEvent.u.ExitProcess.dwExitCode;
               running = false;
               break;

          case LOAD_DLL_DEBUG_EVENT:
          {
               auto base = reinterpret_cast<DWORD64>(dbgEvent.u.LoadDll.lpBaseOfDll);
               HANDLE file = dbgEvent.u.LoadDll.hFile;
               SymLoadModuleEx(pi.hProcess, file, nullptr, nullptr, base, 0, nullptr, 0);

               std::string path;
               if (dbgEvent.u.LoadDll.lpImageName)
               {
                    std::string nameBuffer{};
                    nameBuffer.resize(MAX_PATH);
                    SIZE_T bytesRead = 0;
                    LPCVOID namePtr = nullptr;
                    if (ReadProcessMemory(pi.hProcess, dbgEvent.u.LoadDll.lpImageName, &namePtr, sizeof(namePtr),
                                          &bytesRead) &&
                        namePtr)
                    {
                         ReadProcessMemory(pi.hProcess, namePtr, nameBuffer.data(), nameBuffer.size(), &bytesRead);
                         path = nameBuffer;
                    }
               }
               std::println("# DLL loaded at 0x{:016x} ({})", base, path.empty() ? "unknown" : path);
               if (dbgEvent.u.LoadDll.hFile) CloseHandle(dbgEvent.u.LoadDll.hFile);
          }
          break;
          case UNLOAD_DLL_DEBUG_EVENT:
               std::println("# DLL unloaded at 0x{:016x}",
                            reinterpret_cast<std::uintptr_t>(dbgEvent.u.UnloadDll.lpBaseOfDll));
               break;

          case OUTPUT_DEBUG_STRING_EVENT:
          {
               SIZE_T read{};
               std::string string;
               string.resize(dbgEvent.u.DebugString.nDebugStringLength);
               ReadProcessMemory(pi.hProcess, dbgEvent.u.DebugString.lpDebugStringData, string.data(), string.size(),
                                 &read);
               std::print("{}", string);
          }
          break;

          case RIP_EVENT: running = false; break;

          default: break;
          }

          ContinueDebugEvent(dbgEvent.dwProcessId, dbgEvent.dwThreadId, continueStatus);
     }

     if (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0) WaitForSingleObject(pi.hProcess, INFINITE);

     DWORD processExitCode = 0;
     if (GetExitCodeProcess(pi.hProcess, &processExitCode) != 0) processExitCode = -1;

     CloseHandle(pi.hThread);
     CloseHandle(pi.hProcess);

     std::println("Process exited with code {} (0x{:x})", processExitCode, processExitCode);

     return static_cast<int>(processExitCode);
}
