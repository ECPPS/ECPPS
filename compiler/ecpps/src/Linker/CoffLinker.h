#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "Linker.h"

namespace ecpps::linker::win
{
     struct COFFSection final
     {
          std::string name;
          std::vector<std::byte> data;

          std::uint32_t characteristics = 0;

          std::uint32_t relocationOffset = 0;
          std::uint16_t relocationCount = 0;
     };

     struct COFFRelocation final
     {
          std::uint32_t offset = 0;
          std::string symbolName;
          std::uint16_t type = 0;
          std::size_t sectionIndex = 0;
     };

     enum struct COFFStorageClass : std::uint8_t
     {
          External = 2,
          Static = 3,
          Label = 6
     };

     struct COFFSymbol final
     {
          std::string name;

          std::uint32_t value = 0;
          std::int16_t sectionIndex = 0;

          std::uint16_t type = 0;
          COFFStorageClass storageClass = COFFStorageClass::External;

          std::uint8_t auxSymbols = 0;
     };

     class CoffLinker final : public LinkerBase
     {
     public:
          void SetStringRelocations(const std::vector<std::size_t>& stringRelocations, std::size_t toRelocateWidth)
          {
               _stringRelocationOffsets = stringRelocations;
               _toRelocateWidth = toRelocateWidth;
          }

          explicit CoffLinker(const LinkerOptions<LinkerType::Coff>& options) {}

          std::vector<std::byte> CodeSection(std::vector<std::byte> data,
                                             const codegen::LinkerRelocationMap& relocationMap) override;

          void ExportFunction(const std::string& name, std::uint32_t address) override;

          void ImportFunction(const std::string& symbolName, const std::string& importName,
                              const std::string& dll) override;

          [[nodiscard]] std::vector<std::byte> ToBytes(const std::string& imageName, std::size_t entryPointAddress,
                                                       const std::vector<std::byte>& stringData) const override;

     private:
          std::vector<COFFSection> _sections{};
          std::vector<COFFRelocation> _relocations{};
          std::vector<COFFSymbol> _symbols{};

          std::unordered_map<std::string, std::uint32_t> _symbolOffsets{};

          std::vector<std::size_t> _stringRelocationOffsets{};
          std::size_t _toRelocateWidth = 4;
     };
} // namespace ecpps::linker::win
