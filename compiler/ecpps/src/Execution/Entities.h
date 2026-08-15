#pragma once

#include <cstdint>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "RuntimeAssert.h"
namespace ecpps::ir
{
     enum struct StorageDuration : std::uint8_t
     {
          Static,
          ThreadLocal,
          Automatic,
          Dynamic
     };
     enum struct EntityKind : std::uint8_t
     {
          Object,
          Value,
          Reference,
          Type,
          Function,
          Enumerator,
          ClassMember,
          Namespace,
          Template,
          TemplateSpecialisation,
          Bitfield
     };
     struct EntityStatistics
     {
          std::size_t AddEntry(EntityKind kind, std::optional<std::string> name = std::nullopt)
          {
               this->_entries[kind].emplace_back(std::move(name));
               return this->_entries[kind].size();
          }
          [[nodiscard]] const std::unordered_map<EntityKind, std::vector<std::optional<std::string>>>& Entries(
              void) const noexcept
          {
               return this->_entries;
          }
          void UpdateEntry(EntityKind kind, std::size_t id, std::optional<std::string> name)
          {
               runtime_assert(this->_entries.contains(kind), "Invalid entity kind in UpdateEntry");
               runtime_assert(id > 0 && id <= this->_entries[kind].size(), "Invalid entity ID in UpdateEntry");
               this->_entries[kind][id - 1] = std::move(name);
          }

          [[nodiscard]] std::size_t Count(EntityKind kind) const noexcept
          {
               if (!this->_entries.contains(kind)) return 0;
               return this->_entries.at(kind).size();
          }

          [[nodiscard]] std::size_t Count(void) const noexcept
          {
               return std::accumulate(this->_entries.begin(), this->_entries.end(), 0ULL,
                                      [](std::size_t sum, const auto& pair)
                                      {
                                           return sum + pair.second.size();
                                      });
          }

     private:
          std::unordered_map<EntityKind, std::vector<std::optional<std::string>>> _entries;
     };
     EntityStatistics& GetEntityStatistics(void);
     [[nodiscard]] inline std::string_view EntityKindToString(EntityKind kind) noexcept
     {
          switch (kind)
          {
          case EntityKind::Object: return "Object";
          case EntityKind::Value: return "Value";
          case EntityKind::Reference: return "Reference";
          case EntityKind::Type: return "Type";
          case EntityKind::Function: return "Function";
          case EntityKind::Enumerator: return "Enumerator";
          case EntityKind::ClassMember: return "ClassMember";
          case EntityKind::Namespace: return "Namespace";
          case EntityKind::Template: return "Template";
          case EntityKind::TemplateSpecialisation: return "TemplateSpecialisation";
          case EntityKind::Bitfield: return "Bitfield";
          default: return "Unknown";
          }
     }
     struct Entity
     {
          virtual ~Entity(void);
          Entity(const Entity&) = delete;
          Entity(Entity&&) = default;
          Entity& operator=(const Entity&) = delete;
          Entity& operator=(Entity&&) = default;

          [[nodiscard]] EntityKind Kind(void) const noexcept
          {
               return this->_kind;
          }
          [[nodiscard]] const std::optional<std::string>& Name(void) const noexcept
          {
               return this->_name;
          }
          [[nodiscard]] virtual std::string ToString(void) const = 0;

     protected:
          explicit Entity(EntityKind kind, std::optional<std::string> name = std::nullopt)
              : _name(std::move(name)), _kind(kind)
          {
               this->_id = GetEntityStatistics().AddEntry(kind, this->_name);
          }
          void SetName(std::string name)
          {
               this->_name = std::move(name);
               GetEntityStatistics().UpdateEntry(this->_kind, this->_id, this->_name);
          }
          void ClearName(void)
          {
               this->_name = std::nullopt;
               GetEntityStatistics().UpdateEntry(this->_kind, this->_id, this->_name);
          }

     private:
          std::size_t _id{};
          std::optional<std::string> _name;
          EntityKind _kind;
     };
} // namespace ecpps::ir
