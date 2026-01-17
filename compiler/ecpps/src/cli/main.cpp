#ifdef _WIN32
#include <Windows.h>
#endif

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <print>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <CodeGeneration/CodeEmitter.h>
#include <CodeGeneration/PseudoAssembly.h>
#include <Debugger/Debugger.h>
#include <Execution/IR.h>
#include <Linker/Linker.h>
#include <Parsing/AST.h>
#include <Parsing/Preprocessor.h>
#include <Parsing/SourceMap.h>
#include <Parsing/Tokeniser.h>
#include <Shared/Config.h>
#include <Shared/Diagnostics.h>

#ifdef min
#undef min
#undef max
#endif

static std::unordered_map<std::string, ecpps::Diagnostics*> g_diagnosticsReferences{};

#ifdef _WIN32

template <> struct ecpps::platformlib::PointerInterconvertibility<ecpps::platformlib::DebuggerContext, CONTEXT>
{
     constexpr static bool IsValid = true;
};

static void IssueDiagnostics(void)
{
     for (const auto& [sourceName, lpDiagnostics] : g_diagnosticsReferences)
     {
          const auto& diagnostics = *lpDiagnostics;

          for (const auto& diag : diagnostics.diagnosticsList) ecpps::diagnostics::PrintDiagnostic(sourceName, diag);
     }
}

static LONG WINAPI winExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
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

          ecpps::IssueICE(built, &ecpps::platformlib::DebuggerContext::From(*exceptionInfo->ContextRecord));
     }
     break;
     case EXCEPTION_INT_DIVIDE_BY_ZERO:
          ecpps::IssueICE("Divide by zero", &ecpps::platformlib::DebuggerContext::From(*exceptionInfo->ContextRecord));
          break;
     default:
          ecpps::IssueICE(
              std::format("Unhandled exception has occurred: {}", exceptionInfo->ExceptionRecord->ExceptionCode),
              &ecpps::platformlib::DebuggerContext::From(*exceptionInfo->ContextRecord));
          break;
     }

     return EXCEPTION_EXECUTE_HANDLER;
}

static void EnableVirtualProcessing(void)
{
     auto* const hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
     DWORD consoleMode{};
     GetConsoleMode(hConsoleOutput, &consoleMode);
     consoleMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
     SetConsoleMode(hConsoleOutput, consoleMode);
}
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
     EnableVirtualProcessing();
     SetUnhandledExceptionFilter(winExceptionHandler);
#endif

     try
     {
          const auto startTime = std::chrono::steady_clock::now();

          ecpps::CompilerConfig config{argc, argv};
          ecpps::SourceMap sources{config};

          if (sources.files.empty())
          {
               std::println("\x1b[31mNo input files\x1b[0m");
               return -1;
          }

          auto emitter = ecpps::codegen::CodeEmitter::New(ecpps::abi::ABI::Current().Isa());
          if (emitter == nullptr)
          {
               std::println("\x1b[31mUnsupported architecture for the code generation\x1b[0m");
               return -1;
          }
          std::println("Target: {}", emitter->Name());

          std::vector<std::byte> generatedMachineCode{};
          std::vector<std::pair<std::string, std::size_t>> functions{};
          std::size_t mainOffset{};

          bool hadErrors = false;
          const bool isVerbose = config.verboseStatus == ecpps::VerboseStatus::Verbose ||
                                 config.verboseStatus == ecpps::VerboseStatus::ExtraVerbose;
          const bool isExtraVerbose = config.verboseStatus == ecpps::VerboseStatus::ExtraVerbose;

          g_diagnosticsReferences.reserve(sources.files.size());

          for (auto& source : sources.files)
          {
               g_diagnosticsReferences.emplace(source.name, &source.diagnostics);

               try
               {
                    std::println("Compiling {}...", source.name);
                    // ECPPS pushed macros (& standard)

                    std::vector<ecpps::MacroReplacement> macros{};
                    macros.emplace_back("__cplusplus", std::nullopt, "202302", false); // TODO: 202302L
                    // TODO: __DATE__
                    // TODO: __FILE__
                    macros.emplace_back("__LINE__", std::nullopt, "1", false);
                    // TODO: __STDC_HOSTED__
                    // TODO: __STDCPP_DEFAULT_NEW_ALIGNMENT__
                    // TODO: __STDCPP_FLOAT16_T__
                    // TODO: __STDCPP_FLOAT32_T__
                    // TODO: __STDCPP_FLOAT64_T__
                    // TODO: __STDCPP_FLOAT128_T__
                    // TODO: __STDCPP_BFLOAT16_T__
                    // TODO: __TIME__
                    // TODO: feature-test macros
                    // TODO: __STDCPP_THREADS__

                    macros.emplace_back("__ecpps_stl_version", std::nullopt, "0", false);
                    macros.emplace_back("__ecpps_stl_version_minor", std::nullopt, "0", false);
                    macros.emplace_back("__ecpps_stl_version_patch", std::nullopt, "1", false);
                    macros.emplace_back("__ecpps_version", std::nullopt, "0", false);
                    macros.emplace_back("__ecpps_version_minor", std::nullopt, "0", false);
                    macros.emplace_back("__ecpps_version_patch", std::nullopt, "1", false);

                    const auto ppTokens = ecpps::Preprocessor::Parse(source.contents, macros);
                    const auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
                    if (isExtraVerbose) std::println();
                    if (isExtraVerbose) std::println("Tokens:");
                    if (isExtraVerbose) ecpps::Tokeniser::Print(tokens);
                    const auto ast = ecpps::ast::AST{tokens, source.diagnostics}.Parse();
                    if (isExtraVerbose) std::println();
                    if (isExtraVerbose) std::println("AST:");
                    if (isExtraVerbose)
                         for (const auto& node : ast) std::println("{}", node->ToString(0));
                    const auto ir = ecpps::ir::IR::Parse(source.diagnostics, ast);
                    if (isExtraVerbose) std::println();
                    if (isExtraVerbose) std::println("IR:");
                    if (isExtraVerbose)
                         for (const auto& node : ir) std::println("{}", node->ToString(0));
                    ecpps::codegen::Compile(source, ir);
                    if (isExtraVerbose) std::println();
                    if (isExtraVerbose) std::println("Assembly:");

                    std::unordered_map<std::string, std::size_t> routines{};
                    routines.reserve(source.compiledRoutines.size());

                    for (const auto& procedure : source.compiledRoutines)
                    {
                         if (isExtraVerbose)
                         {
                              std::println("{}:", procedure.name);
                              for (const auto& instruction : procedure.instructions)
                              {
                                   std::println("     {}", ecpps::codegen::ToString(instruction));
                              }
                         }

                         const auto machineCode = emitter->EmitRoutine(procedure, generatedMachineCode.size());

                         routines.emplace(procedure.name, generatedMachineCode.size());
                         generatedMachineCode.append_range(machineCode);
                         if (!isExtraVerbose) continue;
                    }

                    emitter->PatchCalls(generatedMachineCode, routines);

                    for (const auto& [procedureName, procedurOffset] : routines)
                    {
                         if (procedureName == "_EntryPoint") mainOffset = procedurOffset;

                         functions.emplace_back(procedureName, procedurOffset);
                    }

                    if (isExtraVerbose)
                    {
                         std::vector<std::pair<std::string, std::size_t>> ordered;
                         ordered.reserve(routines.size());
                         for (const auto& r : routines) ordered.emplace_back(r);

                         std::ranges::sort(ordered, {}, &std::pair<std::string, std::size_t>::second);

                         for (std::size_t i = 0; i < ordered.size(); ++i)
                         {
                              const auto& [routineName, routineOffset] = ordered[i];
                              std::println("{}:", routineName);

                              std::size_t start = std::min(routineOffset, generatedMachineCode.size());
                              std::size_t end = (i + 1 < ordered.size())
                                                    ? std::min(ordered[i + 1].second, generatedMachineCode.size())
                                                    : generatedMachineCode.size();

                              if (start >= end) continue;

                              auto machineCode = std::ranges::subrange(
                                  generatedMachineCode.begin() + static_cast<std::ptrdiff_t>(start),
                                  generatedMachineCode.begin() + static_cast<std::ptrdiff_t>(end));

                              constexpr std::size_t RowSize = 8; // in bytes
                              const auto rows = (machineCode.size() + RowSize - 1) / RowSize;

                              std::println("Emitted {} bytes:", machineCode.size());
                              for (std::size_t row = 0; row < rows; row++)
                              {
                                   std::print("| ");
                                   const auto offset = row * RowSize;
                                   for (std::size_t column = 0; column < RowSize; column++)
                                   {
                                        const auto byteOffset = offset + column;
                                        if (byteOffset >= machineCode.size()) std::print("   ");
                                        else
                                             std::print("{:02x} ",
                                                        static_cast<std::size_t>(
                                                            machineCode[static_cast<std::ptrdiff_t>(byteOffset)]));
                                   }
                                   std::println("|");
                              }
                         }
                    }

                    for (const auto& diag : source.diagnostics.diagnosticsList)
                    {
                         ecpps::diagnostics::PrintDiagnostic(source.name, diag);

                         if (diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Error &&
                             (!config.warningsAreErrors ||
                              diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Warning))
                              break;
                         hadErrors = true;
                    }
               }
               catch (const ecpps::TracedException& traceException)
               {
                    hadErrors = true;
                    try
                    {
                         for (const auto& diag : source.diagnostics.diagnosticsList)
                         {
                              ecpps::diagnostics::PrintDiagnostic(source.name, diag);

                              if (diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Error &&
                                  (!config.warningsAreErrors ||
                                   diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Warning))
                                   break;
                              hadErrors = true;
                         }
                    }
                    catch (const ecpps::TracedException& nestedTraceException)
                    {
                         ecpps::IssueICE(nestedTraceException);
                    }
                    catch (const std::exception& nestedException)
                    {
                         ecpps::IssueICE(nestedException.what(), nullptr);
                    }

                    ecpps::IssueICE(traceException);
                    return -1;
               }
               catch (const std::exception& e)
               {
                    hadErrors = true;
                    try
                    {
                         for (const auto& diag : source.diagnostics.diagnosticsList)
                         {
                              ecpps::diagnostics::PrintDiagnostic(source.name, diag);

                              if (diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Error &&
                                  (!config.warningsAreErrors ||
                                   diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Warning))
                                   break;
                              hadErrors = true;
                         }
                    }
                    catch (const ecpps::TracedException& nestedTraceException)
                    {
                         ecpps::IssueICE(nestedTraceException);
                    }
                    catch (const std::exception& nestedException)
                    {
                         ecpps::IssueICE(nestedException.what(), nullptr);
                    }

                    ecpps::IssueICE(e.what(), nullptr);
                    return -1;
               }
               catch (...)
               {
                    hadErrors = true;
                    try
                    {
                         for (const auto& diag : source.diagnostics.diagnosticsList)
                         {
                              ecpps::diagnostics::PrintDiagnostic(source.name, diag);

                              if (diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Error &&
                                  (!config.warningsAreErrors ||
                                   diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Warning))
                                   break;
                              hadErrors = true;
                         }
                    }
                    catch (const ecpps::TracedException& nestedTraceException)
                    {
                         ecpps::IssueICE(nestedTraceException);
                    }
                    catch (const std::exception& nestedException)
                    {
                         ecpps::IssueICE(nestedException.what(), nullptr);
                    }

                    ecpps::IssueICE("unknown", nullptr);
                    return -1;
               }
          }

          if (hadErrors)
          {
               const auto end = std::chrono::steady_clock::now();

               std::println("Compilation failed. {} elapsed",
                            std::chrono::duration_cast<std::chrono::milliseconds>(end - startTime));

               return -1;
          }

          if (isVerbose) std::println("Linking objects...");

          std::vector<std::byte> codeSection{};

          std::vector<std::byte> imageBytes = ecpps::linker::Linker::SelectAndLink(
              config, generatedMachineCode, functions, mainOffset, emitter->linkerForwardedRelocations, codeSection);

          if (imageBytes.empty())
          {
               std::println("No linker selected.");
               return -1;
          }

          if (isExtraVerbose)
          {
               constexpr std::size_t RowSize = 8; // in bytes
               const auto rows = (codeSection.size() + RowSize - 1) / RowSize;

               std::println("Emitted {} bytes, of which {} are code:", imageBytes.size(), codeSection.size());
               for (std::size_t row = 0; row < rows; row++)
               {
                    std::print("| ");
                    const auto offset = row * RowSize;
                    for (std::size_t column = 0; column < RowSize; column++)
                    {
                         const auto byteOffset = offset + column;
                         if (byteOffset >= codeSection.size()) std::print("   ");
                         else
                              std::print("{:02x} ", static_cast<std::size_t>(codeSection[byteOffset]));
                    }
                    std::println("|");
               }
          }

          std::ofstream outFile(config.outputImage, std::ios::binary);
          if (!outFile.is_open())
          {
               std::println("Failed to open file: {}", config.outputImage);
               return -1;
          }

          outFile.write(reinterpret_cast<const char*>(imageBytes.data()),
                        static_cast<std::streamsize>(imageBytes.size()));
          if (!outFile)
          {
               std::println("Failed during write to: {}", config.outputImage);
               return -1;
          }

          const auto outputImagePath = absolute(std::filesystem::path(config.outputImage));
          const auto end = std::chrono::steady_clock::now();
          std::println("Fully linked {}", outputImagePath.string());
          std::println("Compilation successful. {}ms elapsed",
                       (std::chrono::duration_cast<std::chrono::microseconds>(end - startTime) / 1000.0).count());
          outFile.close();

          if (config.useDebugger) return ecpps::debugging::Debugger::SelectAndDebug(config, outputImagePath);

          return 0;
     }
     catch (const std::exception& e)
     {
          ecpps::IssueICE(e.what(), nullptr);
     }
     catch (...)
     {
          ecpps::IssueICE("unknown", nullptr);
          return -1;
     }
}
