#pragma once
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include "Shared/Error.h"
#include "TypeBase.h"

namespace ecpps::typeSystem
{
     using ClassId = std::uint32_t;
     constexpr ClassId InvalidClassId = std::numeric_limits<ClassId>::max();
     enum struct ScopeKind : std::uint_fast8_t
     {
          Namespace,
          Class,
          Function
     };
     struct ScopeSegment
     {
          ScopeKind kind{};
          std::string name{};
     };
     enum struct ClassKind : std::uint_fast8_t
     {
          Class,
          Struct,
          Union
     };
     enum struct Access : std::uint_fast8_t
     {
          Private,
          Protected,
          Public
     };
     enum struct ClassState : std::uint_fast8_t
     {
          Declared,
          Defining,
          Complete
     };

     struct ClassField
     {
          std::string name{}; // empty == anon
          NonowningTypePointer type{};
          Access access{Access::Public};
          bool isAnonymousUnion{}; // [class.union.anon]; TODO: name patching
          bool isMutable{};
          bool hasDefaultInitialiser{};
          std::size_t offset{}; // valid once ::Complete
          Location source{0, 0, 0};
     };
     struct ClassInfo
     {
          ClassId id{InvalidClassId};
          ClassKind kind{ClassKind::Struct};
          std::string name{}; // empty == unnamed
          std::vector<ScopeSegment> enclosingPath{};
          bool isLocal{};
          Location source{0, 0, 0};
          ClassState state{ClassState::Declared};
          std::size_t size{};       // valid once ::Complete
          std::size_t alignment{};  // valid once ::Complete
          bool triviallyCopyable{}; // valid once ::Complete; computed from the member types
          bool hasUserDeclaredConstructors{};
          std::vector<ClassField> fields{};

          std::optional<std::string> instantiationKey{};

          [[nodiscard]] bool IsComplete(void) const noexcept
          {
               return this->state == ClassState::Complete;
          }
          [[nodiscard]] std::vector<ScopeSegment> QualifiedScope(void) const
          {
               auto scope = this->enclosingPath;
               scope.push_back({ScopeKind::Class, this->name});
               return scope;
          }

          [[nodiscard]] std::string Keyword(void) const
          {
               switch (this->kind)
               {
               case ClassKind::Class: return "class";
               case ClassKind::Struct: return "struct";
               case ClassKind::Union: return "union";
               }
               return "class";
          }
          [[nodiscard]] std::string DisplayName(void) const
          {
               std::string result;
               for (const auto& part : this->enclosingPath)
                    result += part.name + (part.kind == ScopeKind::Function ? "()::" : "::");
               return result + (this->name.empty() ? std::format("<unnamed {}>", this->Keyword()) : this->name);
          }
          [[nodiscard]] std::string RawName(void) const
          {
               std::string result;
               for (const auto& part : this->enclosingPath)
                    result += part.name + (part.kind == ScopeKind::Function ? "()::" : "::");
               return result + (this->name.empty() ? std::format("{{unnamed:{}}}", this->id) : this->name);
          }
     };

     struct ClassDeclaration
     {
          ClassKind kind{};
          std::string name{};
          std::vector<ScopeSegment> enclosingPath{};
          bool isLocal{};
          Location source{0, 0, 0};
     };
} // namespace ecpps::typeSystem
