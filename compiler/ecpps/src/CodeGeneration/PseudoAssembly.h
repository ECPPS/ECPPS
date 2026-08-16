#pragma once
#include <functional>
#include <span>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>
#include "../Execution/NodeBase.h"
#include "../Parsing/SourceMap.h"
#include "Machine/Storage.h"
#include "Shared/Config.h"

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
                    ByteView probe{static_cast<std::size_t>(value.data() - this->_arena.data()), value.size()};

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

          std::stack<std::unordered_map<std::string, std::pair<ecpps::abi::StorageRef, ecpps::abi::StorageRequirement>>>
               symbolTables;
          std::vector<ecpps::abi::StorageRef> functionParameters{};
          std::size_t stackFrameAdjustment = 0;

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
                  const std::vector<ir::NodePointer>& intermediateRepresentation);
} // namespace ecpps::codegen
