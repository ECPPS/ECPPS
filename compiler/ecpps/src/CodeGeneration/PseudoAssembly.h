#pragma once
#include <format>
#include <functional>
#include <span>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "../Execution/NodeBase.h"
#include "../Parsing/SourceMap.h"
#include "AbstractNodes.h"
#include "CodeGeneration/Nodes.h"
#include "Execution/Operations.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Storage.h"
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

     struct AllocationDescriptor
     {
          enum struct Type : std::uint8_t
          {
               Locked = 0, // highest priority
               HotTemporary,
               HotAllocation,
               Temporary,
               Allocation,
               ColdAllocation, // lowest priority
          };
          std::size_t size;
          std::size_t alignment;
          Type type;
     };
     struct VirtualNotFoundError : std::exception
     {
          VirtualNotFoundError([[maybe_unused]] auto&&... args) // TODO: do something lol
          {
          }
     };
     struct AllocationMap
     {

          std::size_t EmplaceAllocate(std::size_t ssaIndex, std::size_t size, std::size_t alignment,
                                      AllocationDescriptor::Type type)
          {
               auto virtualIndex = _firstFreeEntry++;

               if (this->_descriptorArray.size() <= virtualIndex) this->_descriptorArray.resize(virtualIndex + 1);
               if (this->_descriptorArrayWindow.size() <= ssaIndex) this->_descriptorArrayWindow.resize(ssaIndex + 1);

               this->_descriptorArray[virtualIndex] =
                    AllocationDescriptor{.size = size, .alignment = alignment, .type = type};
               this->_descriptorArrayWindow[ssaIndex] = virtualIndex;

               return virtualIndex;
          }

          [[nodiscard]] std::size_t FindVirtualBySSA(std::size_t ssaIndex)
          {
               if (this->_descriptorArrayWindow.size() <= ssaIndex)
                    throw VirtualNotFoundError(std::logic_error(std::format("Invalid index: {}", ssaIndex)));
               return this->_descriptorArrayWindow[ssaIndex];
          }

          [[nodiscard]] std::size_t FindSSAByVirtual(std::size_t virtualIndex)
          {
               auto remaining = static_cast<decltype(0z)>(this->_descriptorArrayWindow.size());
               auto iterator = this->_descriptorArrayWindow.begin() + (remaining /= 2);
               while (iterator != this->_descriptorArrayWindow.end() &&
                      iterator != this->_descriptorArrayWindow.begin())
               {
                    auto ordering = *iterator <=> virtualIndex;
                    if (is_eq(ordering)) break;
                    if (is_lt(ordering)) iterator += (remaining /= 2);
                    if (is_gt(ordering)) iterator -= (remaining /= 2);
               }
               return static_cast<std::size_t>(iterator - this->_descriptorArrayWindow.begin());
          }
          [[nodiscard]] AllocationDescriptor& GetDescriptorFromSSA(std::size_t ssaIndex)
          {
               return this->_descriptorArray[FindVirtualBySSA(ssaIndex)];
          }
          [[nodiscard]] AllocationDescriptor& GetDescriptorFromVirtual(std::size_t virtualIndex)
          {
               return this->_descriptorArray[virtualIndex];
          }

     private:
          std::vector<std::size_t> _descriptorArrayWindow{};
          std::vector<AllocationDescriptor> _descriptorArray{};
          std::size_t _firstFreeEntry{};
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
          void ParseLoadNode(const ir::SSALoadNode& node);
          explicit ParsingContext(ecpps::abi::ABI& abi);
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
                              // if (const auto position = this->_arena.substr(view.begin, view.Size()).find(probe);
                              // position != std::basic_string_view<Byte>::npos)
                              // {
                              //      return {.indexInTable = index, .offset = static_cast<std::uint32_t>(position)};
                              // }
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
               _arena.push_back(Byte{0}); // still important

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
