#pragma once
#include <format>
#include <functional>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../Execution/NodeBase.h"
#include "../Parsing/SourceMap.h"
#include "AbstractNodes.h"
#include "Execution/Operations.h"
#include "Machine/Encoders/API/Target.h"
#include "Shared/Config.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"

namespace ecpps::codegen
{
     using Byte = char8_t;
     struct ByteView
     {
          std::size_t begin{};
          std::size_t end{};

          [[nodiscard]] constexpr std::size_t Size(void) const noexcept
          {
               return this->end - this->begin;
          }
          [[nodiscard]] constexpr bool operator==(const ByteView& other) const noexcept
          {
               return this->Size() == other.Size() && this->begin == other.begin;
          }
     };
} // namespace ecpps::codegen

template <> struct std::hash<ecpps::codegen::ByteView>
{
     std::size_t operator()(const ecpps::codegen::ByteView& view) const noexcept
     {
          return view.begin ^ (view.end << 1);
     }
};

namespace ecpps::codegen
{
     extern std::unordered_map<std::string, std::string> g_functionImports;
     enum struct ValueProperty : std::uint8_t
     {
          None = 0,
          Integer = 1 << 0,
          Floating = 1 << 1,
          Pointer = 1 << 2,
          Aggregate = 1 << 3,
          Array = 1 << 4,

          Signed = 1 << 5,
     };
     constexpr bool operator&(const ValueProperty left, const ValueProperty right)
     {
          return (std::to_underlying(left) & std::to_underlying(right)) != 0;
     }
     constexpr ValueProperty operator|(const ValueProperty left, const ValueProperty right)
     {
          return static_cast<ValueProperty>(std::to_underlying(left) | std::to_underlying(right));
     }

     struct AllocationDescriptor
     {
          using Type = ir::abstract::AllocationClass;

          std::size_t size;
          std::size_t alignment;
          ValueProperty properties;
          Type type;
     };
     struct VirtualNotFoundError : std::exception
     {
          VirtualNotFoundError([[maybe_unused]] auto&&... args)
          {
          }
     };
     struct AllocationMap
     {
          using Index = std::size_t;
          static constexpr Index InvalidIndex = std::numeric_limits<Index>::max();

          [[nodiscard]] Index EmplaceAllocate(Index ssaIndex, Index size, Index alignment,
                                              AllocationDescriptor::Type type,
                                              ValueProperty extraProperties = ValueProperty::None)
          {
               const Index virtualIndex = _descriptorArray.size();

               _descriptorArray.emplace_back(AllocationDescriptor{
                    .size = size, .alignment = alignment, .properties = extraProperties, .type = type});

               _ssaByVirtual.push_back(InvalidIndex);

               if (ssaIndex >= _descriptorArrayWindow.size()) _descriptorArrayWindow.resize(ssaIndex + 1, InvalidIndex);

               runtime_assert(_descriptorArrayWindow[ssaIndex] == InvalidIndex, "SSA register is already allocated");

               _descriptorArrayWindow[ssaIndex] = virtualIndex;
               _ssaByVirtual[virtualIndex] = ssaIndex;

               return virtualIndex;
          }

          [[nodiscard]] Index FindVirtualBySSA(Index ssaIndex) const
          {
               if (ssaIndex >= _descriptorArrayWindow.size())
                    throw VirtualNotFoundError(std::logic_error(std::format("Invalid SSA index: {}", ssaIndex)));

               const auto virtualIndex = _descriptorArrayWindow[ssaIndex];

               if (virtualIndex == InvalidIndex)
                    throw VirtualNotFoundError(
                         std::logic_error(std::format("SSA index {} is not allocated", ssaIndex)));

               return virtualIndex;
          }

          [[nodiscard]] Index FindSSAByVirtual(Index virtualIndex) const
          {
               if (virtualIndex >= _ssaByVirtual.size() || _ssaByVirtual[virtualIndex] == InvalidIndex)
                    throw VirtualNotFoundError(
                         std::logic_error(std::format("Virtual index {} is not allocated", virtualIndex)));

               return _ssaByVirtual[virtualIndex];
          }

          [[nodiscard]] AllocationDescriptor& GetDescriptorFromSSA(Index ssaIndex)
          {
               return GetDescriptorFromVirtual(FindVirtualBySSA(ssaIndex));
          }

          [[nodiscard]] const AllocationDescriptor& GetDescriptorFromSSA(Index ssaIndex) const
          {
               return GetDescriptorFromVirtual(FindVirtualBySSA(ssaIndex));
          }

          [[nodiscard]] AllocationDescriptor& GetDescriptorFromVirtual(Index virtualIndex)
          {
               if (virtualIndex >= _descriptorArray.size() || _ssaByVirtual[virtualIndex] == InvalidIndex)
                    throw TracedException(std::format("Invalid virtual index {}", virtualIndex));

               return _descriptorArray[virtualIndex];
          }

          [[nodiscard]] const AllocationDescriptor& GetDescriptorFromVirtual(Index virtualIndex) const
          {
               if (virtualIndex >= _descriptorArray.size() || _ssaByVirtual[virtualIndex] == InvalidIndex)
                    throw TracedException(std::format("Invalid virtual index {}", virtualIndex));

               return _descriptorArray[virtualIndex];
          }

          void ReleaseSSA(Index ssaIndex)
          {
               runtime_assert(ssaIndex < _descriptorArrayWindow.size(), "invalid ssa register");

               const auto virtualIndex = std::exchange(_descriptorArrayWindow[ssaIndex], InvalidIndex);

               if (virtualIndex == InvalidIndex) return;

               runtime_assert(virtualIndex < _descriptorArray.size(), "invalid virtual index");

               _ssaByVirtual[virtualIndex] = InvalidIndex;
               _descriptorArray[virtualIndex].type = AllocationDescriptor::Type::Invalid;
          }

     private:
          std::vector<Index> _descriptorArrayWindow{};
          std::vector<AllocationDescriptor> _descriptorArray{};
          std::vector<Index> _ssaByVirtual{};
     };

     struct ParsingContext
     {
          std::vector<ir::abstract::VirtualInstruction> instructions;
          ecpps::abi::ABI* abi{};
          std::vector<ecpps::diagnostics::DiagnosticsMessage> diagnostics{};
          abi::api::Target* target{};
          AllocationMap virtualRegisterAllocationMap;

          void ParseNode(const ir::NodeBase* node);

          void ParseAllocateNode(const ir::AllocationNode& node);
          void ParseReturnNode(const ir::SSAReturnNode& node);
          void ParseStoreNode(const ir::SSAStoreNode& node);
          void ParseStoreIntNode(const ir::SSAStoreIntegerNode& node);
          void ParseAddNode(const ir::SSAAddNode& node);
          void ParseSubNode(const ir::SSASubNode& node);
          void ParseLeftShiftNode(const ir::SSALeftShiftNode& node);
          void ParseRightShiftNode(const ir::SSARightShiftNode& node);
          void ParseBinOrNode(const ir::SSABinOrNode& node);
          void ParseBinAndNode(const ir::SSABinAndNode& node);
          void ParseBinXorNode(const ir::SSABinXorNode& node);
          void ParseBitwiseNotNode(const ir::SSABitwiseNotNode& node);
          void ParseArithmeticNegationNode(const ir::SSAArithmeticNegationNode& node);
          void ParseConvertNode(const ir::SSAConvertNode& node);
          void ParseLoadNode(const ir::SSALoadNode& node);
          void ParseIntNode(const ir::SSAImmNode& node);
          explicit ParsingContext(ecpps::abi::ABI& abi);

     private:
          void DereferenceSSA(std::size_t ssaIndex);
          [[nodiscard]] std::size_t AllocateVirtual(std::size_t ssaIndex, std::size_t size, std::size_t alignment,
                                                    AllocationDescriptor::Type type,
                                                    ValueProperty properties = ValueProperty::None);
     };

     struct AssemblyContext
     {
          struct alignas(std::uint64_t) StringEntry
          {
               std::uint32_t length{};
               std::uint32_t offset{};
          };

          explicit AssemblyContext(CompilerConfig& config) : _config(std::ref(config))
          {
          }
          AssemblyContext(const AssemblyContext&) = delete;
          AssemblyContext(AssemblyContext&&) = delete;
          AssemblyContext& operator=(const AssemblyContext&) = delete;
          AssemblyContext& operator=(AssemblyContext&&) = delete;

          [[nodiscard]] StringIndex AddString(std::span<const Byte> value)
          {
               switch (_config.get().stringPooling)
               {
               case StringPooling::None: return AppendNew(value);

               case StringPooling::Exact:
               {
                    ByteView probe{.begin = static_cast<std::size_t>(value.data() - this->_arena.data()),
                                   .end = value.size()};

                    if (const auto iterator = _exactLookup.find(probe); iterator != _exactLookup.end())
                    {
                         return {.indexInTable = iterator->second, .offset = 0};
                    }

                    return AppendNew(value);
               }

               case StringPooling::Substring:
               {
                    std::basic_string_view<Byte> probe{value.data(), value.size()};

                    for (const auto& [view, index] : _exactLookup)
                    {
                         if (view.Size() >= probe.size())
                         {
                              if (const auto position =
                                       std::basic_string_view<Byte>{this->_arena.data() + view.begin, view.Size()}.find(
                                            probe);
                                  position != std::basic_string_view<Byte>::npos)
                              {
                                   return {.indexInTable = index, .offset = static_cast<std::uint32_t>(position)};
                              }
                         }
                    }

                    return AppendNew(value);
               }
               }

               std::unreachable();
          }

          [[nodiscard]] std::basic_string_view<Byte> GetString(StringIndex index) const noexcept
          {
               const auto& entry = _stringTable[index.indexInTable];
               return {this->_arena.data() + entry.offset + index.offset, entry.length};
          }

          [[nodiscard]] std::size_t GetStringOffset(StringIndex index) const noexcept
          {
               const auto& entry = _stringTable[index.indexInTable];
               return entry.offset + index.offset;
          }
          [[nodiscard]] const auto& GetStringSection(void) const noexcept
          {
               return this->_arena;
          }

          void AddStringPatch(std::uint32_t instructionOffset, InstructionPatchType patchType, StringIndex index)
          {
               this->_patches.emplace_back(index, instructionOffset, patchType);
          }

          [[nodiscard]] auto& Patches(void) noexcept
          {
               return this->_patches;
          }

     private:
          static std::uint32_t ReserveNextStringEntry(void) noexcept;

          [[nodiscard]] StringIndex AppendNew(std::span<const Byte> value)
          {
               const std::uint32_t offset = static_cast<std::uint32_t>(_arena.size());

               _arena.insert(_arena.end(), value.begin(), value.end());
               _arena.push_back(Byte{0});

               const std::uint32_t index = static_cast<std::uint32_t>(_stringTable.size());

               _stringTable.emplace_back(static_cast<std::uint32_t>(value.size()), offset);

               ByteView view{offset, value.size()};
               _exactLookup.emplace(view, index);

               return {.indexInTable = index, .offset = 0};
          }

          std::vector<Byte> _arena;
          std::vector<StringEntry> _stringTable;

          std::unordered_map<ByteView, std::uint32_t, std::hash<ByteView>, std::equal_to<>> _exactLookup;

          std::reference_wrapper<CompilerConfig> _config;
          std::vector<StringPatch> _patches;
     };

     void Compile(CompilerConfig& config, SourceFile& source,
                  const std::vector<ir::NodePointer>& intermediateRepresentation, ecpps::abi::api::Target* target);
} // namespace ecpps::codegen
