#include "CoffLinker.h"
#include <unordered_map>
#include <vector>

using namespace ecpps::linker::win;

namespace
{
#pragma pack(push, 1)

     struct COFFHeader
     {
          std::uint16_t machine;
          std::uint16_t numberOfSections;
          std::uint32_t timeDateStamp;
          std::uint32_t pointerToSymbolTable;
          std::uint32_t numberOfSymbols;
          std::uint16_t sizeOfOptionalHeader;
          std::uint16_t characteristics;
     };
     constexpr std::uint16_t F_RELFLG = 0x0001;
     constexpr std::uint16_t F_EXEC = 0x0002;
     constexpr std::uint16_t F_LNNO = 0x0004;
     constexpr std::uint16_t F_LSYMS = 0x0008;
     constexpr std::uint16_t F_LITTLE = 0x0100;
     constexpr std::uint16_t F_BIG = 0x0200;
     constexpr std::uint16_t F_SYMMERGE = 0x1000;
     struct COFFOptionalHeader
     {
          std::uint16_t magic;
          std::uint8_t majorLinkerVersion;
          std::uint8_t minorLinkerVersion;
          std::uint32_t sizeOfCode;
          std::uint32_t sizeOfInitializedData;
          std::uint32_t sizeOfUninitializedData;
          std::uint32_t addressOfEntryPoint;
          std::uint32_t baseOfCode;
     };

     struct COFFSectionHeader
     {
          char name[8]; // NOLINT
          std::uint32_t virtualSize;
          std::uint32_t virtualAddress;
          std::uint32_t sizeOfRawData;
          std::uint32_t pointerToRawData;
          std::uint32_t pointerToRelocations;
          std::uint32_t pointerToLinenumbers;
          std::uint16_t numberOfRelocations;
          std::uint16_t numberOfLinenumbers;
          std::uint32_t characteristics;
     };

     constexpr std::uint32_t SCN_CNT_CODE = 0x00000020;
     constexpr std::uint32_t SCN_MEM_EXECUTE = 0x20000000;
     constexpr std::uint32_t SCN_MEM_READ = 0x40000000;
     constexpr std::uint32_t SCN_MEM_WRITE = 0x80000000;
     constexpr std::uint32_t SCN_CNT_INITIALISED_DATA = 0x00000040;
     constexpr std::uint32_t SCN_CNT_UNINITIALISED_DATA = 0x00000080;

     struct COFFRelocationRaw
     {
          std::uint32_t virtualAddress;
          std::uint32_t symbolTableIndex;
          std::uint16_t type;
     };

     struct COFFSymbolRaw
     {
          union
          {
               char shortName[8]; // NOLINT
               struct LongName
               {
                    std::uint32_t zero;
                    std::uint32_t offset;
               };
               LongName longName;
          };

          std::uint32_t value;
          std::int16_t sectionNumber;
          std::uint16_t type;
          std::uint8_t storageClass;
          std::uint8_t numberOfAuxSymbols;
     };

#pragma pack(pop)

     constexpr std::uint16_t MachineAMD64 = 0x8664;
     constexpr std::uint32_t SCN_TEXT = 0x60000020;  // CNT_CODE | MEM_EXECUTE | MEM_READ
     constexpr std::uint32_t SCN_RDATA = 0x40000040; // CNT_INITIALIZED_DATA | MEM_READ
     constexpr std::uint16_t REL_REL32 = 0x0004;     // IMAGE_REL_AMD64_REL32
} // namespace

constexpr std::string_view CodeSectionName = ".text";
constexpr std::string_view RdataSectionName = ".rdata";
constexpr std::string_view RdataSymbolName = "__ecpps_rdata";

std::vector<std::byte> CoffLinker::CodeSection(std::vector<std::byte> data,
                                               const codegen::LinkerRelocationMap& relocationMap)
{
     COFFSection section{};
     section.name = CodeSectionName;
     section.data = std::move(data);
     section.characteristics = SCN_TEXT;

     for (const auto& [where, relocation] : relocationMap)
     {
          std::unordered_map<std::string, std::vector<std::byte>> meow{};
          const auto call = relocation.apply(Address{0}, meow);
          const auto pos = where.Value();

          for (std::size_t i = 0; i < call.size() && (pos + i) < section.data.size(); i++)
               section.data[pos + i] = call[i];

          for (std::size_t i = 2; i < 6 && (pos + i) < section.data.size(); i++) section.data[pos + i] = std::byte{0};

          COFFRelocation r{};
          r.offset = static_cast<std::uint32_t>(pos) + 2;
          r.symbolName = _symbols.at(this->_symbolOffsets.at(relocation.symbolName)).name;
          r.type = REL_REL32;
          r.sectionIndex = 0;
          this->_relocations.emplace_back(std::move(r));
     }

     for (const std::size_t slotOffset : _stringRelocationOffsets)
     {
          if (slotOffset + 4 > section.data.size()) continue;

          std::int32_t wrongDisp = 0;
          std::memcpy(&wrongDisp, section.data.data() + slotOffset, 4);

          const std::int32_t stringOffset = wrongDisp + static_cast<std::int32_t>(slotOffset) + 4 - 0x3000;

          std::memcpy(section.data.data() + slotOffset, &stringOffset, 4);

          COFFRelocation r{};
          r.offset = static_cast<std::uint32_t>(slotOffset);
          r.symbolName = std::string(RdataSymbolName);
          r.type = REL_REL32;
          r.sectionIndex = 0;
          this->_relocations.emplace_back(std::move(r));
     }

     this->_sections.emplace_back(std::move(section));
     return this->_sections.back().data;
}

void CoffLinker::ExportFunction(const std::string& name, std::uint32_t address)
{
     COFFSymbol symbol{};
     symbol.name = name;
     symbol.value = address;
     symbol.sectionIndex = 1;
     symbol.storageClass = COFFStorageClass::External;

     this->_symbolOffsets[name] = static_cast<std::uint32_t>(_symbols.size());
     this->_symbols.emplace_back(std::move(symbol));
}

void CoffLinker::ImportFunction(const std::string& symbolName, const std::string& importName,
                                [[maybe_unused]] const std::string& dll)
{
     const std::string impName = "__imp_" + importName;
     if (_symbolOffsets.contains(impName)) return;

     COFFSymbol symbol{};
     symbol.name = impName;
     symbol.value = 0;
     symbol.sectionIndex = 0;
     symbol.storageClass = COFFStorageClass::External;
     symbol.type = 0x20;

     const auto idx = static_cast<std::uint32_t>(_symbols.size());
     this->_symbolOffsets[symbolName] = idx;
     this->_symbolOffsets[importName] = idx;
     this->_symbolOffsets[impName] = idx;
     this->_symbols.emplace_back(std::move(symbol));
}

std::vector<std::byte> CoffLinker::ToBytes(const std::string& imageName, std::size_t entryPointAddress,
                                           const std::vector<std::byte>& stringData) const
{
     auto symbols = this->_symbols;
     auto symbolOffsets = this->_symbolOffsets;

     std::vector<COFFSection> ownedSections(this->_sections);
     std::size_t rdataSectionIndex = std::string::npos;
     for (std::size_t i = 0; i < ownedSections.size(); ++i)
     {
          if (ownedSections[i].name == RdataSectionName)
          {
               ownedSections[i].data = stringData;
               ownedSections[i].characteristics = SCN_RDATA;
               rdataSectionIndex = i;
               break;
          }
     }

     if (rdataSectionIndex == std::string::npos)
     {
          rdataSectionIndex = ownedSections.size();
          COFFSection rdataSection{};
          rdataSection.name = RdataSectionName;
          rdataSection.data = stringData;
          rdataSection.characteristics = SCN_RDATA;
          ownedSections.emplace_back(std::move(rdataSection));
     }

     const auto rdataCoffSectionNumber = static_cast<std::int16_t>(rdataSectionIndex + 1);

     COFFSymbol rdataSym{};
     rdataSym.name = std::string(RdataSymbolName);
     rdataSym.value = 0;
     rdataSym.sectionIndex = rdataCoffSectionNumber;
     rdataSym.storageClass = COFFStorageClass::Static;
     rdataSym.type = 0;

     symbolOffsets[std::string(RdataSymbolName)] = static_cast<std::uint32_t>(symbols.size());
     symbols.emplace_back(std::move(rdataSym));

     const std::size_t numSections = ownedSections.size();

     std::vector<std::vector<COFFRelocationRaw>> perSectionRelocs(numSections);

     for (const auto& r : this->_relocations)
     {
          COFFRelocationRaw raw{};
          raw.virtualAddress = r.offset;

          auto it = symbolOffsets.find(r.symbolName);
          if (it == symbolOffsets.end())
               throw std::runtime_error("CoffLinker: unresolved relocation symbol: " + r.symbolName);

          raw.symbolTableIndex = it->second;
          raw.type = r.type;
          perSectionRelocs[r.sectionIndex].emplace_back(raw);
     }

     const std::size_t headerSize = sizeof(COFFHeader) + (numSections * sizeof(COFFSectionHeader));

     std::size_t offset = headerSize;
     std::vector<COFFSectionHeader> sectionHeaders;
     sectionHeaders.reserve(numSections);

     for (std::size_t i = 0; i < numSections; ++i)
     {
          const auto& sec = ownedSections[i];
          const auto& relocs = perSectionRelocs[i];

          COFFSectionHeader sh{};
          std::ranges::copy(sec.name | std::views::take(8), sh.name);
          sh.virtualSize = static_cast<std::uint32_t>(sec.data.size());
          sh.sizeOfRawData = static_cast<std::uint32_t>(sec.data.size());
          sh.pointerToRawData = static_cast<std::uint32_t>(offset);
          sh.characteristics = sec.characteristics;
          offset += sec.data.size();

          sh.pointerToRelocations = static_cast<std::uint32_t>(offset);
          sh.numberOfRelocations = static_cast<std::uint16_t>(relocs.size());
          offset += relocs.size() * sizeof(COFFRelocationRaw);

          sectionHeaders.emplace_back(sh);
     }

     COFFHeader header{};
     header.machine = MachineAMD64;
     header.numberOfSections = static_cast<std::uint16_t>(numSections);
     header.pointerToSymbolTable = static_cast<std::uint32_t>(offset);
     header.numberOfSymbols = static_cast<std::uint32_t>(symbols.size());

     std::vector<std::byte> strTable;
     std::uint32_t strSize = 4;

     std::vector<COFFSymbolRaw> rawSymbols;
     rawSymbols.reserve(symbols.size());

     for (const auto& sym : symbols)
     {
          COFFSymbolRaw raw{};
          if (sym.name.size() <= 8) { std::ranges::copy(sym.name, raw.shortName); }
          else
          {
               raw.longName.zero = 0;
               raw.longName.offset = strSize;
               for (char c : sym.name) strTable.push_back(static_cast<std::byte>(c));
               strTable.push_back(std::byte{0});
               strSize += static_cast<std::uint32_t>(sym.name.size() + 1);
          }
          raw.value = sym.value;
          raw.sectionNumber = sym.sectionIndex;
          raw.type = sym.type;
          raw.storageClass = static_cast<std::uint8_t>(sym.storageClass);
          raw.numberOfAuxSymbols = sym.auxSymbols;
          rawSymbols.emplace_back(raw);
     }

     const std::uint32_t strTableSize = strSize;

     std::vector<std::byte> output;

     output.resize(sizeof(COFFHeader));
     std::memcpy(output.data(), &header, sizeof(header));

     for (const auto& sh : sectionHeaders)
     {
          const auto p = output.size();
          output.resize(p + sizeof(sh));
          std::memcpy(output.data() + p, &sh, sizeof(sh));
     }

     for (std::size_t i = 0; i < numSections; ++i)
     {
          const auto& sec = ownedSections[i];
          const auto p = output.size();
          output.resize(p + sec.data.size());
          std::memcpy(output.data() + p, sec.data.data(), sec.data.size());

          for (const auto& r : perSectionRelocs[i])
          {
               const auto rp = output.size();
               output.resize(rp + sizeof(r));
               std::memcpy(output.data() + rp, &r, sizeof(r));
          }
     }

     for (const auto& sym : rawSymbols)
     {
          const auto p = output.size();
          output.resize(p + sizeof(sym));
          std::memcpy(output.data() + p, &sym, sizeof(sym));
     }

     const auto p = output.size();
     output.resize(p + 4);
     std::memcpy(output.data() + p, &strTableSize, 4);
     output.insert(output.end(), strTable.begin(), strTable.end());

     return output;
}
