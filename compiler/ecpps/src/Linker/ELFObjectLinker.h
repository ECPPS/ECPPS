#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "Linker.h"

namespace ecpps::linker::elf
{
     class ELFObjectLinker final : public LinkerBase
     {
     public:
          void SetStringRelocations(const std::vector<std::size_t>& stringRelocations, std::size_t toRelocateWidth)
          {
               _stringRelocationOffsets = stringRelocations;
               _toRelocateWidth = toRelocateWidth;
          }

          [[nodiscard]] std::vector<std::byte> CodeSection(
               std::vector<std::byte> data, const codegen::LinkerRelocationMap& relocationMap) override;

          void ExportFunction(const std::string& name, std::uint32_t address) override;
          void ImportFunction(const std::string& symbolName, const std::string& importName,
                              const std::string& dll) override;

          [[nodiscard]] std::vector<std::byte> ToBytes(const std::string& imageName, std::size_t entryPointAddress,
                                                       const std::vector<std::byte>& stringData) const override;

     private:
          struct Symbol final
          {
               std::string name;
               std::uint16_t sectionIndex = 0;
               std::uint64_t value = 0;
          };

          struct Relocation final
          {
               std::uint64_t offset = 0;
               std::string symbolName;
               std::uint32_t type = 0;
               std::int64_t addend = 0;
          };

          [[nodiscard]] std::size_t EnsureSymbol(const std::string& name);

          std::vector<std::byte> _text{};
          std::vector<Symbol> _symbols{};
          std::vector<Relocation> _relocations{};
          std::unordered_map<std::string, std::size_t> _symbolIndices{};
          std::unordered_map<std::string, std::string> _symbolAliases{};
          std::vector<std::size_t> _stringRelocationOffsets{};
          std::size_t _toRelocateWidth = 4;
     };
} // namespace ecpps::linker::elf
