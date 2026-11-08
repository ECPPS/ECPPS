#include <Linker/ELFObjectLinker.h>
#include <catch_amalgamated.hpp>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
     struct ELFHeader64 final
     {
          std::uint8_t identity[16]; // NOLINT
          std::uint16_t type;
          std::uint16_t machine;
          std::uint32_t version;
          std::uint64_t entry;
          std::uint64_t programHeaderOffset;
          std::uint64_t sectionHeaderOffset;
          std::uint32_t flags;
          std::uint16_t headerSize;
          std::uint16_t programHeaderEntrySize;
          std::uint16_t programHeaderCount;
          std::uint16_t sectionHeaderEntrySize;
          std::uint16_t sectionHeaderCount;
          std::uint16_t sectionNameStringTableIndex;
     };

     struct ELFSectionHeader64 final
     {
          std::uint32_t name;
          std::uint32_t type;
          std::uint64_t flags;
          std::uint64_t address;
          std::uint64_t offset;
          std::uint64_t size;
          std::uint32_t link;
          std::uint32_t info;
          std::uint64_t alignment;
          std::uint64_t entrySize;
     };

     struct ELFSymbol64 final
     {
          std::uint32_t name;
          std::uint8_t info;
          std::uint8_t other;
          std::uint16_t sectionIndex;
          std::uint64_t value;
          std::uint64_t size;
     };

     struct ELFRelocation64 final
     {
          std::uint64_t offset;
          std::uint64_t info;
          std::int64_t addend;
     };
     static_assert(sizeof(ELFHeader64) == 64);
     static_assert(sizeof(ELFSectionHeader64) == 64);
     static_assert(sizeof(ELFSymbol64) == 24);
     static_assert(sizeof(ELFRelocation64) == 24);

     template <typename T> T ReadAt(const std::vector<std::byte>& bytes, const std::size_t offset)
     {
          if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
               throw std::out_of_range("ELF test reader: structure is outside the object file");

          T value{};
          std::memcpy(&value, bytes.data() + offset, sizeof(value));
          return value;
     }

     std::string ReadString(const std::vector<std::byte>& bytes, const std::size_t offset)
     {
          if (offset >= bytes.size()) throw std::out_of_range("ELF test reader: string is outside the object file");
          return std::string(reinterpret_cast<const char*>(bytes.data() + offset));
     }

     std::vector<ELFSectionHeader64> ReadSections(const std::vector<std::byte>& bytes, const ELFHeader64& header)
     {
          std::vector<ELFSectionHeader64> sections;
          sections.reserve(header.sectionHeaderCount);
          for (std::size_t i = 0; i < header.sectionHeaderCount; i++)
               sections.push_back(ReadAt<ELFSectionHeader64>(
                    bytes, static_cast<std::size_t>(header.sectionHeaderOffset + (i * header.sectionHeaderEntrySize))));
          return sections;
     }

     std::size_t FindSection(const std::vector<std::byte>& bytes, const ELFHeader64& header,
                             const std::vector<ELFSectionHeader64>& sections, const std::string& name)
     {
          const auto& names = sections.at(header.sectionNameStringTableIndex);
          for (std::size_t i = 0; i < sections.size(); i++)
          {
               if (ReadString(bytes, static_cast<std::size_t>(names.offset + sections[i].name)) == name) return i;
          }
          throw std::runtime_error("ELF test reader: missing section " + name);
     }

     std::string GetSymbolName(const std::vector<std::byte>& bytes, const ELFSectionHeader64& symbols,
                               const ELFSectionHeader64& strings, const std::uint32_t index)
     {
          const auto symbol =
               ReadAt<ELFSymbol64>(bytes, static_cast<std::size_t>(symbols.offset + (index * symbols.entrySize)));
          return ReadString(bytes, static_cast<std::size_t>(strings.offset + symbol.name));
     }
} // namespace

TEST_CASE("ELF relocatable objects encode imported calls and string addresses", "[linker][elf]")
{
     using ecpps::codegen::LinkerRelocationMap;
     using ecpps::codegen::Relocation;

     ecpps::linker::elf::ELFObjectLinker linker{};
     linker.ImportFunction("puts", "puts", "libc.so.6");
     linker.SetStringRelocations({3}, sizeof(std::uint32_t));

     LinkerRelocationMap relocationMap;
     relocationMap.emplace(ByteOffset{10}, Relocation{.symbolName = "puts",
                                                      .apply =
                                                           [](const bool)
                                                      {
                                                           return std::vector<std::byte>{std::byte{0xE8}, std::byte{0},
                                                                                         std::byte{0}, std::byte{0},
                                                                                         std::byte{0}};
                                                      },
                                                      .applyOutputSize = 2,
                                                      .isIndirect = false});

     std::vector<std::byte> code{std::byte{0x48}, std::byte{0x8D}, std::byte{0x05}, std::byte{0},    std::byte{0},
                                 std::byte{0},    std::byte{0},    std::byte{0x48}, std::byte{0x89}, std::byte{0xC7},
                                 std::byte{0xE8}, std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},
                                 std::byte{0x90}, std::byte{0xC3}};
     const auto patchedCode = linker.CodeSection(std::move(code), relocationMap);
     linker.ExportFunction("main", 0);

     const std::vector<std::byte> stringData{std::byte{'h'}, std::byte{'i'}, std::byte{0}};
     const auto object = linker.ToBytes("test", 0, stringData);
     const auto header = ReadAt<ELFHeader64>(object, 0);

     CHECK(header.identity[0] == 0x7F);
     CHECK(header.identity[1] == 'E');
     CHECK(header.identity[2] == 'L');
     CHECK(header.identity[3] == 'F');
     CHECK(header.type == 1);
     CHECK(header.machine == 62);
     CHECK(patchedCode[10] == std::byte{0xE8});
     CHECK(patchedCode[11] == std::byte{0});
     CHECK(patchedCode[15] == std::byte{0x90});

     const auto sections = ReadSections(object, header);
     const auto textIndex = FindSection(object, header, sections, ".text");
     const auto relocationIndex = FindSection(object, header, sections, ".rela.text");
     const auto symbolIndex = FindSection(object, header, sections, ".symtab");
     const auto stringIndex = FindSection(object, header, sections, ".strtab");

     const auto& textSection = sections[textIndex];
     const auto& relocationSection = sections[relocationIndex];
     const auto& symbolSection = sections[symbolIndex];
     const auto& stringSection = sections[stringIndex];
     CHECK(relocationSection.alignment == alignof(std::uint64_t));
     CHECK(relocationSection.entrySize == sizeof(ELFRelocation64));
     CHECK(relocationSection.link == symbolIndex);
     CHECK(relocationSection.info == textIndex);

     bool foundCallRelocation = false;
     bool foundStringRelocation = false;
     for (std::size_t offset = 0; offset < relocationSection.size; offset += relocationSection.entrySize)
     {
          const auto relocation =
               ReadAt<ELFRelocation64>(object, static_cast<std::size_t>(relocationSection.offset + offset));
          const auto symbolNumber = static_cast<std::uint32_t>(relocation.info >> 32);
          const auto relocationType = static_cast<std::uint32_t>(relocation.info);
          const auto targetName = GetSymbolName(object, symbolSection, stringSection, symbolNumber);

          if (relocation.offset == 11)
          {
               CHECK(relocationType == 4);
               CHECK(relocation.addend == -4);
               CHECK(targetName == "puts");
               foundCallRelocation = true;
          }
          else if (relocation.offset == 3)
          {
               CHECK(relocationType == 2);
               CHECK(relocation.addend == -4);
               CHECK(targetName == "__ecpps_rdata");
               foundStringRelocation = true;
          }
     }

     CHECK(foundCallRelocation);
     CHECK(foundStringRelocation);
     CHECK(textSection.size == patchedCode.size());
}
