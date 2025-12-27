#include "Debugger.h"
#include <filesystem>
#include <memory>
#include <print>
#include "Debuggers/Win64.h"

int ecpps::debugging::Debugger::SelectAndDebug(CompilerConfig& configuration, std::filesystem::path program)
{
     std::unique_ptr<Debugger> selectedDebugger{};
     switch (configuration.linker)
     {
     case LinkerUsed::Windows64: selectedDebugger = std::make_unique<Win64Debugger>(); break;
     default:
     {
          std::println("Unable to launch the debugger: no debugger associated with the current executable format.");
          return -1;
     }
     }

     return selectedDebugger->Debug(configuration, std::move(program));
}
