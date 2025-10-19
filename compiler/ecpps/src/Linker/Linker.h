#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../CodeGeneration/CodeEmitter.h"
#include "../Shared/Config.h"

namespace ecpps::linker
{
     enum struct LinkerBitness : std::uint_fast8_t
     {
          x32,
          x64
     };

     enum struct PESubsystem : std::uint8_t
     {
          Unknown = 0,
          Native = 1,
          Windows = 2,
          Console = 3,
          NativeWindows = 5,
          WindowsCeGui = 9,
          EfiApplication = 10,
          EfiBootServicesDriver = 11,
          EfiRuntimeDrive = 12,
          EfiRom = 13,
          Xbox = 14,
          WindowsBootApplication = 15,
          XboxCodeCatalogue = 16
     };

     enum struct LinkType : bool
     {
          Executable = true,
          DynamicLibrary = false
     };

     enum struct LinkerType : std::uint_fast8_t
     {
          PE,
     };
     struct LinkerOptionsBase
     {
          virtual ~LinkerOptionsBase(void) = default;

     protected:
          explicit LinkerOptionsBase(void) = default;
     };

     template <LinkerType TType> struct LinkerOptions;

     template <> struct LinkerOptions<LinkerType::PE> final : LinkerOptionsBase
     {
          // required
          PESubsystem subsystem;
          LinkType type;
          LinkerBitness bitness;

          // optional
          std::uint64_t baseAddress = 0x140000000;
          bool enableNXBit = true;
          bool dynamicBase = false;
          std::uint32_t fileAlignment = 0x200;

          explicit LinkerOptions(const PESubsystem subsystem, const LinkType type, const LinkerBitness bitness)
              : subsystem(subsystem), type(type), bitness(bitness)
          {
          }
     };

     class LinkerBase
     {
     public:
          virtual ~LinkerBase(void) = default;

          [[nodiscard]] virtual std::vector<std::byte> ToBytes(const std::string& imageName,
                                                               std::size_t entryPointAddress) const = 0;

          virtual std::vector<std::byte> CodeSection(std::vector<std::byte> data,
                                                     const codegen::LinkerRelocationMap& relocationMap) = 0;
          virtual void ExportFunction(const std::string& name, std::uint32_t address) = 0;
          virtual void ImportFunction(const std::string& name, const std::string& dll) = 0;

     private:
     };

     class Linker
     {
     public:
          [[nodiscard]] static std::unique_ptr<LinkerBase> CreateLinker(LinkerType type,
                                                                        std::unique_ptr<LinkerOptionsBase> options);
          explicit Linker(void) = delete;

          static std::vector<std::byte> SelectAndLink(const ecpps::CompilerConfig& config,
                                                      std::vector<std::byte> generatedMachineCode,
                                                      std::vector<std::pair<std::string, std::size_t>> functions,
                                                      std::size_t mainOffset,
                                                      const codegen::LinkerRelocationMap& relocationMap,
                                                      std::vector<std::byte>& diagnosticsCodeSection);
     };
} // namespace ecpps::linker