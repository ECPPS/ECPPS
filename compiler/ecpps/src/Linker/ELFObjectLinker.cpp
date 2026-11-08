#include "ELFObjectLinker.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
     constexpr std::uint16_t ET_REL = 1;
     constexpr std::uint16_t EM_X86_64 = 62;
     constexpr std::uint32_t EV_CURRENT = 1;

     constexpr std::uint32_t SHT_PROGBITS = 1;
     constexpr std::uint32_t SHT_SYMTAB = 2;
     constexpr std::uint32_t SHT_STRTAB = 3;
     constexpr std::uint32_t SHT_RELA = 4;

     constexpr std::uint64_t SHF_ALLOC = 0x2;
     constexpr std::uint64_t SHF_EXECINSTR = 0x4;
     constexpr std::uint64_t SHF_INFO_LINK = 0x40;

     constexpr std::uint8_t STB_LOCAL = 0;
     constexpr std::uint8_t STB_GLOBAL = 1;
     constexpr std::uint8_t STT_OBJECT = 1;
     constexpr std::uint8_t STT_FUNC = 2;
     constexpr std::uint8_t STT_SECTION = 3;

     constexpr std::uint32_t R_X86_64_PC32 = 2;
     constexpr std::uint32_t R_X86_64_PLT32 = 4;

     constexpr std::string_view RdataSymbolName = "__ecpps_rdata";

     struct ELFHeader64 final
     {
          std::array<std::uint8_t, 16> identity{};
          std::uint16_t type{};
          std::uint16_t machine{};
          std::uint32_t version{};
          std::uint64_t entry{};
          std::uint64_t programHeaderOffset{};
          std::uint64_t sectionHeaderOffset{};
          std::uint32_t flags{};
          std::uint16_t headerSize{};
          std::uint16_t programHeaderEntrySize{};
          std::uint16_t programHeaderCount{};
          std::uint16_t sectionHeaderEntrySize{};
          std::uint16_t sectionHeaderCount{};
          std::uint16_t sectionNameStringTableIndex{};
     };

     struct ELFSectionHeader64 final
     {
          std::uint32_t name{};
          std::uint32_t type{};
          std::uint64_t flags{};
          std::uint64_t address{};
          std::uint64_t offset{};
          std::uint64_t size{};
          std::uint32_t link{};
          std::uint32_t info{};
          std::uint64_t alignment{};
          std::uint64_t entrySize{};
     };

     struct ELFSymbol64 final
     {
          std::uint32_t name{};
          std::uint8_t info{};
          std::uint8_t other{};
          std::uint16_t sectionIndex{};
          std::uint64_t value{};
          std::uint64_t size{};
     };

     struct ELFRelocation64 final
     {
          std::uint64_t offset{};
          std::uint64_t info{};
          std::int64_t addend{};
     };
     static_assert(sizeof(ELFHeader64) == 64);
     static_assert(sizeof(ELFSectionHeader64) == 64);
     static_assert(sizeof(ELFSymbol64) == 24);
     static_assert(sizeof(ELFRelocation64) == 24);

     struct SectionData final
     {
          std::string_view name;
          std::uint32_t type;
          std::uint64_t flags;
          std::vector<std::byte> data;
          std::uint64_t alignment;
          std::uint64_t entrySize = 0;
          std::uint32_t link = 0;
          std::uint32_t info = 0;
          std::uint32_t nameOffset = 0;
     };

     constexpr std::size_t AlignUp(const std::size_t value, const std::size_t alignment) noexcept
     {
          return (value + alignment - 1) & ~(alignment - 1);
     }

     std::uint32_t AddString(std::vector<std::byte>& table, const std::string_view value)
     {
          if (table.size() > std::numeric_limits<std::uint32_t>::max())
               throw std::overflow_error("ELF string table exceeds 32-bit name offsets");

          const auto offset = static_cast<std::uint32_t>(table.size());
          for (const char character : value) table.push_back(static_cast<std::byte>(character));
          table.push_back(std::byte{0});
          return offset;
     }

     std::vector<std::byte> AsBytes(const auto& values)
     {
          std::vector<std::byte> bytes(values.size() * sizeof(values.front()));
          if (!values.empty()) std::memcpy(bytes.data(), values.data(), bytes.size());
          return bytes;
     }
} // namespace

std::size_t ecpps::linker::elf::ELFObjectLinker::EnsureSymbol(const std::string& name)
{
     const auto symbol = this->_symbolIndices.find(name);
     if (symbol != this->_symbolIndices.end()) return symbol->second;

     const auto index = this->_symbols.size();
     this->_symbols.emplace_back(Symbol{.name = name});
     this->_symbolIndices.emplace(name, index);
     return index;
}

std::vector<std::byte> ecpps::linker::elf::ELFObjectLinker::CodeSection(
     std::vector<std::byte> data, const codegen::LinkerRelocationMap& relocationMap)
{
     this->_text = std::move(data);
     this->_relocations.clear();

     for (const auto& [where, relocation] : relocationMap)
     {
          const auto position = where.Value();
          const auto call = relocation.apply(false);
          if (call.size() != 5 || position + call.size() > this->_text.size())
               throw std::runtime_error("ELFObjectLinker: invalid call relocation range");

          std::ranges::copy(call, this->_text.begin() + static_cast<std::ptrdiff_t>(position));
          std::fill_n(this->_text.begin() + static_cast<std::ptrdiff_t>(position + 1), sizeof(std::int32_t),
                      std::byte{0});

          this->_relocations.emplace_back(Relocation{
               .offset = position + 1, .symbolName = relocation.symbolName, .type = R_X86_64_PLT32, .addend = -4});
     }

     for (const auto offset : this->_stringRelocationOffsets)
     {
          if (this->_toRelocateWidth != sizeof(std::uint32_t) || offset + sizeof(std::uint32_t) > this->_text.size())
               continue;

          std::uint32_t stringOffset{};
          std::memcpy(&stringOffset, this->_text.data() + offset, sizeof(stringOffset));
          std::fill_n(this->_text.begin() + static_cast<std::ptrdiff_t>(offset), sizeof(stringOffset), std::byte{0});

          this->_relocations.emplace_back(Relocation{.offset = offset,
                                                     .symbolName = std::string(RdataSymbolName),
                                                     .type = R_X86_64_PC32,
                                                     .addend = static_cast<std::int64_t>(stringOffset) - 4});
     }

     return this->_text;
}

void ecpps::linker::elf::ELFObjectLinker::ExportFunction(const std::string& name, const std::uint32_t address)
{
     auto& symbol = this->_symbols[this->EnsureSymbol(name)];
     symbol.sectionIndex = 1;
     symbol.value = address;
}

void ecpps::linker::elf::ELFObjectLinker::ImportFunction(const std::string& symbolName, const std::string& importName,
                                                         [[maybe_unused]] const std::string& dll)
{
     static_cast<void>(this->EnsureSymbol(importName));
     this->_symbolAliases.insert_or_assign(symbolName, importName);
}

std::vector<std::byte> ecpps::linker::elf::ELFObjectLinker::ToBytes(
     [[maybe_unused]] const std::string& imageName, [[maybe_unused]] const std::size_t entryPointAddress,
     const std::vector<std::byte>& stringData) const
{
     std::vector<std::byte> symbolStringTable{std::byte{0}};
     std::vector<ELFSymbol64> symbols(4);
     symbols[1].info = static_cast<std::uint8_t>((STB_LOCAL << 4) | STT_SECTION);
     symbols[1].sectionIndex = 1;
     symbols[2].info = static_cast<std::uint8_t>((STB_LOCAL << 4) | STT_SECTION);
     symbols[2].sectionIndex = 2;
     symbols[3].name = AddString(symbolStringTable, RdataSymbolName);
     symbols[3].info = static_cast<std::uint8_t>((STB_LOCAL << 4) | STT_OBJECT);
     symbols[3].sectionIndex = 2;
     symbols[3].size = stringData.size();

     std::unordered_map<std::string, std::uint32_t> outputSymbolIndices{};
     outputSymbolIndices.emplace(".text", 1);
     outputSymbolIndices.emplace(".rodata", 2);
     outputSymbolIndices.emplace(std::string(RdataSymbolName), 3);

     std::vector<std::uint64_t> functionOffsets;
     for (const auto& symbol : this->_symbols)
          if (symbol.sectionIndex == 1) functionOffsets.push_back(symbol.value);
     std::ranges::sort(functionOffsets);
     functionOffsets.erase(std::unique(functionOffsets.begin(), functionOffsets.end()), functionOffsets.end());

     for (const auto& symbol : this->_symbols)
     {
          ELFSymbol64 raw{};
          raw.name = AddString(symbolStringTable, symbol.name);
          raw.info = static_cast<std::uint8_t>((STB_GLOBAL << 4) | STT_FUNC);
          raw.sectionIndex = symbol.sectionIndex;
          raw.value = symbol.value;
          if (symbol.sectionIndex == 1)
          {
               const auto next = std::ranges::upper_bound(functionOffsets, symbol.value);
               raw.size = next == functionOffsets.end() ? this->_text.size() - symbol.value : *next - symbol.value;
          }

          const auto rawIndex = static_cast<std::uint32_t>(symbols.size());
          if (!outputSymbolIndices.emplace(symbol.name, rawIndex).second)
               throw std::runtime_error("ELFObjectLinker: duplicate symbol " + symbol.name);
          symbols.emplace_back(raw);
     }

     for (const auto& [alias, target] : this->_symbolAliases)
     {
          const auto symbol = outputSymbolIndices.find(target);
          if (symbol == outputSymbolIndices.end())
               throw std::runtime_error("ELFObjectLinker: unresolved alias target " + target);
          outputSymbolIndices.insert_or_assign(alias, symbol->second);
     }

     std::vector<ELFRelocation64> rawRelocations;
     rawRelocations.reserve(this->_relocations.size());
     for (const auto& relocation : this->_relocations)
     {
          const auto targetName = this->_symbolAliases.contains(relocation.symbolName)
                                       ? this->_symbolAliases.at(relocation.symbolName)
                                       : relocation.symbolName;
          const auto symbolIndex = outputSymbolIndices.find(targetName);
          if (symbolIndex == outputSymbolIndices.end())
               throw std::runtime_error("ELFObjectLinker: unresolved relocation symbol " + targetName);

          rawRelocations.emplace_back(
               ELFRelocation64{.offset = relocation.offset,
                               .info = (static_cast<std::uint64_t>(symbolIndex->second) << 32) | relocation.type,
                               .addend = relocation.addend});
     }

     const auto symbolBytes = AsBytes(symbols);
     const auto relocationBytes = AsBytes(rawRelocations);

     std::vector<SectionData> sections;
     sections.reserve(7);
     sections.emplace_back(SectionData{.name = ".text",
                                       .type = SHT_PROGBITS,
                                       .flags = SHF_ALLOC | SHF_EXECINSTR,
                                       .data = this->_text,
                                       .alignment = 16});
     sections.emplace_back(
          SectionData{.name = ".rodata", .type = SHT_PROGBITS, .flags = SHF_ALLOC, .data = stringData, .alignment = 1});
     sections.emplace_back(SectionData{.name = ".rela.text",
                                       .type = SHT_RELA,
                                       .flags = SHF_INFO_LINK,
                                       .data = relocationBytes,
                                       .alignment = alignof(ELFRelocation64),
                                       .entrySize = sizeof(ELFRelocation64),
                                       .link = 5,
                                       .info = 1});
     sections.emplace_back(SectionData{.name = ".note.GNU-stack",
                                       .type = SHT_PROGBITS,
                                       .flags = 0,
                                       .data = {},
                                       .alignment = 1,
                                       .entrySize = 0,
                                       .link = 0,
                                       .info = 0,
                                       .nameOffset = 0});
     sections.emplace_back(SectionData{.name = ".symtab",
                                       .type = SHT_SYMTAB,
                                       .flags = 0,
                                       .data = symbolBytes,
                                       .alignment = alignof(ELFSymbol64),
                                       .entrySize = sizeof(ELFSymbol64),
                                       .link = 6,
                                       .info = 4});
     sections.emplace_back(SectionData{.name = ".strtab",
                                       .type = SHT_STRTAB,
                                       .flags = 0,
                                       .data = symbolStringTable,
                                       .alignment = 1,
                                       .entrySize = 0,
                                       .link = 0,
                                       .info = 0,
                                       .nameOffset = 0});

     std::vector<std::byte> sectionNameStringTable{std::byte{0}};
     for (auto& section : sections) section.nameOffset = AddString(sectionNameStringTable, section.name);
     sections.emplace_back(SectionData{.name = ".shstrtab",
                                       .type = SHT_STRTAB,
                                       .flags = 0,
                                       .data = std::move(sectionNameStringTable),
                                       .alignment = 1,
                                       .entrySize = 0,
                                       .link = 0,
                                       .info = 0,
                                       .nameOffset = 0});
     sections.back().nameOffset = AddString(sections.back().data, ".shstrtab");

     std::vector<std::byte> output(sizeof(ELFHeader64), std::byte{0});
     std::vector<ELFSectionHeader64> sectionHeaders(sections.size() + 1);
     for (std::size_t i = 0; i < sections.size(); i++)
     {
          auto& section = sections[i];
          output.resize(AlignUp(output.size(), static_cast<std::size_t>(section.alignment)), std::byte{0});

          ELFSectionHeader64& header = sectionHeaders[i + 1];
          header.name = section.nameOffset;
          header.type = section.type;
          header.flags = section.flags;
          header.offset = output.size();
          header.size = section.data.size();
          header.link = section.link;
          header.info = section.info;
          header.alignment = section.alignment;
          header.entrySize = section.entrySize;
          output.insert(output.end(), section.data.begin(), section.data.end());
     }

     output.resize(AlignUp(output.size(), alignof(ELFSectionHeader64)), std::byte{0});
     ELFHeader64 header{};
     header.identity = {0x7F, 'E', 'L', 'F', 2, 1, 1};
     header.type = ET_REL;
     header.machine = EM_X86_64;
     header.version = EV_CURRENT;
     header.headerSize = sizeof(ELFHeader64);
     header.sectionHeaderOffset = output.size();
     header.sectionHeaderEntrySize = sizeof(ELFSectionHeader64);
     header.sectionHeaderCount = static_cast<std::uint16_t>(sectionHeaders.size());
     header.sectionNameStringTableIndex = static_cast<std::uint16_t>(sections.size());

     const auto sectionHeaderBytes = AsBytes(sectionHeaders);
     output.insert(output.end(), sectionHeaderBytes.begin(), sectionHeaderBytes.end());
     std::memcpy(output.data(), &header, sizeof(header));
     return output;
}
