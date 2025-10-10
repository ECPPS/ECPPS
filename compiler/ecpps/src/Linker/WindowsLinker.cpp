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
     for (const auto& [where, relocation] : relocationMap)
     {
          const auto resolvedAddress = this->LookupSymbol(relocation.symbolName);
          auto toInsert = relocation.apply(Address{resolvedAddress});
          auto begin = data.begin() + offset;
          offset += toInsert.size();
          data.insert_range(begin + where.Value(), std::move(toInsert));
     }

     textSection.name = CodeSectionName;
     textSection.flags = PESectionFlags::Read | PESectionFlags::Execute | PESectionFlags::ExecutableCode;
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

std::uint32_t ecpps::linker::win::WindowsLinker::LookupSymbol(const std::string& symbolName) const
{
     if (auto it = this->_exports.find(symbolName); it != this->_exports.end())
     {
          return it->second; // already absolute RVA
     }

     for (const auto& [dll, symbols] : this->_imports)
     {
          for (std::size_t i = 0; i < symbols.size(); ++i)
          {
               if (symbols[i] == symbolName)
               {
                    std::uint32_t iatBase = 0x202a; // example IAT base RVA
                    return iatBase + static_cast<std::uint32_t>(i * sizeof(std::uint64_t));
               }
          }
     }

     throw nullptr; // std::runtime_error("Symbol not found: " + symbolName);
}


ecpps::linker::win::PEImage ecpps::linker::win::WindowsLinker::Link(const std::uint32_t entryPoint) const
{
     PEImage output{this->_imageBase, entryPoint,      this->_isNxCompatible, this->_isRelocatable,
                    this->_subsystem, this->_linkType, this->_fileAlignment};
     output.sections = this->_sections;
     output.exports = this->_exports;
     output.imports = this->_imports;

     return output;
}
