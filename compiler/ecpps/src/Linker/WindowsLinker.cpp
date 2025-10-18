#include "WindowsLinker.h"
#include <cstdint>
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
     std::size_t offset{};
     std::unordered_map<std::string, std::vector<std::byte>> relocationThunks{};
     for (const auto& [where, relocation] : relocationMap)
     {
          const auto resolvedAddress = this->LookupSymbol(relocation.symbolName);
          auto toInsert = relocation.apply(Address{resolvedAddress - where.Value() - offset}, relocationThunks);
          auto begin = data.begin() + offset;
          offset += toInsert.size();
          data.insert_range(begin + where.Value(), std::move(toInsert));
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

void ecpps::linker::win::WindowsLinker::ImportFunction(const std::string& name, const std::string& dll)
{
     this->_imports[dll].push_back(name);
}

void ecpps::linker::win::WindowsLinker::AddSection(const PESection& value) { this->_sections.emplace_back(value); }

void ecpps::linker::win::WindowsLinker::ExportAt(const std::string& name, std::uint32_t address)
{
     this->_exports.emplace(name, address);
}

std::vector<std::byte> ecpps::linker::win::WindowsLinker::ToBytes(const std::string& imageName,
                                                                  const std::size_t entryPointAddress) const
{
     return this->Link(entryPointAddress + ExportDisplacement).toBytes(imageName);
}

template <std::integral T> constexpr static T AlignUp(const T value, const T alignment)
{
     return (value + alignment - 1) & ~(alignment - 1);
}

std::uint32_t ecpps::linker::win::WindowsLinker::LookupSymbol(const std::string& symbolName) const
{
     if (auto it = this->_exports.find(symbolName); it != this->_exports.end()) return it->second; // absolute RVA

     std::uint32_t iltPtr = 0; // offset to first thunk (relative to idataVA)
     std::uint32_t iatPtr = 0; // offset to IAT entries
     std::uint32_t currentNameOffset = 0;

     // Calculate descriptorCount, thunk offsets etc. the same way as BuildIdataBuffer
     const size_t descriptorCount = this->_imports.size() + 1;
     const size_t descriptorSize = 20;
     size_t totalThunks = 0;
     for (auto& kv : this->_imports) totalThunks += kv.second.size() + 1;

     const size_t intOffset = AlignUp(descriptorCount * descriptorSize, 8UZ);
     const size_t iatOffset = intOffset + totalThunks * sizeof(std::uint64_t);
     size_t nameOffset = AlignUp(iatOffset + totalThunks * sizeof(std::uint64_t), 2UZ);

     iltPtr = static_cast<std::uint32_t>(intOffset);
     iatPtr = static_cast<std::uint32_t>(iatOffset);
     currentNameOffset = static_cast<std::uint32_t>(nameOffset);

     // Now iterate DLLs
     for (auto& [dll, funcs] : this->_imports)
     {
          for (std::size_t i = 0; i < funcs.size(); ++i)
          {
               if (funcs[i] == symbolName)
               {
                    // Compute the IAT entry RVA
                    // The IAT starts at iatOffset relative to idataVA
                    return 0x2000 + iatPtr + static_cast<std::uint32_t>(i * sizeof(std::uint64_t)) - 6;
               }

               // Update currentNameOffset if you also need name RVA
               currentNameOffset += 2 + static_cast<std::uint32_t>(funcs[i].size() + 1);
               currentNameOffset = static_cast<std::uint32_t>(AlignUp(currentNameOffset, 2U));
          }

          // Advance iatPtr past this DLL's thunks + null terminator
          iatPtr += static_cast<std::uint32_t>((funcs.size() + 1) * sizeof(std::uint64_t));
     }

     throw nullptr;
     //throw std::runtime_error("Symbol not found: " + symbolName);
}


ecpps::linker::win::PEImage ecpps::linker::win::WindowsLinker::Link(const std::uint32_t entryPoint) const
{
     PEImage output{this->_imageBase, entryPoint,      this->_isNxCompatible, this->_isRelocatable,
                    this->_subsystem, this->_linkType, this->_fileAlignment};
     output.sections = this->_sections;
     output.exports = this->_exports;
     output.imports = this->_imports;
     output._ntHeaders.optionalHeader.sizeOfCode = this->_sizeOfCode;

     // TODO: Real values
     output._ntHeaders.optionalHeader.baseOfCode = 0x1000;
     output._ntHeaders.optionalHeader.sizeOfHeapCommit = 0x1000;
     output._ntHeaders.optionalHeader.sizeOfHeapReserve = 0x100000;
     output._ntHeaders.optionalHeader.sizeOfStackCommit = 0x1000;
     output._ntHeaders.optionalHeader.sizeOfStackReserve = 0x100000;

     return output;
}
