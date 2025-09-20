#include "PE.h"
#include <Windows.h>
#include <corecrt.h>
#include <concepts>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <map>

template <std::integral T> constexpr static T AlignUp(const T value, const T alignment)
{
     return (value + alignment - 1) & ~(alignment - 1);
}

ecpps::linker::win::PEImage::PEImage(std::uintptr_t imageBase, std::uint32_t entryPoint, bool isNxCompatible,
                                     bool isRelocatable, PESubsystem subsystem, LinkType linkType,
                                     std::uint32_t fileAlignment)
{
     this->_dosHeader.e_magic = DosMagic;
     this->_dosHeader.e_cblp = 0x90;
     this->_dosHeader.e_cp = 3;
     this->_dosHeader.e_cparhdr = 4;
     this->_dosHeader.e_lfarlc = 0x40;
     this->_dosHeader.e_sp = 0xb8;
     this->_dosHeader.e_maxalloc = 0xFFFF;
     this->_dosHeader.e_lfanew = 0xe8;

     this->_ntHeaders.signature = PeMagic;
     this->_ntHeaders.fileHeader.machine = IMAGE_FILE_MACHINE_AMD64;
     this->_ntHeaders.fileHeader.sizeOfOptionalHeader = sizeof(OptionalHeader);
     this->_ntHeaders.fileHeader.characteristics =
         (linkType == LinkType::Executable ? IMAGE_FILE_EXECUTABLE_IMAGE
                                           : (IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL)) |
         IMAGE_FILE_LARGE_ADDRESS_AWARE;
     const std::time_t currentTime = std::time(nullptr);
     this->_ntHeaders.fileHeader.timeDateStamp = static_cast<std::uint32_t>(currentTime);

     this->_ntHeaders.optionalHeader.subsystem = static_cast<std::uint16_t>(subsystem);
     this->_ntHeaders.optionalHeader.majorSubsystemVersion = 6;
     this->_ntHeaders.optionalHeader.minorSubsystemVersion = 0;
     this->_ntHeaders.optionalHeader.fileAlignment = fileAlignment;
     this->_ntHeaders.optionalHeader.magic = MagicPE32Plus;
     this->_ntHeaders.optionalHeader.majorLinkerVersion = 1;
     this->_ntHeaders.optionalHeader.minorLinkerVersion = 0;
     this->_ntHeaders.optionalHeader.majorOperatingSystemVersion = 6;
     this->_ntHeaders.optionalHeader.minorOperatingSystemVersion = 0;
     this->_ntHeaders.optionalHeader.dllCharacteristics = IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA |
                                                          (isNxCompatible ? IMAGE_DLLCHARACTERISTICS_NX_COMPAT : 0) |
                                                          (isRelocatable ? IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE : 0);
     this->_ntHeaders.optionalHeader.loaderFlags = 0;
     this->_ntHeaders.optionalHeader.imageBase = imageBase;
     this->_ntHeaders.optionalHeader.sectionAlignment = 0x1000;
     this->_ntHeaders.optionalHeader.sizeOfHeaders = sizeof(NtHeaders) + sizeof(DosHeader);
     this->_ntHeaders.optionalHeader.addressOfEntryPoint = entryPoint;
}
std::vector<std::byte> ecpps::linker::win::PEImage::toBytes(const std::string& imageName)
{
     // Add export directory
     IMAGE_EXPORT_DIRECTORY exportDirectory{};
     exportDirectory.Characteristics = 0;
     exportDirectory.TimeDateStamp = static_cast<DWORD>(std::time(nullptr));
     exportDirectory.MajorVersion = 1;
     exportDirectory.MinorVersion = 0;
     exportDirectory.Base = 1; // Start ordinals from 1
     exportDirectory.NumberOfFunctions = static_cast<std::uint32_t>(this->exports.size());
     exportDirectory.NumberOfNames = static_cast<std::uint32_t>(this->exports.size());

     // Calculate aligned offsets
     const std::uint32_t sectionAlignment = this->_ntHeaders.optionalHeader.sectionAlignment;

     std::vector<std::byte> exportData{};

     constexpr std::uint32_t exportRVA = AlignUp(0x2000, 0x1000); // Aligned RVA for export section
     std::uint32_t currentOffset = exportRVA + sizeof(exportDirectory);
     const std::string dllName = imageName;
     exportData.resize(dllName.size() + 1);
     std::memcpy(exportData.data(), dllName.c_str(), dllName.size() + 1);
     exportDirectory.Name = currentOffset; // Typically 0 for no name in this case

     currentOffset += AlignUp<std::uint32_t>(static_cast<std::uint32_t>(dllName.size() + 1), 1);

     // Calculate RVAs for the function addresses, names, and ordinals
     exportDirectory.AddressOfFunctions = currentOffset;
     currentOffset +=
         AlignUp<std::uint32_t>(static_cast<std::uint32_t>(this->exports.size() * sizeof(std::uint32_t)), 1);

     exportDirectory.AddressOfNames = currentOffset;
     currentOffset +=
         AlignUp<std::uint32_t>(static_cast<std::uint32_t>(this->exports.size() * sizeof(std::uint32_t)), 1);

     exportDirectory.AddressOfNameOrdinals = currentOffset; // Ensure AddressOfNameOrdinals is set
     currentOffset +=
         AlignUp<std::uint32_t>(static_cast<std::uint32_t>(this->exports.size() * sizeof(std::uint16_t)), 1);

     int offset = 0;
     // Serialise function addresses
     for (auto& [name, addr] : this->exports)
     {
          std::size_t insertPos = (static_cast<std::size_t>(exportDirectory.AddressOfFunctions) - exportRVA) + offset;
          if (insertPos + sizeof(std::uint32_t) > exportData.size())
          {
               exportData.resize(insertPos + sizeof(std::uint32_t));
          }

          std::uint32_t* pRVA = reinterpret_cast<std::uint32_t*>(exportData.data() + offset +
                                                                 (exportDirectory.AddressOfFunctions - exportRVA));
          *pRVA = addr;
          addr = currentOffset;
          currentOffset += static_cast<std::uint32_t>(name.size() + 1);
          offset += sizeof(std::uint32_t);
     }

     // Serialise function names (null-terminated strings)
     offset = 0;
     for (const auto& [name, address] : this->exports)
     {
          const std::size_t insertPos = max(address - exportRVA, offset + exportDirectory.AddressOfNames - exportRVA);
          if (insertPos + static_cast<std::size_t>(name.size() + 1) > exportData.size())
          {
               exportData.resize(insertPos + static_cast<std::size_t>(name.size() + 1));
          }

          std::uint32_t* pRVA = reinterpret_cast<std::uint32_t*>(exportData.data() + offset +
                                                                 (exportDirectory.AddressOfNames - exportRVA));
          *pRVA = address;
          std::memcpy(exportData.data() + address - exportRVA, name.c_str(), name.size() + 1);
          offset += sizeof(std::uint32_t);
     }

     offset = 0;
     // Serialise ordinals (start from 1)
     for (size_t i = 0; i < this->exports.size(); ++i)
     {
          const size_t insertPos = (exportDirectory.AddressOfNameOrdinals - exportRVA) + offset;

          // Ensure the size of exportData is sufficient to hold the ordinals
          if (insertPos + sizeof(std::uint16_t) > exportData.size())
          {
               exportData.resize(insertPos + sizeof(std::uint16_t));
          }

          // Write the ordinal (1-based index)
          std::uint16_t* pOrdinal = reinterpret_cast<std::uint16_t*>(exportData.data() + insertPos);
          *pOrdinal = static_cast<std::uint16_t>(i); // Ordinals are 1-based
          offset += sizeof(std::uint16_t);
     }

     std::memcpy(exportData.data(), &exportDirectory, sizeof(exportDirectory));

     this->sections.emplace_back(".edata", exportData, PESectionFlags::Read | PESectionFlags::InitialisedData, 0, 0);

     std::uint32_t exportSize = currentOffset - exportRVA;

     const auto importRVA = AlignUp<std::uint32_t>(currentOffset, 0x1000);
     std::vector<std::byte> importData{};
     std::size_t descriptorOffset{};
     importData.resize((this->imports.size() + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR));

     struct ImportLayout
     {
          std::uint32_t originalThunkRVA{};
          std::uint32_t firstThunkRVA{};
          std::uint32_t nameRVA{};
          std::vector<std::pair<std::string, std::uint32_t>> thunkNameRVAs{};
          std::string dllName;
     };

     std::vector<ImportLayout> layouts;
     std::unordered_map<std::size_t, std::string> importNameTable;

     std::size_t currentDataOffset = (this->imports.size() + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR);

     for (const auto& [dll, functions] : this->imports)
     {
          ImportLayout layout{};
          layout.dllName = dll;
          std::vector<std::uint32_t> thunkRVAs;

          // For each symbol, write IMAGE_IMPORT_BY_NAME and save RVA
          for (const auto& func : functions)
          {
               const std::size_t nameOffset = currentDataOffset;
               importData.resize(nameOffset + 2 + func.size() + 1);
               *reinterpret_cast<std::uint16_t*>(importData.data() + nameOffset) = 0; // Hint
               std::memcpy(importData.data() + nameOffset + 2, func.c_str(), func.size() + 1);
               const std::uint32_t nameRVA = importRVA + static_cast<std::uint32_t>(nameOffset);
               thunkRVAs.push_back(nameRVA);
               currentDataOffset = importData.size();
          }

          // Write OriginalFirstThunk array (INT)
          const std::size_t originalThunkOffset = currentDataOffset;
          for (std::uint32_t rva : thunkRVAs)
          {
               importData.resize(currentDataOffset + sizeof(std::uint32_t));
               *reinterpret_cast<std::uint32_t*>(importData.data() + currentDataOffset) = rva;
               currentDataOffset += sizeof(std::uint32_t);
          }
          // Null terminator for INT
          importData.resize(currentDataOffset + sizeof(std::uint32_t));
          *reinterpret_cast<std::uint32_t*>(importData.data() + currentDataOffset) = 0;
          currentDataOffset += sizeof(std::uint32_t);

          // Write FirstThunk array (IAT)
          const std::size_t firstThunkOffset = currentDataOffset;
          for (std::uint32_t rva : thunkRVAs)
          {
               importData.resize(currentDataOffset + sizeof(std::uint32_t));
               *reinterpret_cast<std::uint32_t*>(importData.data() + currentDataOffset) = rva;
               currentDataOffset += sizeof(std::uint32_t);
          }
          // Null terminator for IAT
          importData.resize(currentDataOffset + sizeof(std::uint32_t));
          *reinterpret_cast<std::uint32_t*>(importData.data() + currentDataOffset) = 0;
          currentDataOffset += sizeof(std::uint32_t);

          // Write DLL name
          const std::size_t nameOffset = currentDataOffset;
          layout.nameRVA = importRVA + static_cast<std::uint32_t>(nameOffset);
          importData.resize(nameOffset + dll.size() + 1);
          std::memcpy(importData.data() + nameOffset, dll.c_str(), dll.size());
          importData[nameOffset + dll.size()] = std::byte{ 0 };
          currentDataOffset = importData.size();

          layout.originalThunkRVA = importRVA + static_cast<std::uint32_t>(originalThunkOffset);
          layout.firstThunkRVA = importRVA + static_cast<std::uint32_t>(firstThunkOffset);

          layouts.push_back(layout);
     }

 /*    for (const auto& [nameOffset, name] : importNameTable)
     {
          const auto offset = importData.size();

          *std::launder(reinterpret_cast<std::uint32_t*>(importData.data() + nameOffset)) = importRVA + offset;
          importData.resize(offset + name.size() + 1);
          std::memcpy(importData.data() + offset, name.c_str(), name.size() + 1);
     }*/

     // Now write IMAGE_IMPORT_DESCRIPTORs
     for (std::size_t i = 0; i < layouts.size(); ++i)
     {
          auto& descriptor = *std::launder(
               reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(importData.data() + i * sizeof(IMAGE_IMPORT_DESCRIPTOR))
          );

          descriptor.OriginalFirstThunk = layouts[i].originalThunkRVA;
          descriptor.FirstThunk = layouts[i].firstThunkRVA;
          descriptor.Name = layouts[i].nameRVA;
     }
     std::memset(importData.data() + layouts.size() * sizeof(IMAGE_IMPORT_DESCRIPTOR), 0, sizeof(IMAGE_IMPORT_DESCRIPTOR));


     // Null descriptor
     importData.resize(importData.size() + sizeof(IMAGE_IMPORT_DESCRIPTOR), std::byte{});
     const auto importSize = static_cast<std::uint32_t>(importData.size());

     this->sections.emplace_back(".idata", importData,
                                 PESectionFlags::Read | PESectionFlags::InitialisedData, 0, 0);

     // Update data directory with export info
     this->_ntHeaders.optionalHeader.numberOfRvaAndSizes = 2;
     this->_ntHeaders.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] =
         DataDirectory{.VirtualAddress = exportRVA, .Size = exportSize};  
     this->_ntHeaders.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] =
         DataDirectory{.VirtualAddress = importRVA, .Size = importSize };

     this->_ntHeaders.optionalHeader.sizeOfImage = AlignUp(exportRVA + exportSize + importSize, sectionAlignment);

     // Headers and section layout
     constexpr std::uint32_t ntHeadersOffset = AlignUp(sizeof(DosHeader), sizeof(std::uint32_t));
     constexpr std::uint32_t sectionHeadersOffset = ntHeadersOffset + sizeof(NtHeaders);
     const std::uint32_t headersSize =
         AlignUp(sectionHeadersOffset + static_cast<std::uint32_t>(this->sections.size() * sizeof(SectionHeader)),
                 this->_ntHeaders.optionalHeader.fileAlignment);

     this->_ntHeaders.fileHeader.numberOfSections = static_cast<std::uint16_t>(this->sections.size());

     uint32_t currentVirtualAddress = AlignUp(headersSize, this->_ntHeaders.optionalHeader.sectionAlignment);

     // Section header details
     for (auto& section : this->sections)
     {
          section.virtualAddress = currentVirtualAddress;
          currentVirtualAddress =
              AlignUp(currentVirtualAddress + AlignUp(static_cast<std::uint32_t>(section.data.size()),
                                                      this->_ntHeaders.optionalHeader.sectionAlignment),
                      this->_ntHeaders.optionalHeader.sectionAlignment);
     }

     std::uint32_t fileSize = headersSize;
     std::uint32_t imageSize = headersSize;

     // Update section raw data and image size
     for (auto& section : this->sections)
     {
          section.pointerToRawData = fileSize;
          fileSize = AlignUp(fileSize + static_cast<std::uint32_t>(section.data.size()),
                             this->_ntHeaders.optionalHeader.fileAlignment);
          imageSize += AlignUp(static_cast<std::uint32_t>(section.data.size()),
                               this->_ntHeaders.optionalHeader.sectionAlignment);
     }

     this->_ntHeaders.optionalHeader.sizeOfImage = imageSize;

     std::vector<std::byte> binary(fileSize, std::byte{0});

     // Copy DOS header
     memcpy(binary.data(), &this->_dosHeader, sizeof(DosHeader));
     auto dosHeaderPtr = reinterpret_cast<DosHeader*>(binary.data());
     dosHeaderPtr->e_lfanew = ntHeadersOffset;

     // Copy NT headers
     auto ntHeadersPtr = reinterpret_cast<NtHeaders*>(binary.data() + ntHeadersOffset);
     memcpy(ntHeadersPtr, &this->_ntHeaders, sizeof(NtHeaders));

     // Copy section headers
     auto sectionHeadersPtr = reinterpret_cast<SectionHeader*>(binary.data() + sectionHeadersOffset);
     for (const auto& section : this->sections)
     {
          memcpy(sectionHeadersPtr->Name, section.name.c_str(),
                 min(section.name.size(), sizeof(sectionHeadersPtr->Name)));
          sectionHeadersPtr->VirtualAddress =
              AlignUp(section.virtualAddress, this->_ntHeaders.optionalHeader.sectionAlignment);
          sectionHeadersPtr->PointerToRawData = section.pointerToRawData;
          sectionHeadersPtr->SizeOfRawData = static_cast<std::uint32_t>(section.data.size());
          sectionHeadersPtr->VirtualSize =
              AlignUp(static_cast<std::uint32_t>(section.data.size()), this->_ntHeaders.optionalHeader.fileAlignment);
          sectionHeadersPtr->Characteristics = static_cast<std::uint32_t>(section.flags);
          ++sectionHeadersPtr;
     }

     // Copy section data
     for (const auto& section : this->sections)
     {
          if (!section.data.empty())
          {
               auto sectionDataPtr = binary.data() + section.pointerToRawData;
               memcpy(sectionDataPtr, section.data.data(), section.data.size());
          }
     }

     return binary;
}