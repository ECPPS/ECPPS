#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "Linker.h"

namespace ecpps::linker::win
{

#pragma pack(push, 1)

     constexpr std::uint16_t DosMagic = 0x5A4D;
     constexpr std::uint16_t MagicPE32 = 0x10b;
     constexpr std::uint16_t MagicPE32Plus = 0x20b;
     constexpr std::uint32_t PeMagic = 0x4550; // "PE\0\0"    z

     struct DosHeader
     {
          std::uint16_t e_magic;    // Magic number (0x5A4D)
          std::uint16_t e_cblp;     // Bytes on last page of file
          std::uint16_t e_cp;       // Pages in file
          std::uint16_t e_crlc;     // Relocations
          std::uint16_t e_cparhdr;  // Size of header in paragraphs
          std::uint16_t e_minalloc; // Minimum extra paragraphs needed
          std::uint16_t e_maxalloc; // Maximum extra paragraphs needed
          std::uint16_t e_ss;       // Initial (relative) SS
          std::uint16_t e_sp;       // Initial SP
          std::uint16_t e_csum;     // Checksum
          std::uint16_t e_ip;       // Initial IP
          std::uint16_t e_cs;       // Initial (relative) CS
          std::uint16_t e_lfarlc;   // File address of relocation table
          std::uint16_t e_ovno;     // Overlay number
          std::uint16_t e_res[4];   // Reserved words
          std::uint16_t e_oemid;    // OEM identifier (for e_oeminfo)
          std::uint16_t e_oeminfo;  // OEM information; e_oemid specific
          std::uint16_t e_res2[10]; // Reserved words
          std::uint32_t e_lfanew;   // File address of new exe header
     };

     // COFF File Header
     struct CoffFileHeader
     {
          std::uint16_t machine;              // Machine type
          std::uint16_t numberOfSections;     // Number of sections
          std::uint32_t timeDateStamp;        // Timestamp
          std::uint32_t pointerToSymbolTable; // Pointer to symbol table
          std::uint32_t numberOfSymbols;      // Number of symbols
          std::uint16_t sizeOfOptionalHeader; // Size of the optional header
          std::uint16_t characteristics;      // File characteristics
     };

     struct DataDirectory
     {
          std::uint32_t VirtualAddress;
          std::uint32_t Size;
     };

     // Optional Header (Image)
     struct OptionalHeader
     {
          std::uint16_t magic{};                     // Magic number (0x010B for PE32, 0x020B for PE32+)
          std::uint8_t majorLinkerVersion{};         // Linker version
          std::uint8_t minorLinkerVersion{};         // Linker version
          std::uint32_t sizeOfCode{};                // Size of code section
          std::uint32_t sizeOfInitializedData{};     // Size of initialized data section
          std::uint32_t sizeOfUninitializedData{};   // Size of uninitialized data section
          std::uint32_t addressOfEntryPoint{};       // Entry point address
          std::uint32_t baseOfCode{};                // Base address of code section
          std::uint64_t imageBase{};                 // Preferred base address
          std::uint32_t sectionAlignment{};          // Section alignment
          std::uint32_t fileAlignment{};             // File alignment
          std::uint16_t majorOperatingSystemVersion{}; // OS version
          std::uint16_t minorOperatingSystemVersion{}; // OS version
          std::uint16_t majorImageVersion{};         // Image version
          std::uint16_t minorImageVersion{};         // Image version
          std::uint16_t majorSubsystemVersion{};     // Subsystem version
          std::uint16_t minorSubsystemVersion{};     // Subsystem version
          std::uint32_t win32VersionValue{};         // Reserved
          std::uint32_t sizeOfImage{};               // Size of the image
          std::uint32_t sizeOfHeaders{};             // Size of headers
          std::uint32_t checkSum{};                  // Checksum
          std::uint16_t subsystem{};                 // Subsystem
          std::uint16_t dllCharacteristics{};        // DLL characteristics
          std::uint64_t sizeOfStackReserve{};        // Size of stack reserve
          std::uint64_t sizeOfStackCommit{};         // Size of stack commit
          std::uint64_t sizeOfHeapReserve{};         // Size of heap reserve
          std::uint64_t sizeOfHeapCommit{};          // Size of heap commit
          std::uint32_t loaderFlags{};               // Loader flags
          std::uint32_t numberOfRvaAndSizes{};       // Number of RVA and sizes
          // Optional: Data Directory (up to 16 entries)
          DataDirectory dataDirectory[16]{};
     };

     // NT Headers
     struct NtHeaders
     {
          std::uint32_t signature;       // PE signature ("PE\0\0")
          CoffFileHeader fileHeader;     // COFF file header
          OptionalHeader optionalHeader; // Optional header
     };

     struct SectionHeader
     {
          char Name[8];
          std::uint32_t VirtualSize;
          std::uint32_t VirtualAddress; // RVA of the section
          std::uint32_t SizeOfRawData;
          std::uint32_t PointerToRawData;
          std::uint32_t PointerToRelocations;
          std::uint32_t PointerToLinenumbers;
          std::uint16_t NumberOfRelocations;
          std::uint16_t NumberOfLinenumbers;
          std::uint32_t Characteristics;
     };

     struct ImportEntry
     {
          const char* name;
          void* address;
     };
#pragma pack(pop)

     enum struct PESectionFlags : std::uint32_t
     {
          ExecutableCode = 0x00000020,    // The section contains executable code
          InitialisedData = 0x00000040,   // The section contains initialised data.
          UninitialisedData = 0x00000080, // The section contains uninitialised data.
          Align1Byte = 0x00100000,        // Align data on a 1-byte boundary. This is valid only for object files.
          Align2Bytes = 0x00200000,       // Align data on a 2-byte boundary. This is valid only for object files.
          Align4Bytes = 0x00300000,       // Align data on a 4-byte boundary. This is valid only for object files.
          Align8Bytes = 0x00400000,       // Align data on a 8-byte boundary. This is valid only for object files.
          Align16Bytes = 0x00500000,      // Align data on a 16-byte boundary. This is valid only for object files.
          Align32Bytes = 0x00600000,      // Align data on a 32-byte boundary. This is valid only for object files.
          Align64Bytes = 0x00700000,      // Align data on a 64-byte boundary. This is valid only for object files.
          Align128Bytes = 0x00800000,     // Align data on a 128-byte boundary. This is valid only for object files.
          Align256Bytes = 0x00900000,     // Align data on a 256-byte boundary. This is valid only for object files.
          Align512Bytes = 0x00A00000,     // Align data on a 512-byte boundary. This is valid only for object files.
          Align1024Bytes = 0x00B00000,    // Align data on a 1024-byte boundary. This is valid only for object files.
          Align2048Bytes = 0x00C00000,    // Align data on a 2024-byte boundary. This is valid only for object files.
          Align4096Bytes = 0x00D00000,    // Align data on a 4096-byte boundary. This is valid only for object files.
          Align8192Bytes = 0x00E00000,    // Align data on a 8192-byte boundary. This is valid only for object files.
          /// <summary>
          /// The section contains extended relocations. The count of relocations for the section exceeds the
          /// 16 bits that is reserved for it in the section header. If the NumberOfRelocations field in the
          /// section header is 0xffff, the actual relocation count is stored in the VirtualAddress field of
          /// the first relocation. It is an error if IMAGE_SCN_LNK_NRELOC_OVFL is set and there are fewer
          /// than 0xffff relocations in the section.
          /// </summary>
          ExtendedAllocations = 0x01000000,
          Discard = 0x02000000,     // The section can be discarded as needed.
          NoCache = 0x04000000,     // The section cannot be cached.
          NonPageable = 0x08000000, // The section cannot be paged.
          Shared = 0x10000000,      // The section can be shared in memory.
          Execute = 0x20000000,     // The section can be executed as code.
          Read = 0x40000000,        // The section can be read.
          Write = 0x80000000,       // The section can be written to.
     };

     constexpr PESectionFlags operator|(PESectionFlags lhs, PESectionFlags rhs)
     {
          return static_cast<PESectionFlags>(static_cast<std::underlying_type_t<PESectionFlags>>(lhs) |
                                             static_cast<std::underlying_type_t<PESectionFlags>>(rhs));
     }

     struct PESection
     {
          std::string name{};
          std::vector<std::byte> data{};
          PESectionFlags flags{};
          std::uint32_t pointerToRawData{};
          std::uint32_t virtualAddress{};
     };

     struct PEImage
     {
          explicit PEImage(std::uintptr_t imageBase, std::uint32_t entryPoint, bool isNxCompatible, bool isRelocatable,
                           PESubsystem subsystem, LinkType linkType, std::uint32_t fileAlignment);

          std::vector<PESection> sections{};
          std::unordered_map<std::string, std::uint32_t> exports{};
          std::unordered_map<std::string, std::vector<std::string>> imports;

          [[nodiscard]] std::vector<std::byte> toBytes(const std::string& imageName);

     //private:
          DosHeader _dosHeader{};
          NtHeaders _ntHeaders{};
     };
} // namespace ecpps::linker::win