#include "WindowsLinker.h"
#include <algorithm>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include "PE.h"

constexpr std::string_view CodeSectionName = ".text";

/// <summary>
/// RVA of the functions/code section, has to be used in all exports
/// </summary>
constexpr std::uint32_t ExportDisplacement = 0x1000;

std::vector<std::byte> ecpps::linker::win::WindowsLinker::CodeSection(std::vector<std::byte> data,
                                                                      const codegen::LinkerRelocationMap& relocationMap)
{
     PESection textSection{};
     std::unordered_map<std::string, std::vector<std::byte>> relocationThunks{};
     auto codeSize = data.size();

     std::vector<std::pair<ByteOffset, codegen::Relocation>> sortedRelocations(relocationMap.begin(),
                                                                               relocationMap.end());
     std::ranges::sort(sortedRelocations,
                       [](const auto& a, const auto& b) { return a.first.Value() > b.first.Value(); });

     std::size_t cumulativeShift = 0;

     for (const auto& [where, relocation] : sortedRelocations)
     {
          const auto resolvedAddress = this->LookupSymbol(relocation.symbolName, codeSize);
          auto toInsert =
              relocation.apply(Address{resolvedAddress - where.Value() - cumulativeShift}, relocationThunks);

          const auto pos = where.Value();
          for (std::size_t i = 0; i < toInsert.size() && (pos + i) < data.size(); ++i) { data[pos + i] = toInsert[i]; }

          if (toInsert.size() > relocation.applyOutputSize)
          {
               const auto extraBytes = toInsert.size() - relocation.applyOutputSize;
               const auto insertPos = data.begin() + static_cast<std::streamsize>(pos + relocation.applyOutputSize);
               const auto extraBegin = toInsert.begin() + static_cast<std::streamsize>(relocation.applyOutputSize);
               data.insert(insertPos, extraBegin, toInsert.end());
               cumulativeShift += extraBytes;
          }
     }

     textSection.name = CodeSectionName;
     textSection.flags = PESectionFlags::Read | PESectionFlags::Execute | PESectionFlags::ExecutableCode;
     this->_sizeOfCode = data.size();
     textSection.data = std::move(data);

     this->AddSection(textSection);
     return textSection.data;
}

void ecpps::linker::win::WindowsLinker::ExportFunction(const std::string& name, std::uint32_t address)
{
     this->ExportAt(name, ExportDisplacement + address);
}

void ecpps::linker::win::WindowsLinker::ImportFunction(const std::string& symbolName, const std::string& importName,
                                                       const std::string& dll)
{
     this->_imports[dll].push_back(importName);
     this->_importNameMap[symbolName] = importName;
}

void ecpps::linker::win::WindowsLinker::AddSection(const PESection& value) { this->_sections.emplace_back(value); }

void ecpps::linker::win::WindowsLinker::ExportAt(const std::string& name, std::uint32_t address)
{
     this->_exports.emplace(name, address);
}

std::vector<std::byte> ecpps::linker::win::WindowsLinker::ToBytes(const std::string& imageName,
                                                                  const std::size_t entryPointAddress,
                                                                  const std::vector<std::byte>& stringData) const
{
     return this->Link(entryPointAddress + ExportDisplacement).ToBytes(imageName, stringData);
}

template <std::integral T> constexpr static T AlignUp(const T value, const T alignment)
{
     return (value + alignment - 1) & ~(alignment - 1);
}

std::uint32_t ecpps::linker::win::WindowsLinker::LookupSymbol(const std::string& symbolName,
                                                              std::uint32_t codeSize) const
{
     if (auto it = this->_exports.find(symbolName); it != this->_exports.end()) return it->second;

     std::string lookupName = symbolName;
     if (auto mapIt = this->_importNameMap.find(symbolName); mapIt != this->_importNameMap.end())
     {
          lookupName = mapIt->second;
     }

     std::uint32_t iatPtr = 0;
     std::uint32_t currentNameOffset = 0;

     const size_t descriptorCount = this->_imports.size() + 1;
     const size_t descriptorSize = 20;
     size_t totalThunks = 0;
     for (const auto& kv : this->_imports) totalThunks += kv.second.size() + 1;

     const size_t intOffset = AlignUp(descriptorCount * descriptorSize, 8UZ);
     const size_t iatOffset = intOffset + (totalThunks * sizeof(std::uint64_t));
     size_t nameOffset = AlignUp(iatOffset + (totalThunks * sizeof(std::uint64_t)), 2UZ);

     iatPtr = static_cast<std::uint32_t>(iatOffset);
     currentNameOffset = static_cast<std::uint32_t>(nameOffset);

     const std::uint32_t textBegin = 0; // we are already uh in text...
     const std::uint32_t textEnd = AlignUp(textBegin + codeSize, 0x1000U);
     const std::uint32_t idataRva = textEnd + 0x1000;

     for (const auto& [dll, funcs] : this->_imports)
     {
          for (std::size_t i = 0; i < funcs.size(); ++i)
          {
               if (funcs[i] == lookupName)
               {
                    return idataRva + iatPtr + static_cast<std::uint32_t>(i * sizeof(std::uint64_t)) - 6;
               }

               currentNameOffset += 2 + static_cast<std::uint32_t>(funcs[i].size() + 1);
               currentNameOffset = static_cast<std::uint32_t>(AlignUp(currentNameOffset, 2U));
          }

          iatPtr += static_cast<std::uint32_t>((funcs.size() + 1) * sizeof(std::uint64_t));
     }

     return 0;
}

ecpps::linker::win::PEImage ecpps::linker::win::WindowsLinker::Link(const std::uint32_t entryPoint) const
{
     PEImage output{this->_imageBase, entryPoint,      this->_isNxCompatible, this->_isRelocatable,
                    this->_subsystem, this->_linkType, this->_fileAlignment};
     output.sections = this->_sections;
     output.exports = this->_exports;
     output.imports = this->_imports;
     output.ntHeaders.optionalHeader.sizeOfCode = this->_sizeOfCode;

     // TODO: Real values
     output.ntHeaders.optionalHeader.baseOfCode = 0x1000;
     output.ntHeaders.optionalHeader.sizeOfHeapCommit = 0x1000;
     output.ntHeaders.optionalHeader.sizeOfHeapReserve = 0x100000;
     output.ntHeaders.optionalHeader.sizeOfStackCommit = 0x1000;
     output.ntHeaders.optionalHeader.sizeOfStackReserve = 0x100000;

     return output;
}
