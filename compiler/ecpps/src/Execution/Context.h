#pragma once
#include <Queue.h>
#include <SBOVector.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include "../Machine/ABI.h"
#include "../Shared/Diagnostics.h"
#include "../TypeSystem/TypeBase.h"

namespace ecpps::ir
{
     struct FunctionScope;

     struct Scope
     {
          virtual ~Scope(void) = default;

          Scope* parentScope = nullptr;
          std::unordered_set<typeSystem::TypePointer, typeSystem::TypePointerHash, typeSystem::TypePointerEqual>
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
          explicit FunctionContext(Scope* vScope, typeSystem::TypePointer returnType)
              : ContextBase(vScope), returnType(std::move(returnType))
          {
          }
          abi::CallingConventionName callingConvention{};
          typeSystem::TypePointer returnType;
          std::string name;
          std::vector<typeSystem::TypePointer> parameters;

          explicit FunctionContext(Scope* vScope, abi::CallingConventionName callingConvention,
                                   [[maybe_unused]] typeSystem::TypePointer returnType, std::string name,
                                   std::vector<typeSystem::TypePointer> parameters)
              : ContextBase(vScope), callingConvention(callingConvention), returnType(std::move(returnType)),
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
          typeSystem::TypePointer type;
          explicit TemplateNonTypeParameter(typeSystem::TypePointer type) : type(std::move(type)) {}
     };
     struct FunctionScope final : Scope
     {
          std::string name{};
          typeSystem::TypePointer returnType{};
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
               typeSystem::TypePointer type{};
               bool isVariadic = false;
          };
          std::vector<Parameter> parameters{};
          struct Variable
          {
               std::string name{};
               typeSystem::TypePointer type{};
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
              typeSystem::TypePointer value) && noexcept
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
