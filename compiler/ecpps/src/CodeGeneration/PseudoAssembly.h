#pragma once
#include <functional>
#include <stack>
#include <string>
#include <unordered_set>
#include <vector>
#include "../Execution/NodeBase.h"
#include "../Parsing/SourceMap.h"
#include "Shared/Config.h"

namespace ecpps::codegen
{
     extern std::unordered_set<std::string> g_functionImports;

     struct AssemblyContext
     {
          using Byte = char8_t;
          using ByteView = std::basic_string_view<Byte>;

          struct alignas(std::uint64_t) StringEntry
          {
               std::uint32_t length{};
               std::uint32_t offset{};
          };

          explicit AssemblyContext(CompilerConfig& config) : _config(std::ref(config)) {}
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
                    ByteView probe{value.data(), value.size()};

                    if (const auto iterator = _exactLookup.find(probe); iterator != _exactLookup.end())
                    {
                         const auto& entry = _stringTable[iterator->second];
                         return {.indexInTable = iterator->second, .offset = entry.offset};
                    }

                    return AppendNew(value);
               }

               case StringPooling::Substring:
               {
                    ByteView probe{value.data(), value.size()};

                    for (const auto& [view, index] : _exactLookup)
                    {
                         if (view.size() >= probe.size())
                         {
                              if (const auto position = view.find(probe); position != ByteView::npos)
                              {
                                   const auto& entry = _stringTable[index];
                                   return {.indexInTable = index,
                                           .offset = entry.offset + static_cast<std::uint32_t>(position)};
                              }
                         }
                    }

                    return AppendNew(value);
               }
               }

               std::unreachable();
          }

          [[nodiscard]] ByteView GetString(StringIndex index) const noexcept
          {
               const auto& entry = _stringTable[index.indexInTable];
               return {this->_arena.data() + entry.offset + index.offset, entry.length};
          }

          [[nodiscard]] std::size_t GetStringOffset(StringIndex index) const noexcept
          {
               const auto& entry = _stringTable[index.indexInTable];
               return entry.offset + index.offset;
          }
          [[nodiscard]] const auto& GetStringSection(void) const noexcept { return this->_arena; }

          void AddStringPatch(std::uint32_t instructionOffset, InstructionPatchType patchType, StringIndex index)
          {
               this->_patches.emplace_back(index, instructionOffset, patchType);
          }

          std::stack<std::unordered_map<std::string, std::pair<ecpps::abi::StorageRef, ecpps::abi::StorageRequirement>>>
              symbolTables;

          [[nodiscard]] auto& Patches(void) noexcept { return this->_patches; }

     private:
          static std::uint32_t ReserveNextStringEntry(void) noexcept
          {
               static std::atomic<std::uint32_t> next = 0;
               return next.fetch_add(1, std::memory_order::relaxed);
          }

          [[nodiscard]] StringIndex AppendNew(std::span<const Byte> value)
          {
               const std::uint32_t offset = static_cast<std::uint32_t>(_arena.size());

               _arena.insert(_arena.end(), value.begin(), value.end());
               _arena.push_back(Byte{0}); // still important

               const std::uint32_t index = static_cast<std::uint32_t>(_stringTable.size());

               _stringTable.emplace_back(offset, static_cast<std::uint32_t>(value.size()));

               ByteView view{_arena.data() + offset, value.size()};
               _exactLookup.emplace(view, index);

               return {.indexInTable = index, .offset = offset};
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
