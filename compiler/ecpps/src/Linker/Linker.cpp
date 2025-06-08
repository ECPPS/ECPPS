#include "Linker.h"
#include <memory>
#include <utility>
#include "WindowsLinker.h"

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
     }

     return nullptr;
}

std::vector<std::byte> ecpps::linker::Linker::SelectAndLink(const ecpps::CompilerConfig& config,
                                                            std::vector<std::byte> generatedMachineCode,
                                                            std::vector<std::pair<std::string, std::size_t>> functions,
                                                            std::size_t mainOffset)
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
     }

     selectedLinker->CodeSection(generatedMachineCode);

     for (const auto& [functionName, functionOffset] : functions)
          selectedLinker->ExportFunction(functionName, functionOffset);

     // TODO: Imports

     return selectedLinker->ToBytes(config.outputImage, mainOffset);
}
