#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "Linker.h"
#include "PE.h"

namespace ecpps::linker::win
{
     class WindowsLinker final : public LinkerBase
     {
     public:
          explicit WindowsLinker(LinkerOptions<LinkerType::PE> options)
              : _imageBase(options.baseAddress), _isNxCompatible(options.enableNXBit),
                _isRelocatable(options.dynamicBase), _subsystem(options.subsystem),
                _fileAlignment(options.fileAlignment), _linkType(options.type)
          {
          }

          std::vector<std::byte> CodeSection(std::vector<std::byte> data,
                                             const codegen::LinkerRelocationMap& relocationMap) override;
          void ExportFunction(const std::string& name, std::uint32_t address) override;
          void ImportFunction(const std::string& name, const std::string& dll) override;

          void AddSection(const PESection& value);
          void ExportAt(const std::string& name, std::uint32_t address);

          [[nodiscard]] std::vector<std::byte> ToBytes(const std::string& imageName,
                                                       std::size_t entryPointAddress) const override;

          [[nodiscard]] std::uint32_t LookupSymbol(const std::string& symbolName, std::uint32_t codeSize) const;

     private:
          [[nodiscard]] PEImage Link(std::uint32_t entryPoint) const;

          std::uintptr_t _imageBase;
          bool _isNxCompatible;
          bool _isRelocatable;
          PESubsystem _subsystem;
          std::uint32_t _fileAlignment;
          LinkType _linkType;
          std::vector<PESection> _sections{};
          std::unordered_map<std::string, std::uint32_t> _exports{};
          std::unordered_map<std::string, std::vector<std::string>> _imports{};
          std::size_t _baseOfCode{};
          std::size_t _sizeOfCode{};
     };
} // namespace ecpps::linker::win