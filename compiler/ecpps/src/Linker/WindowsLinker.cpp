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

void ecpps::linker::win::WindowsLinker::CodeSection(const std::vector<std::byte>& data)
{
     PESection textSection{};

     textSection.name = CodeSectionName;
     textSection.flags = PESectionFlags::Read | PESectionFlags::Execute | PESectionFlags::ExecutableCode;
     textSection.data = data;

     this->AddSection(textSection);
}

void ecpps::linker::win::WindowsLinker::ExportFunction(const std::string& name, std::uint32_t address)
{
     this->ExportAt(name, ExportDisplacement + address);
}

void ecpps::linker::win::WindowsLinker::AddSection(const PESection& value) { this->_sections.emplace_back(value); }

void ecpps::linker::win::WindowsLinker::ExportAt(const std::string& name, std::uint32_t address)
{
     this->_exports.emplace(name, address);
}

void ecpps::linker::win::WindowsLinker::ImportAt(std::uint32_t address, const std::string& name)
{
     this->_imports.emplace(address, name);
}

std::vector<std::byte> ecpps::linker::win::WindowsLinker::ToBytes(const std::string& imageName,
                                                                  const std::size_t entryPointAddress) const
{
     return this->Link(entryPointAddress + ExportDisplacement).toBytes(imageName);
}

ecpps::linker::win::PEImage ecpps::linker::win::WindowsLinker::Link(const std::uint32_t entryPoint) const
{
     PEImage output{this->_imageBase, entryPoint,      this->_isNxCompatible, this->_isRelocatable,
                    this->_subsystem, this->_linkType, this->_fileAlignment};
     output.sections = this->_sections;
     output.exports = this->_exports;

     return output;
}
