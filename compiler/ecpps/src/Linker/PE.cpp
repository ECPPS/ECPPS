#include "PE.h"
#include <Windows.h>
#include <corecrt.h>
#include <concepts>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>
#include <ranges>
#include <algorithm>

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

template <typename T = decltype([] {})> struct [[nodiscard]] Defer : T
{
     ~Defer(void) noexcept(noexcept(T::operator())) { (*this)(); }
};

auto AppendBytes(std::vector<std::byte>& destination, const void* data, std::size_t size) -> void
{
     const auto* first = static_cast<const std::byte*>(data);
     destination.insert(destination.end(), first, first + size);
}

auto AppendStringAsBytes(std::vector<std::byte>& destination, std::string_view string) -> void
{
     for (const char character : string) destination.push_back(std::byte{static_cast<unsigned char>(character)});
}
static std::vector<std::byte> BuildIdataBuffer(std::uint32_t idataVA,
                                               const std::unordered_map<std::string, std::vector<std::string>>& imports)
{
     const size_t descriptorCount = imports.size() + 1;
     const size_t descriptorSize = sizeof(IMAGE_IMPORT_DESCRIPTOR);
     const size_t thunkSize = sizeof(std::uint64_t);

     // Count total thunks
     size_t totalThunks = 0;
     for (auto& kv : imports) totalThunks += kv.second.size() + 1;

     // Calculate offsets
     const size_t intOffset = AlignUp(descriptorCount * descriptorSize, 8uz);
     const size_t iatOffset = intOffset + totalThunks * thunkSize;
     size_t nameOffset = AlignUp(iatOffset + totalThunks * thunkSize, 2uz);

     std::vector<std::byte> buffer(nameOffset); // preallocate for descriptors + INT + IAT
     size_t iltPtr = intOffset;
     size_t iatPtr = iatOffset;
     size_t currentNameOffset = nameOffset;
     size_t descIndex = 0;

     auto ensureSize = [&](size_t size)
     {
          if (buffer.size() < size) buffer.resize(size);
     };
     auto write64 = [&](size_t off, std::uint64_t val)
     {
          ensureSize(off + 8);
          std::memcpy(buffer.data() + off, &val, 8);
     };
     auto write16 = [&](size_t off, std::uint16_t val)
     {
          ensureSize(off + 2);
          std::memcpy(buffer.data() + off, &val, 2);
     };
     auto writeString = [&](size_t off, std::string_view s)
     {
          ensureSize(off + s.size() + 1);
          std::memcpy(buffer.data() + off, s.data(), s.size());
          buffer[off + s.size()] = std::byte{0};
     };

     for (auto& [dll, funcs] : imports)
     {
          // Descriptor
          size_t descPos = descIndex * descriptorSize;
          IMAGE_IMPORT_DESCRIPTOR desc{};
          desc.OriginalFirstThunk = idataVA + static_cast<uint32_t>(iltPtr);
          desc.FirstThunk = idataVA + static_cast<uint32_t>(iatPtr);

          // Function thunks
          for (auto& f : funcs)
          {
               const std::string funcName = f.empty() ? "Unknown" : f;
               const uint32_t ibnRVA = idataVA + static_cast<uint32_t>(currentNameOffset);

               write64(iltPtr, ibnRVA);
               iltPtr += thunkSize;
               write64(iatPtr, ibnRVA);
               iatPtr += thunkSize;

               // IMAGE_IMPORT_BY_NAME: Hint + Name + Null
               write16(currentNameOffset, 0); // Hint
               currentNameOffset += 2;
               writeString(currentNameOffset, funcName);
               currentNameOffset += funcName.size() + 1;
               currentNameOffset = AlignUp(currentNameOffset, 2uz);
          }

          write64(iltPtr, 0);
          iltPtr += thunkSize;
          write64(iatPtr, 0);
          iatPtr += thunkSize;

          // DLL name
          const std::string dllName = dll.empty() ? "dummy.dll" : dll;
          const uint32_t dllRVA = idataVA + static_cast<uint32_t>(currentNameOffset);
          writeString(currentNameOffset, dllName);
          currentNameOffset = AlignUp(currentNameOffset + dllName.size() + 1, 2uz);

          desc.Name = dllRVA;
          ensureSize(descPos + descriptorSize);
          std::memcpy(buffer.data() + descPos, &desc, descriptorSize);

          ++descIndex;
     }

     // Final zero descriptor
     IMAGE_IMPORT_DESCRIPTOR zero{};
     ensureSize(descIndex * descriptorSize + descriptorSize);
     std::memcpy(buffer.data() + descIndex * descriptorSize, &zero, descriptorSize);

     // Resize buffer to actual used size
     buffer.resize(currentNameOffset);

     return buffer;
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
     for (size_t i = 0; i < this->exports.size(); ++i)
     {
          const size_t insertPos = (exportDirectory.AddressOfNameOrdinals - exportRVA) + offset;

          if (insertPos + sizeof(std::uint16_t) > exportData.size())
          {
               exportData.resize(insertPos + sizeof(std::uint16_t));
          }

          std::uint16_t* pOrdinal = reinterpret_cast<std::uint16_t*>(exportData.data() + insertPos);
          *pOrdinal = static_cast<std::uint16_t>(i); // Ordinals are 1-based
          offset += sizeof(std::uint16_t);
     }

     std::memcpy(exportData.data(), &exportDirectory, sizeof(exportDirectory));

     this->sections.emplace_back(".edata", exportData, PESectionFlags::Read | PESectionFlags::InitialisedData, 0, 0);

     std::uint32_t exportSize = currentOffset - exportRVA;

// Section alignment
     const uint32_t sectionAlign = this->_ntHeaders.optionalHeader.sectionAlignment;
     uint32_t currentRVA = AlignUp(exportRVA + exportSize, sectionAlign);

     // .idata section
     const uint32_t importRVA = AlignUp(currentRVA, sectionAlign);
     auto idataBuffer = BuildIdataBuffer(importRVA, this->imports);

     // Compute IAT RVA inside .idata
     size_t descriptorCount = this->imports.size() + 1;
     size_t totalThunkEntries = 0;
     for (auto& kv : this->imports) totalThunkEntries += kv.second.size() + 1;
     size_t intOffset = AlignUp(descriptorCount * sizeof(IMAGE_IMPORT_DESCRIPTOR), 8uz);
     const uint32_t iatRVA = importRVA + static_cast<uint32_t>(intOffset + totalThunkEntries * sizeof(uint64_t));

     // Add .idata section
     this->sections.emplace_back(".idata", idataBuffer, PESectionFlags::Read | PESectionFlags::InitialisedData, 0, 0);

     // Update directories
     this->_ntHeaders.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] =
         DataDirectory{.VirtualAddress = importRVA, .Size = static_cast<std::uint32_t>(idataBuffer.size())};

     this->_ntHeaders.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_IAT] = DataDirectory{
         .VirtualAddress = iatRVA, .Size = static_cast<std::uint32_t>(totalThunkEntries * sizeof(uint64_t))};

     // Update sizeOfImage
     currentRVA = importRVA + static_cast<std::uint32_t>(idataBuffer.size());
     this->_ntHeaders.optionalHeader.sizeOfImage = AlignUp(currentRVA, sectionAlign);


     this->_ntHeaders.optionalHeader.numberOfRvaAndSizes = 14;
     this->_ntHeaders.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] =
         DataDirectory{.VirtualAddress = exportRVA, .Size = exportSize};

     //this->_ntHeaders.optionalHeader.sizeOfImage =
     //    AlignUp(exportRVA + exportSize + static_cast<std::uint32_t>(idataBuf.size()), sectionAlignment);

     // Headers and section layout
     constexpr std::uint32_t ntHeadersOffset = AlignUp(sizeof(DosHeader), sizeof(std::uint32_t));
     constexpr std::uint32_t sectionHeadersOffset = ntHeadersOffset + sizeof(NtHeaders);
     const std::uint32_t headersSize =
         AlignUp(sectionHeadersOffset + static_cast<std::uint32_t>(this->sections.size() * sizeof(SectionHeader)),
                 this->_ntHeaders.optionalHeader.fileAlignment);

     this->_ntHeaders.optionalHeader.sizeOfInitializedData = static_cast<std::uint32_t>(idataBuffer.size());
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