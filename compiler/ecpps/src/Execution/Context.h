#pragma once
#include <Queue.h>
#include <SBOVector.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>
#include "../Machine/ABI.h"
#include "../Shared/Diagnostics.h"
#include "../TypeSystem/TypeBase.h"
#include "Shared/BumpAllocator.h"
#include "TypeSystem/ArithmeticTypes.h"
#include "TypeSystem/CompoundTypes.h"

namespace ecpps::ir
{
     struct FunctionScope;

     enum struct TypeKind : std::uint_fast8_t
     {
          Compound,
          Fundamental
     };

     struct StandardSignedIntegerRequest
     {
          typeSystem::TypeKind size{};
          typeSystem::Signedness signedness{};
          bool isCharWithoutSign{};
     };
     struct BoundedArrayRequest
     {
          typeSystem::NonowningTypePointer elementType{};
          std::size_t size{};
     };
     struct PointerRequest
     {
          typeSystem::NonowningTypePointer elementType{};
     };
     using VoidRequest = std::monostate;

     struct TypeRequest
     {
          using VarRequest =
              std::variant<VoidRequest, StandardSignedIntegerRequest, BoundedArrayRequest, PointerRequest>;

          TypeKind kind{};
          typeSystem::Qualifiers qualifiers{};
          VarRequest data{};

          [[nodiscard]] constexpr bool operator==(const TypeRequest& other) const
          {
               if (this->kind != other.kind) return false;
               if (this->qualifiers != other.qualifiers) return false;

               if (std::holds_alternative<StandardSignedIntegerRequest>(this->data))
               {
                    if (!std::holds_alternative<StandardSignedIntegerRequest>(other.data)) return false;
                    const auto& thisData = std::get<StandardSignedIntegerRequest>(this->data);
                    const auto& otherData = std::get<StandardSignedIntegerRequest>(other.data);

                    if (thisData.signedness != otherData.signedness) return false;
                    if (thisData.size != otherData.size) return false;

                    return true;
               }

               if (std::holds_alternative<BoundedArrayRequest>(this->data))
               {
                    if (!std::holds_alternative<BoundedArrayRequest>(other.data)) return false;
                    const auto& thisData = std::get<BoundedArrayRequest>(this->data);
                    const auto& otherData = std::get<BoundedArrayRequest>(other.data);

                    if (thisData.elementType != otherData.elementType) return false;
                    if (thisData.size != otherData.size) return false;

                    return true;
               }

               if (std::holds_alternative<PointerRequest>(this->data))
               {
                    if (!std::holds_alternative<PointerRequest>(other.data)) return false;
                    const auto& thisData = std::get<PointerRequest>(this->data);
                    const auto& otherData = std::get<PointerRequest>(other.data);

                    return thisData.elementType == otherData.elementType;
               }

               if (std::holds_alternative<VoidRequest>(this->data))
               {
                    return std::holds_alternative<VoidRequest>(other.data);
               }

               throw TracedException("Not implemented");
          }
     };
     struct TypeRequestHash
     {
          using is_transparent = void;

          std::size_t operator()(const TypeRequest& value) const noexcept
          {
               auto HashCombine = [](std::size_t seed, const auto& v) constexpr noexcept
               {
                    const std::size_t h = std::hash<std::decay_t<decltype(v)>>{}(v);
                    // 64-bit friendly mix
                    seed ^= h + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
                    return seed;
               };

               std::size_t seed = 0;

               seed = HashCombine(seed, value.kind);
               seed = HashCombine(seed, value.qualifiers);
               seed = HashCombine(seed, value.data.index());

               std::visit(
                   [&](const auto& data)
                   {
                        using T = std::decay_t<decltype(data)>;

                        if constexpr (std::is_same_v<T, StandardSignedIntegerRequest>)
                        {
                             seed = HashCombine(seed, data.size);
                             seed = HashCombine(seed, data.signedness);
                             seed = HashCombine(seed, data.isCharWithoutSign);
                        }
                        else if constexpr (std::is_same_v<T, BoundedArrayRequest>)
                        {
                             seed = HashCombine(seed, data.elementType);
                             seed = HashCombine(seed, data.size);
                        }
                        else if constexpr (std::is_same_v<T, PointerRequest>)
                        {
                             seed = HashCombine(seed, data.elementType);
                        }
                        else if constexpr (std::is_same_v<T, VoidRequest>) {}
                        else
                        {
                             std::terminate(); // keep consistent with your throw
                        }
                   },
                   value.data);

               return seed;
          }
     };

     struct TypeContext
     {
     private:
          struct Node
          {
               typeSystem::OwningTypePointer typePointer{};
               std::size_t hitCount{};
          };
          struct NonownedNode
          {
               typeSystem::NonowningTypePointer typePointer{};
               std::size_t hitCount{};
          };

     public:
          explicit TypeContext(void) = default;
          // typeSystem::TypeId PushType(typeSystem::OwningTypePointer type)
          // {
          //      typeSystem::TypeId id{static_cast<std::uint32_t>(this->_typeDatabase.size())};
          //      this->_typeDatabase.push_back(std::move(type));
          //      return id;
          // }

          typeSystem::NonowningTypePointer Get(const TypeRequest& request)
          {
               // TODO: Atomic? or not?
               if (this->_typeDatabase.contains(request))
               {
                    auto& node = this->_typeDatabase.at(request);
                    std::atomic_ref(node.hitCount).fetch_add(1, std::memory_order::relaxed);
                    return node.typePointer.get();
               }

               auto created = CreateType(request);
               return this->_typeDatabase.emplace(request, Node{.typePointer = std::move(created), .hitCount = 1})
                   .first->second.typePointer.get();
          }

          [[nodiscard]] std::size_t Count(void) const noexcept { return this->_typeDatabase.size(); }
          [[nodiscard]] std::vector<NonownedNode> List(void) const noexcept
          {
               return this->_typeDatabase | std::views::values |
                      std::views::transform(
                          [](const Node& node)
                          { return NonownedNode{.typePointer = node.typePointer.get(), .hitCount = node.hitCount}; }) |
                      std::ranges::to<std::vector>();
          }

          // [[nodiscard]] const typeSystem::TypeBase* Get(typeSystem::TypeId id) const noexcept
          // {
          //      return this->_typeDatabase[id.value].get();
          // }

     private:
          typeSystem::OwningTypePointer CreateType(const TypeRequest& request)
          {
               if (request.kind == TypeKind::Fundamental)
               {
                    if (std::holds_alternative<VoidRequest>(request.data))
                    {
                         return std::make_unique<typeSystem::VoidType>("void", request.qualifiers);
                    }
                    if (std::holds_alternative<StandardSignedIntegerRequest>(request.data))
                    {
                         const auto& data = std::get<StandardSignedIntegerRequest>(request.data);
                         std::string cv{};
                         switch (request.qualifiers)
                         {
                         case typeSystem::Qualifiers::ConstVolatile: cv = "const volatile "; break;
                         case typeSystem::Qualifiers::Const: cv = "const "; break;
                         case typeSystem::Qualifiers::Volatile: cv = "volatile "; break;
                         case typeSystem::Qualifiers::None: break;
                         }
                         if (data.isCharWithoutSign)
                              return std::make_unique<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::Char,
                                                                                 std::format("{}char", cv),
                                                                                 request.qualifiers);
                         switch (data.size)
                         {
                         case typeSystem::TypeKind::Char:
                              return data.signedness == typeSystem::Signedness::Signed
                                         ? std::make_unique<typeSystem::CharacterType>(
                                               ecpps::typeSystem::CharacterSign::SignedChar,
                                               std::format("{}signed char", cv), request.qualifiers)
                                         : std::make_unique<typeSystem::CharacterType>(
                                               ecpps::typeSystem::CharacterSign::UnsignedChar,
                                               std::format("{}unsigned char", cv), request.qualifiers);
                         case typeSystem::TypeKind::Short:
                         case typeSystem::TypeKind::Int:
                         case typeSystem::TypeKind::Long:
                         case typeSystem::TypeKind::LongLong:
                              return std::make_unique<typeSystem::IntegralType>(
                                  data.signedness, data.size,
                                  std::format("{}{}{}", cv,
                                              data.signedness == typeSystem::Signedness::Signed ? "" : "unsigned ",
                                              (
                                                  [size = data.size] -> std::string
                                                  {
                                                       switch (size)
                                                       {
                                                       case typeSystem::TypeKind::Char: return "char";
                                                       case typeSystem::TypeKind::Short: return "short";
                                                       case typeSystem::TypeKind::Int: return "int";
                                                       case typeSystem::TypeKind::Long: return "long";
                                                       case typeSystem::TypeKind::LongLong: return "long long";
                                                       }
                                                       return "?";
                                                  })()),
                                  request.qualifiers);
                         }
                    }
               }
               else // compound
               {
                    if (std::holds_alternative<BoundedArrayRequest>(request.data))
                    {
                         const auto& arrayData = std::get<BoundedArrayRequest>(request.data);
                         return std::make_unique<typeSystem::ArrayType>(arrayData.size, arrayData.elementType);
                    }
                    if (std::holds_alternative<PointerRequest>(request.data))
                    {
                         const auto& pointerData = std::get<PointerRequest>(request.data);
                         return std::make_unique<typeSystem::PointerType>(
                             pointerData.elementType, std::format("{}*", pointerData.elementType->Name()),
                             request.qualifiers);
                    }
               }

               throw TracedException("Invalid data type");
          }

          std::unordered_map<TypeRequest, Node, TypeRequestHash> _typeDatabase{};
     };

     inline TypeContext& GetContext(void)
     {
          static TypeContext typeContext{};
          return typeContext;
     }

     struct Scope
     {
          virtual ~Scope(void) = default;

          Scope* parentScope = nullptr;
          std::unordered_set<typeSystem::NonowningTypePointer, typeSystem::TypePointerHash,
                             typeSystem::TypePointerEqual>
              types{};
          std::vector<std::shared_ptr<FunctionScope>> functions{};
     };
     using ScopePtr = std::shared_ptr<Scope>;

     struct ContextBase
     {
          virtual ~ContextBase(void) = default;

          [[nodiscard]] const Scope& GetScope(void) const noexcept { return *this->_vScope; }
          [[nodiscard]] Scope& GetScope(void) noexcept { return *this->_vScope; }
          template <typename U> [[nodiscard]] U& GetScope(void) noexcept { return *static_cast<U*>(this->_vScope); }

     protected:
          explicit ContextBase(Scope* vScope) : _vScope(vScope) {}

     private:
          Scope* _vScope;
     };
     using ContextPointer = std::shared_ptr<ContextBase>;

     struct FunctionContext final : ContextBase
     {
          explicit FunctionContext(Scope* vScope, typeSystem::NonowningTypePointer returnType)
              : ContextBase(vScope), returnType(returnType)
          {
          }
          abi::CallingConventionName callingConvention{};
          typeSystem::NonowningTypePointer returnType;
          std::string name;
          std::vector<typeSystem::NonowningTypePointer> parameters;

          explicit FunctionContext(Scope* vScope, abi::CallingConventionName callingConvention,
                                   [[maybe_unused]] typeSystem::NonowningTypePointer returnType, std::string name,
                                   std::vector<typeSystem::NonowningTypePointer> parameters)
              : ContextBase(vScope), callingConvention(callingConvention), returnType(returnType),
                name(std::move(name)), parameters(std::move(parameters))
          {
          }
     };
     struct ClassContext final : ContextBase
     {
     };
     struct NamespaceContext final : ContextBase
     {
          explicit NamespaceContext(Scope* vScope) : ContextBase(vScope) {}
     };

     enum struct ConstexprType : std::uint_fast8_t
     {
          None,
          Constexpr,
          Consteval
     };
     struct TemplateParameter
     {
          virtual ~TemplateParameter(void) = default;
          std::string name{};
     };
     struct TemplateTypeParameter final : TemplateParameter
     {
     };
     struct TemplateNonTypeParameter final : TemplateParameter
     {
          typeSystem::NonowningTypePointer type;
          explicit TemplateNonTypeParameter(typeSystem::NonowningTypePointer type) : type(type) {}
     };
     struct FunctionScope final : Scope
     {
          std::string name{};
          typeSystem::NonowningTypePointer returnType{};
          bool isStatic = false;
          bool isInline = false;
          bool isFriend = false;
          bool isExtern = false;
          ConstexprType constexprSpecifier = ConstexprType::None;
          abi::CallingConventionName callingConvention{};
          abi::Linkage linkage{};
          bool isDllImportExport = false;

          struct Parameter
          {
               std::string name{};
               typeSystem::NonowningTypePointer type{};
               bool isVariadic = false;
          };
          std::vector<Parameter> parameters{};
          struct Variable
          {
               std::string name{};
               typeSystem::NonowningTypePointer type{};
               bool isStatic = false;
               bool isExtern = false;
          };

          std::vector<Variable> locals{};

          std::vector<std::unique_ptr<TemplateParameter>> templateParameters{};
     };

     enum struct FunctionScopeBuilderState : std::uint16_t
     {
          None = 0,
          Name = 1,
          ReturnType = 2,
          Parameters = 4,
          CallingConvention = 8,
          Linkage = 16,
          IsStatic = 32,
          IsInline = 64,
          IsFriend = 128,
          IsExtern = 256,
          ConstexprSpecifier = 512,
          IsDllImportExport = 1024,
          All = Name | ReturnType | Parameters | CallingConvention | Linkage | IsStatic | IsInline | IsFriend |
                IsExtern | ConstexprSpecifier | IsDllImportExport
     };
     [[nodiscard]] constexpr FunctionScopeBuilderState operator|(const FunctionScopeBuilderState lhs,
                                                                 const FunctionScopeBuilderState rhs) noexcept
     {
          return static_cast<FunctionScopeBuilderState>(std::to_underlying(lhs) | std::to_underlying(rhs));
     }

     template <FunctionScopeBuilderState TState = FunctionScopeBuilderState::None> struct FunctionScopeBuilder
     {
          FunctionScopeBuilder(const FunctionScopeBuilder&) = delete;
          FunctionScopeBuilder(FunctionScopeBuilder&&) = default;
          FunctionScopeBuilder& operator=(const FunctionScopeBuilder&) = delete;
          FunctionScopeBuilder& operator=(FunctionScopeBuilder&&) = default;
          template <FunctionScopeBuilderState UState> explicit(false) operator FunctionScopeBuilder<UState>(void) &&
          {
               FunctionScopeBuilder<UState> newBuilder{std::exchange(this->_scope, nullptr)};
               return newBuilder;
          }

          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::Name> Name(
              std::string value) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::name>(std::move(value));
          }

          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::ReturnType> ReturnType(
              typeSystem::NonowningTypePointer value) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::returnType>(std::move(value));
          }
          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::Parameters> Parameters(
              std::vector<FunctionScope::Parameter> value) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::parameters>(std::move(value));
          }
          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::CallingConvention> CallingConvention(
              abi::CallingConventionName value) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::callingConvention>(value);
          }
          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::Linkage> Linkage(
              abi::Linkage value) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::linkage>(value);
          }
          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::IsStatic> IsStatic(
              bool value = true) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::isStatic>(value);
          }
          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::IsInline> IsInline(
              bool value = true) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::isInline>(value);
          }
          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::IsFriend> IsFriend(
              bool value = true) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::isFriend>(value);
          }
          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::IsExtern> IsExtern(
              bool value = true) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::isExtern>(value);
          }
          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::ConstexprSpecifier> ConstexprSpecifier(
              ConstexprType value) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::constexprSpecifier>(value);
          }
          [[nodiscard]] FunctionScopeBuilder<TState | FunctionScopeBuilderState::IsDllImportExport> IsDllImportExport(
              bool value = true) && noexcept
          {
               return std::move(*this).template PropertySetter<&FunctionScope::isDllImportExport>(value);
          }

          [[nodiscard]] std::unique_ptr<FunctionScope> Build(void) && noexcept
               requires(TState == FunctionScopeBuilderState::All)
          {
               return std::unique_ptr<FunctionScope>(std::exchange(this->_scope, nullptr));
          }

     private:
          template <auto T> [[nodiscard]] FunctionScopeBuilder PropertySetter(auto&& value) &&
          {
               this->_scope->*T = std::forward<std::remove_reference_t<decltype(value)>>(value);
               return std::move(*this);
          }
          explicit FunctionScopeBuilder(FunctionScope* scope) : _scope(scope) {}
          FunctionScope* _scope;

          template <FunctionScopeBuilderState> friend struct FunctionScopeBuilder;
          friend inline FunctionScopeBuilder<> MakeFunctionScope(void);
     };
     inline FunctionScopeBuilder<> MakeFunctionScope(void) { return FunctionScopeBuilder{new FunctionScope{}}; }

     struct ClassScope final : Scope
     {
          std::string name{};
          std::vector<std::unique_ptr<FunctionScope>> memberFunctions{};
          std::vector<std::unique_ptr<FunctionScope>> staticMemberFunctions{};
          std::vector<std::unique_ptr<ClassScope>> nestedClasses{};
     };

     struct NamespaceScope final : Scope
     {
          std::string name{};
          bool isInline = false;
          std::vector<std::unique_ptr<NamespaceScope>> subNamespaces{};
          std::vector<std::unique_ptr<ClassScope>> classes{};
          explicit NamespaceScope(void) = default;
          explicit NamespaceScope(std::string name, bool isInline = false) : name(std::move(name)), isInline(isInline)
          {
          }
     };

     struct Context
     {
          std::reference_wrapper<Diagnostics> diagnostics;
          BumpAllocator* nodeAllocator;

          ScopePtr globalScope = std::make_unique<NamespaceScope>();
          SBOQueue<ContextPointer> contextSequence{};

          explicit Context(Diagnostics& diagnostics, BumpAllocator& allocator)
              : diagnostics(std::ref(diagnostics)), nodeAllocator(&allocator)
          {
          }
     };
} // namespace ecpps::ir
