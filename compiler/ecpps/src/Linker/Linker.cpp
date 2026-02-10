#include "Linker.h"
#include <memory>
#include <stdexcept>
#include <utility>
#include "../CodeGeneration/PseudoAssembly.h"
#include "WindowsLinker.h"
#include "dllHelp.h"

std::unique_ptr<ecpps::linker::LinkerBase> ecpps::linker::Linker::CreateLinker(
    LinkerType type, std::unique_ptr<LinkerOptionsBase> options)
{
     switch (type)
     {
     case LinkerType::PE:
     {
          auto peOptions = dynamic_cast<LinkerOptions<LinkerType::PE>&>(*options);
          return std::make_unique<win::WindowsLinker>(std::move(peOptions));
     }
     default: return nullptr;
     }
}

std::vector<std::byte> ecpps::linker::Linker::SelectAndLink(
    const ecpps::CompilerConfig& config, std::vector<std::byte> generatedMachineCode,
    const std::vector<std::pair<std::string, std::size_t>>& functions, std::size_t mainOffset,
    const codegen::LinkerRelocationMap& relocationMap, std::vector<std::byte>& diagnosticsCodeSection,
    const std::vector<std::size_t>& stringRelocations, std::size_t toRelocateWidth,
    const std::vector<std::byte>& stringData)
{
     std::unique_ptr<LinkerBase> selectedLinker = nullptr;

     switch (config.linker)
     {
     case LinkerUsed::Windows64:
     case LinkerUsed::Windows32:
     {
          selectedLinker = ecpps::linker::Linker::CreateLinker(
              ecpps::linker::LinkerType::PE,
              std::make_unique<ecpps::linker::LinkerOptions<ecpps::linker::LinkerType::PE>>(
                  ecpps::linker::PESubsystem::Console, ecpps::linker::LinkType::Executable,
                  config.linker == LinkerUsed::Windows32 ? LinkerBitness::x32 : LinkerBitness::x64));
     }
     break;
     case LinkerUsed::Caosys:
     {
          selectedLinker = ecpps::linker::Linker::CreateLinker(
              ecpps::linker::LinkerType::PE,
              std::make_unique<ecpps::linker::LinkerOptions<ecpps::linker::LinkerType::PE>>(
                  ecpps::linker::PESubsystem::Console, ecpps::linker::LinkType::Executable, LinkerBitness::x64));
     }
     break;
     default: return {};
     }
     if (selectedLinker == nullptr) return {};

     const auto availableExports = GetExportsFromDlls(config.importedLibraries);
     for (const auto& import : ecpps::codegen::g_functionImports)
     {
          std::string dllName{};

          for (const auto& [dll, names] : availableExports)
          {
               if (!dllName.empty()) break;
               for (const auto& name : names)
               {
                    if (name != import) continue;
                    dllName = dll;
                    break;
               }
          }
          if (dllName.empty())
               throw ecpps::TracedException(std::logic_error("LINK error: unresolved function " + import));
          selectedLinker->ImportFunction(import, dllName);
     }

     diagnosticsCodeSection = selectedLinker->CodeSection(std::move(generatedMachineCode), relocationMap);

     for (const auto& [functionName, functionOffset] : functions)
          selectedLinker->ExportFunction(functionName, functionOffset);

     // TODO: Imports

     return selectedLinker->ToBytes(config.outputImage, mainOffset, stringData);
}
