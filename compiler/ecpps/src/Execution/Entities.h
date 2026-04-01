#pragma once

#include <cstdint>
#include <optional>
#include <string>
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
     struct Entity
     {
          virtual ~Entity(void) = default;

          [[nodiscard]] EntityKind Kind(void) const noexcept { return this->_kind; }
          [[nodiscard]] const std::optional<std::string>& Name(void) const noexcept { return this->_name; }
          [[nodiscard]] virtual std::string ToString(void) const = 0;

     protected:
          explicit Entity(EntityKind kind, std::optional<std::string> name = std::nullopt)
              : _kind(kind), _name(std::move(name))
          {
          }
          void SetName(std::string name) { this->_name = std::move(name); }
          void ClearName(void) { this->_name = std::nullopt; }

     private:
          EntityKind _kind;
          std::optional<std::string> _name;
     };
} // namespace ecpps::ir
