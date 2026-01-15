#include "PE.h"
#include <corecrt.h>
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <ranges>
#include <string>
#include <vector>

#define IMAGE_SIZEOF_FILE_HEADER 20

#define IMAGE_FILE_RELOCS_STRIPPED 0x0001         // Relocation info stripped from file.
#define IMAGE_FILE_EXECUTABLE_IMAGE 0x0002        // File is executable  (i.e. no unresolved external references).
#define IMAGE_FILE_LINE_NUMS_STRIPPED 0x0004      // Line nunbers stripped from file.
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED 0x0008     // Local symbols stripped from file.
#define IMAGE_FILE_AGGRESIVE_WS_TRIM 0x0010       // Aggressively trim working set
#define IMAGE_FILE_LARGE_ADDRESS_AWARE 0x0020     // App can handle >2gb addresses
#define IMAGE_FILE_BYTES_REVERSED_LO 0x0080       // Bytes of machine word are reversed.
#define IMAGE_FILE_32BIT_MACHINE 0x0100           // 32 bit word machine.
#define IMAGE_FILE_DEBUG_STRIPPED 0x0200          // Debugging info stripped from file in .DBG file
#define IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP 0x0400 // If Image is on removable media, copy and run from the swap file.
#define IMAGE_FILE_NET_RUN_FROM_SWAP 0x0800       // If Image is on Net, copy and run from the swap file.
#define IMAGE_FILE_SYSTEM 0x1000                  // System File.
#define IMAGE_FILE_DLL 0x2000                     // File is a DLL.
#define IMAGE_FILE_UP_SYSTEM_ONLY 0x4000          // File should only be run on a UP machine
#define IMAGE_FILE_BYTES_REVERSED_HI 0x8000       // Bytes of machine word are reversed.

#define IMAGE_FILE_MACHINE_UNKNOWN 0
#define IMAGE_FILE_MACHINE_TARGET_HOST                                                                                 \
     0x0001                              // Useful for indicating we want to interact with the host and not a WoW guest.
#define IMAGE_FILE_MACHINE_I386 0x014c   // Intel 386.
#define IMAGE_FILE_MACHINE_R3000 0x0162  // MIPS little-endian, 0x160 big-endian
#define IMAGE_FILE_MACHINE_R4000 0x0166  // MIPS little-endian
#define IMAGE_FILE_MACHINE_R10000 0x0168 // MIPS little-endian
#define IMAGE_FILE_MACHINE_WCEMIPSV2 0x0169 // MIPS little-endian WCE v2
#define IMAGE_FILE_MACHINE_ALPHA 0x0184     // Alpha_AXP
#define IMAGE_FILE_MACHINE_SH3 0x01a2       // SH3 little-endian
#define IMAGE_FILE_MACHINE_SH3DSP 0x01a3
#define IMAGE_FILE_MACHINE_SH3E 0x01a4  // SH3E little-endian
#define IMAGE_FILE_MACHINE_SH4 0x01a6   // SH4 little-endian
#define IMAGE_FILE_MACHINE_SH5 0x01a8   // SH5
#define IMAGE_FILE_MACHINE_ARM 0x01c0   // ARM Little-Endian
#define IMAGE_FILE_MACHINE_THUMB 0x01c2 // ARM Thumb/Thumb-2 Little-Endian
#define IMAGE_FILE_MACHINE_ARMNT 0x01c4 // ARM Thumb-2 Little-Endian
#define IMAGE_FILE_MACHINE_AM33 0x01d3
#define IMAGE_FILE_MACHINE_POWERPC 0x01F0 // IBM PowerPC Little-Endian
#define IMAGE_FILE_MACHINE_POWERPCFP 0x01f1
#define IMAGE_FILE_MACHINE_IA64 0x0200      // Intel 64
#define IMAGE_FILE_MACHINE_MIPS16 0x0266    // MIPS
#define IMAGE_FILE_MACHINE_ALPHA64 0x0284   // ALPHA64
#define IMAGE_FILE_MACHINE_MIPSFPU 0x0366   // MIPS
#define IMAGE_FILE_MACHINE_MIPSFPU16 0x0466 // MIPS
#define IMAGE_FILE_MACHINE_AXP64 IMAGE_FILE_MACHINE_ALPHA64
#define IMAGE_FILE_MACHINE_TRICORE 0x0520 // Infineon
#define IMAGE_FILE_MACHINE_CEF 0x0CEF
#define IMAGE_FILE_MACHINE_EBC 0x0EBC   // EFI Byte Code
#define IMAGE_FILE_MACHINE_AMD64 0x8664 // AMD64 (K8)
#define IMAGE_FILE_MACHINE_M32R 0x9041  // M32R little-endian
#define IMAGE_FILE_MACHINE_ARM64 0xAA64 // ARM64 Little-Endian
#define IMAGE_FILE_MACHINE_CEE 0xC0EE

#ifdef min
#undef min
#undef max
#endif

template <std::integral T> constexpr static T AlignUp(const T value, const T alignment)
{
     return (value + alignment - 1) & ~(alignment - 1);
}

ecpps::linker::win::PEImage::PEImage(std::uintptr_t imageBase, std::uint32_t entryPoint, bool isNxCompatible,
                                     bool isRelocatable, PESubsystem subsystem, LinkType linkType,
                                     std::uint32_t fileAlignment)
{
     this->dosHeader.e_magic = DosMagic;
     this->dosHeader.e_cblp = 0x90;
     this->dosHeader.e_cp = 3;
     this->dosHeader.e_cparhdr = 4;
     this->dosHeader.e_lfarlc = 0x40;
     this->dosHeader.e_sp = 0xb8;
     this->dosHeader.e_maxalloc = 0xFFFF;
     this->dosHeader.e_lfanew = 0xe8;

     this->ntHeaders.signature = PeMagic;
     this->ntHeaders.fileHeader.machine = IMAGE_FILE_MACHINE_AMD64;
     this->ntHeaders.fileHeader.sizeOfOptionalHeader = sizeof(OptionalHeader);
     this->ntHeaders.fileHeader.characteristics =
         (linkType == LinkType::Executable ? IMAGE_FILE_EXECUTABLE_IMAGE
                                           : (IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL)) |
         IMAGE_FILE_LARGE_ADDRESS_AWARE;
     const std::time_t currentTime = std::time(nullptr);
     this->ntHeaders.fileHeader.timeDateStamp = static_cast<std::uint32_t>(currentTime);

     this->ntHeaders.optionalHeader.subsystem = static_cast<std::uint16_t>(subsystem);
     this->ntHeaders.optionalHeader.majorSubsystemVersion = 6;
     this->ntHeaders.optionalHeader.minorSubsystemVersion = 0;
     this->ntHeaders.optionalHeader.fileAlignment = fileAlignment;
     this->ntHeaders.optionalHeader.magic = MagicPE32Plus;
     this->ntHeaders.optionalHeader.majorLinkerVersion = 1;
     this->ntHeaders.optionalHeader.minorLinkerVersion = 0;
     this->ntHeaders.optionalHeader.majorOperatingSystemVersion = 6;
     this->ntHeaders.optionalHeader.minorOperatingSystemVersion = 0;
     this->ntHeaders.optionalHeader.dllCharacteristics = IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA |
                                                         (isNxCompatible ? IMAGE_DLLCHARACTERISTICS_NX_COMPAT : 0) |
                                                         (isRelocatable ? IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE : 0);
     this->ntHeaders.optionalHeader.loaderFlags = 0;
     this->ntHeaders.optionalHeader.imageBase = imageBase;
     this->ntHeaders.optionalHeader.sectionAlignment = 0x1000;
     this->ntHeaders.optionalHeader.sizeOfHeaders = sizeof(NtHeaders) + sizeof(DosHeader);
     this->ntHeaders.optionalHeader.addressOfEntryPoint = entryPoint;
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

     size_t totalThunks = 0;
     for (const auto& kv : imports) totalThunks += kv.second.size() + 1;

     const size_t intOffset = AlignUp(descriptorCount * descriptorSize, 8uz);
     const size_t iatOffset = intOffset + (totalThunks * thunkSize);
     size_t nameOffset = AlignUp(iatOffset + (totalThunks * thunkSize), 2uz);

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

     for (const auto& [dll, funcs] : imports)
     {
          // Descriptor
          size_t descPos = descIndex * descriptorSize;
          IMAGE_IMPORT_DESCRIPTOR desc{};
          desc.OriginalFirstThunk = idataVA + static_cast<uint32_t>(iltPtr);
          desc.FirstThunk = idataVA + static_cast<uint32_t>(iatPtr);

          // Function thunks
          for (const auto& f : funcs)
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

     IMAGE_IMPORT_DESCRIPTOR zero{};
     ensureSize((descIndex * descriptorSize) + descriptorSize);
     std::memcpy(buffer.data() + (descIndex * descriptorSize), &zero, descriptorSize);

     buffer.resize(currentNameOffset);

     return buffer;
}

std::vector<std::byte> ecpps::linker::win::PEImage::ToBytes(const std::string& imageName)
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
     const std::uint32_t sectionAlignment = this->ntHeaders.optionalHeader.sectionAlignment;

     std::vector<std::byte> exportData{};

     constexpr std::uint32_t exportRVA = AlignUp(0x2000, 0x1000); // Aligned RVA for export section
     std::uint32_t currentOffset = exportRVA + sizeof(exportDirectory);
     const std::string& dllName = imageName;
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
          const std::size_t insertPos =
              std::max<std::size_t>(address - exportRVA, offset + exportDirectory.AddressOfNames - exportRVA);
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
     const uint32_t sectionAlign = this->ntHeaders.optionalHeader.sectionAlignment;
     uint32_t currentRVA = AlignUp(exportRVA + exportSize, sectionAlign);

     // .idata section
     const uint32_t importRVA = AlignUp(currentRVA, sectionAlign);
     auto idataBuffer = BuildIdataBuffer(importRVA, this->imports);

     // Compute IAT RVA inside .idata
     size_t descriptorCount = this->imports.size() + 1;
     size_t totalThunkEntries = 0;
     for (auto& kv : this->imports) totalThunkEntries += kv.second.size() + 1;
     size_t intOffset = AlignUp(descriptorCount * sizeof(IMAGE_IMPORT_DESCRIPTOR), 8uz);
     const uint32_t iatRVA = importRVA + static_cast<uint32_t>(intOffset + (totalThunkEntries * sizeof(std::uint64_t)));

     // Add .idata section
     this->sections.emplace_back(".idata", idataBuffer, PESectionFlags::Read | PESectionFlags::InitialisedData, 0, 0);

     // Update directories
     this->ntHeaders.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] =
         DataDirectory{.VirtualAddress = importRVA, .Size = static_cast<std::uint32_t>(idataBuffer.size())};

     this->ntHeaders.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_IAT] = DataDirectory{
         .VirtualAddress = iatRVA, .Size = static_cast<std::uint32_t>(totalThunkEntries * sizeof(uint64_t))};

     // Update sizeOfImage
     currentRVA = importRVA + static_cast<std::uint32_t>(idataBuffer.size());
     this->ntHeaders.optionalHeader.sizeOfImage = AlignUp(currentRVA, sectionAlign);

     this->ntHeaders.optionalHeader.numberOfRvaAndSizes = 14;
     this->ntHeaders.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] =
         DataDirectory{.VirtualAddress = exportRVA, .Size = exportSize};

     // this->ntHeaders.optionalHeader.sizeOfImage =
     //     AlignUp(exportRVA + exportSize + static_cast<std::uint32_t>(idataBuf.size()), sectionAlignment);

     // Headers and section layout
     constexpr std::uint32_t ntHeadersOffset = AlignUp(sizeof(DosHeader), sizeof(std::uint32_t));
     constexpr std::uint32_t sectionHeadersOffset = ntHeadersOffset + sizeof(NtHeaders);
     const std::uint32_t headersSize =
         AlignUp(sectionHeadersOffset + static_cast<std::uint32_t>(this->sections.size() * sizeof(SectionHeader)),
                 this->ntHeaders.optionalHeader.fileAlignment);

     this->ntHeaders.optionalHeader.sizeOfInitializedData = static_cast<std::uint32_t>(idataBuffer.size());
     this->ntHeaders.fileHeader.numberOfSections = static_cast<std::uint16_t>(this->sections.size());

     uint32_t currentVirtualAddress = AlignUp(headersSize, this->ntHeaders.optionalHeader.sectionAlignment);

     // Section header details
     for (auto& section : this->sections)
     {
          section.virtualAddress = currentVirtualAddress;
          currentVirtualAddress =
              AlignUp(currentVirtualAddress + AlignUp(static_cast<std::uint32_t>(section.data.size()),
                                                      this->ntHeaders.optionalHeader.sectionAlignment),
                      this->ntHeaders.optionalHeader.sectionAlignment);
     }

     std::uint32_t fileSize = headersSize;
     std::uint32_t imageSize = headersSize;

     // Update section raw data and image size
     for (auto& section : this->sections)
     {
          section.pointerToRawData = fileSize;
          fileSize = AlignUp(fileSize + static_cast<std::uint32_t>(section.data.size()),
                             this->ntHeaders.optionalHeader.fileAlignment);
          imageSize +=
              AlignUp(static_cast<std::uint32_t>(section.data.size()), this->ntHeaders.optionalHeader.sectionAlignment);
     }

     this->ntHeaders.optionalHeader.sizeOfImage = imageSize;

     std::vector<std::byte> binary(fileSize, std::byte{0});

     // Copy DOS header
     memcpy(binary.data(), &this->dosHeader, sizeof(DosHeader));
     auto* dosHeaderPtr = reinterpret_cast<DosHeader*>(binary.data());
     dosHeaderPtr->e_lfanew = ntHeadersOffset;

     // Copy NT headers
     auto* ntHeadersPtr = reinterpret_cast<NtHeaders*>(binary.data() + ntHeadersOffset);
     memcpy(ntHeadersPtr, &this->ntHeaders, sizeof(NtHeaders));

     // Copy section headers
     auto* sectionHeadersPtr = reinterpret_cast<SectionHeader*>(binary.data() + sectionHeadersOffset);
     for (const auto& section : this->sections)
     {
          memcpy(sectionHeadersPtr->Name, section.name.c_str(),
                 std::min(section.name.size(), sizeof(sectionHeadersPtr->Name)));
          sectionHeadersPtr->VirtualAddress =
              AlignUp(section.virtualAddress, this->ntHeaders.optionalHeader.sectionAlignment);
          sectionHeadersPtr->PointerToRawData = section.pointerToRawData;
          sectionHeadersPtr->SizeOfRawData = static_cast<std::uint32_t>(section.data.size());
          sectionHeadersPtr->VirtualSize =
              AlignUp(static_cast<std::uint32_t>(section.data.size()), this->ntHeaders.optionalHeader.fileAlignment);
          sectionHeadersPtr->Characteristics = static_cast<std::uint32_t>(section.flags);
          ++sectionHeadersPtr;
     }

     // Copy section data
     for (const auto& section : this->sections)
     {
          if (!section.data.empty())
          {
               auto* sectionDataPtr = binary.data() + section.pointerToRawData;
               memcpy(sectionDataPtr, section.data.data(), section.data.size());
          }
     }

     return binary;
}
