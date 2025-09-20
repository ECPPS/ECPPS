#pragma once
#include <Queue.h>
#include <functional>
#include <memory>
#include <unordered_set>
#include "../TypeSystem/TypeBase.h"
#include <SBOVector.h>
#include <vector>
#include <string>
#include "../Machine/ABI.h"

namespace ecpps::ir
{
     struct FunctionScope;

     struct Scope
     {
          virtual ~Scope(void) = default;

          Scope* parentScope = nullptr;
          std::unordered_set<typeSystem::TypePointer, typeSystem::TypePointerHash, typeSystem::TypePointerEqual>
              types{};
          std::vector<std::unique_ptr<FunctionScope>> functions{};
     };
     using ScopePtr = std::shared_ptr<Scope>;

     struct ContextBase
     {
          virtual ~ContextBase(void) = default;

          [[nodiscard]] const Scope& GetScope(void) const noexcept { return *this->_vScope;  }
          [[nodiscard]] Scope& GetScope(void) noexcept { return *this->_vScope;  }
     protected:
          explicit ContextBase(Scope* vScope) 
               : _vScope(vScope){}

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
          abi::Linkage linkage;
          abi::CallingConventionName callingConvention;
          typeSystem::TypePointer returnType;
          std::string name;
          std::vector<typeSystem::TypePointer> parameters;
          bool isDllImportExport = false;

          explicit FunctionContext(const abi::Linkage linkage, abi::CallingConventionName callingConvention, const typeSystem::TypePointer returnType, std::string name,
                                   std::vector<typeSystem::TypePointer> parameters)
              : linkage(linkage), callingConvention(callingConvention), returnType(std::move(returnType)), name(std::move(name)), parameters(std::move(parameters))
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

     enum struct ConstexprType : std::uint8_t 
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
          explicit TemplateNonTypeParameter(typeSystem::TypePointer type) 
               : type(std::move(type))
          {}
     };
     struct FunctionScope final : Scope
     {
          std::string name{};
          typeSystem::TypePointer returnType{};
          bool isStatic = false;
          bool isInline = false;
          bool isFriend = false;
          bool isExtern = false;
          std::optional<std::string> externLanguageLinkage{};
          ConstexprType constexprSpecifier = ConstexprType::None;
          abi::CallingConventionName callingConvention{};
          struct Parameter
          {
               std::string name{};
               typeSystem::TypePointer type{};
               bool isVariadic = false;
          };
          std::vector<Parameter> parameters{};
          std::vector<std::unique_ptr<TemplateParameter>> templateParameters{};
     };

     struct FunctionScopeBuilder
     {
          FunctionScopeBuilder(const FunctionScopeBuilder&) = delete;
          FunctionScopeBuilder(FunctionScopeBuilder&&) = default;
          FunctionScopeBuilder& operator=(const FunctionScopeBuilder&) = delete;
          FunctionScopeBuilder& operator=(FunctionScopeBuilder&&) = default;

          [[nodiscard]] FunctionScopeBuilder Name(std::string value) && noexcept 
          { 
               this->_scope->name = std::move(value);
               return std::move(*this);
          }

          [[nodiscard]] FunctionScopeBuilder ReturnType(typeSystem::TypePointer value) && noexcept 
          { 
               this->_scope->returnType = std::move(value);
               return std::move(*this);
          }

          [[nodiscard]] std::unique_ptr<FunctionScope> Build(void) && noexcept
          {
               return std::unique_ptr<FunctionScope>(std::exchange(this->_scope, nullptr));
          }

     private:
          explicit FunctionScopeBuilder(FunctionScope* scope) : _scope(scope) {}
          FunctionScope* _scope;

          friend inline FunctionScopeBuilder MakeFunctionScope(void);
     };
     inline FunctionScopeBuilder MakeFunctionScope(void) 
     {
          return FunctionScopeBuilder{new FunctionScope{}};
     }
     
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
          explicit NamespaceScope(std::string name, bool isInline = false)
              : name(std::move(name)), isInline(isInline)
          {
          }
     };

     struct Context
     {
          std::reference_wrapper<Diagnostics> diagnostics;
          
          ScopePtr globalScope = std::make_unique<NamespaceScope>();
          SBOQueue<ContextPointer> contextSequence{};
          Context(const Context&) = default;

          explicit Context(Diagnostics& diagnostics) : diagnostics(std::ref(diagnostics)) {}
     };
} // namespace ecpps::ir