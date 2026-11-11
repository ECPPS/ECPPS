#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "../Machine/ABI.h"
#include "../Machine/Mangling.h"
#include "../Parsing/ASTs/General.h"
#include "../Parsing/ASTs/Type.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "../TypeSystem/CompoundTypes.h"
#include "Constexpr.h"
#include "Context.h"
#include "Execution/Diagnostics.h"
#include "Execution/Entities.h"
#include "Expressions.h"
#include "HighLevel.h"
#include "NodeBase.h"
#include "Operations.h"
#include "OverloadDiagnostics.h"
#include "Parsing/AST.h"
#include "Procedural.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"
#include "TypeSystem/ClassInfo.h"
#include "TypeSystem/ClassType.h"

#include "IR.Internal.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;
using ClassProperty = ecpps::typeSystem::ClassInfo::Property;

using ecpps::ir::detail::ConversionCost;
using ecpps::ir::detail::DeclaratorIdentifier;
using ecpps::ir::detail::Derivation;
using ecpps::ir::detail::LastDerivation;
using ecpps::ir::detail::QualifiersFrom;
using ecpps::ir::detail::ToAccess;
using ecpps::ir::detail::ToClassKind;

namespace
{
     [[nodiscard]] std::string ConstructorAccessName(const ecpps::typeSystem::Access access)
     {
          switch (access)
          {
          case ecpps::typeSystem::Access::Public: return "public";
          case ecpps::typeSystem::Access::Protected: return "protected";
          case ecpps::typeSystem::Access::Private: return "private";
          }
          return "inaccessible";
     }

     [[nodiscard]] ecpps::ir::overloadDiagnostics::Candidate ConstructorCandidateDiagnostic(
          const ecpps::ir::ClassConstructor& constructor, const bool accessible)
     {
          ecpps::ir::overloadDiagnostics::Candidate candidate{
               .signature = ecpps::ir::detail::FormatFunctionSignature(constructor.function),
               .source = constructor.function->source,
               .isDeleted = constructor.bodyKind == ecpps::ast::FunctionBodyKind::Deleted};
          if (!accessible) candidate.access = ConstructorAccessName(constructor.access);
          return candidate;
     }
} // namespace

void ecpps::ir::IR::CheckClassKey(const typeSystem::ClassInfo& info, const ast::ClassType& node)
{
     const auto key = ToClassKind(node.Key());
     if (key == info.kind) return;
     const bool unionMismatch = (key == typeSystem::ClassKind::Union) != (info.kind == typeSystem::ClassKind::Union);
     if (unionMismatch)
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      std::format("'{}' was declared as a {} but is redeclared as a {}",
                                                  info.DisplayName(), info.Keyword(), node.ToString(0)),
                                      node.Source()));
     else // TODO: Check if MS ABI, since that is ABI violating and shall reject the program
          DiagnosticSink::Report(
               this->GetContext(),
               std::make_unique<diagnostics::Warning>(
                    "warning",
                    std::format("'{}' was declared as a {} but is redeclared with a different class-key",
                                info.DisplayName(), info.Keyword()),
                    node.Source()));
}

ecpps::typeSystem::ClassId ecpps::ir::IR::DeclareClassInScope(Scope& scope, const ast::ClassType& node)
{
     const std::string name{node.Value().View()};
     typeSystem::ClassDeclaration declaration{};
     declaration.kind = ToClassKind(node.Key());
     declaration.name = name;
     declaration.enclosingPath = EnclosingPathUpTo(scope);
     declaration.isLocal = dynamic_cast<const FunctionScope*>(&scope) != nullptr;
     declaration.source = node.Source();

     const auto id = GetTypeContext().DeclareClass(std::move(declaration));
     if (!name.empty()) scope.classes.emplace(name, id);
     return id;
}

ecpps::typeSystem::ClassId ecpps::ir::IR::ResolveElaboratedClass(const ast::ClassType& node)
{
     const std::string name{node.Value().View()};
     if (name.empty())
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                                      "An elaborated type specifier requires a class name", node.Source()));
          return typeSystem::InvalidClassId;
     }
     if (const auto found = LookupClassForElaboration(name, node.Source()); found.has_value())
     {
          if (*found != typeSystem::InvalidClassId) CheckClassKey(GetTypeContext().Class(*found), node);
          return *found;
     }
     return DeclareClassInScope(NearestNamespaceOrBlockScope(), node);
     // TODO: the `friend class X;` form has other rules ([namespace.memdef]/3). Just.. eh
}

ecpps::typeSystem::ClassId ecpps::ir::IR::DefineClass(const ast::ClassDefNode& node)
{
     const std::string name{node.Value().View()};
     auto& types = GetTypeContext();
     auto& scope = this->GetContext().contextSequence.back()->GetScope();

     typeSystem::ClassId id{};
     if (const auto it = scope.classes.find(name); !name.empty() && it != scope.classes.end())
     {
          id = it->second;
          CheckClassKey(types.Class(id), node);
          if (types.Class(id).state != typeSystem::ClassState::Declared)
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Redefinition of '{}'", types.Class(id).DisplayName()), node.Source()));
               return id;
          }
     }
     else
          id = DeclareClassInScope(scope, node);

     types.BeginDefinition(id);

     auto classScope = std::make_unique<ClassScope>();
     classScope->name = name;
     classScope->id = id;
     classScope->parentScope = &scope;
     if (!name.empty()) classScope->classes.emplace(name, id); // injected-class-name
     auto* rawScope = classScope.get();
     scope.classScopes.push_back(std::move(classScope));
     this->GetContext().classTable->classes[id].scope = rawScope;

     this->GetContext().contextSequence.push_back(std::make_shared<ClassContext>(rawScope, id));

     auto access = node.Key() == ast::ClassKey::Class ? typeSystem::Access::Private : typeSystem::Access::Public;
     std::vector<typeSystem::ClassField> fields{};
     std::vector<DeferredMemberInitialiser> deferredInitialisers{};
     std::vector<DeferredFunctionBody> deferredFunctionBodies{};
     for (const auto& member : node.MemberSpecifiers())
          ParseClassMember(member, id, access, fields, deferredInitialisers, deferredFunctionBodies);

     types.Complete(id, std::move(fields));

     DeclareImplicitCopyMoveConstructors(id);
     MarkDefaultedCopyMoveConstructorsDeleted(id);

     for (const auto& initialiser : deferredInitialisers)
          StoreDefaultMemberInitialiser(initialiser.classId, initialiser.fieldIndex, *initialiser.declarator,
                                        initialiser.memberType, initialiser.source);
     for (const auto& body : deferredFunctionBodies)
     {
          if (body.isConstructor)
               EmitConstructorBody(body.function, body.classId, body.declarator->location,
                                   std::span<const ast::NodePointer>{body.declarator->body});
          else
               EmitMemberFunctionBody(*body.declarator, body.function, body.classId);
     }

     this->GetContext().contextSequence.pop_back();
     return id;
}

void ecpps::ir::IR::DeclareImplicitCopyMoveConstructors(const typeSystem::ClassId id)
{
     auto& info = GetTypeContext().Class(id);
     auto& classRecord = this->GetContext().classTable->classes[id];

     bool copyParameterIsConst = true;
     for (const auto& field : info.fields)
     {
          if (field.isMutable) continue;
          auto memberType = field.type;
          while (const auto* arrayType = memberType->CastTo<typeSystem::ArrayType>())
               memberType = arrayType->ElementType();
          const auto* memberClass = memberType->CastTo<typeSystem::ClassType>();
          if (memberClass == nullptr) continue;
          const auto memberRecord = this->GetContext().classTable->classes.find(memberClass->Info().id);
          bool hasConstCopyConstructor = false;
          if (memberRecord != this->GetContext().classTable->classes.end())
               for (const auto& candidate : memberRecord->second.constructors)
               {
                    if (!candidate.isCopyConstructor || candidate.parameters.size() != 1) continue;
                    const auto* reference = candidate.parameters.front().type->CastTo<typeSystem::ReferenceType>();
                    if (reference == nullptr || reference->GetKind() != typeSystem::ReferenceType::Kind::LValue)
                         continue;
                    const auto* qualified = reference->BaseType()->CastTo<typeSystem::QualifiedType>();
                    if (qualified != nullptr && qualified->IsConst())
                    {
                         hasConstCopyConstructor = true;
                         break;
                    }
               }
          if (!hasConstCopyConstructor) copyParameterIsConst = false;
     }

     const auto* classType =
          GetTypeContext().Get(TypeRequest{.kind = TypeKind::Compound, .data = ClassRequest{.id = id}});

     for (std::size_t index = 0; index < 2; index++)
     {
          const bool isCopyConstructor = index == 0;
          if (isCopyConstructor && info.Has(ClassProperty::UserDeclaredCopyConstructor)) continue;
          if (!isCopyConstructor &&
              (info.Has(ClassProperty::UserDeclaredCopyConstructor) ||
               info.Has(ClassProperty::UserDeclaredMoveConstructor) ||
               info.Has(ClassProperty::UserDeclaredCopyAssignment) ||
               info.Has(ClassProperty::UserDeclaredMoveAssignment) || info.Has(ClassProperty::UserDeclaredDestructor)))
               continue;

          const auto* parameterBase = isCopyConstructor && copyParameterIsConst
                                           ? AddQualifiers(classType, typeSystem::Qualifiers::Const)
                                           : classType;
          const auto* parameterType = GetTypeContext().Get(TypeRequest{
               .kind = TypeKind::Compound,
               .data = ReferenceRequest{.elementType = parameterBase,
                                        .kind = isCopyConstructor ? typeSystem::ReferenceType::Kind::LValue
                                                                  : typeSystem::ReferenceType::Kind::RValue}});

          ClassConstructor constructor{};
          constructor.access = typeSystem::Access::Public;
          constructor.bodyKind = isCopyConstructor && info.Has(ClassProperty::UserDeclaredMoveConstructor)
                                      ? ast::FunctionBodyKind::Deleted
                                      : ast::FunctionBodyKind::Defaulted;
          constructor.isCopyConstructor = isCopyConstructor;
          constructor.isMoveConstructor = !isCopyConstructor;
          constructor.isImplicitlyDeclared = true;
          constructor.parameters.push_back({.name = "source", .type = parameterType, .isVariadic = false});

          auto function = std::make_shared<FunctionScope>();
          function->SetName(info.name);
          function->returnType = typeSystem::g_void.get();
          function->callingConvention = abi::ABI::Current().DefaultCallingConventionName();
          function->linkage = abi::Linkage::External;
          function->isInline = true;
          function->parameters = constructor.parameters;
          function->source = info.source;
          function->namespacePath = NamespacePathFromContext();
          function->parentScope = classRecord.scope;
          function->memberClassInfo = &info;
          function->memberAccess = constructor.access;
          function->isDeleted = constructor.bodyKind == ast::FunctionBodyKind::Deleted;
          function->isDefaulted = true;
          function->isMemberFunctionDefined = true;
          constructor.function = std::move(function);
          classRecord.constructors.push_back(std::move(constructor));
     }
}

void ecpps::ir::IR::ParseClassMember(const ast::NodePointer& member, const typeSystem::ClassId id,
                                     typeSystem::Access& access, std::vector<typeSystem::ClassField>& fields,
                                     std::vector<DeferredMemberInitialiser>& deferredInitialisers,
                                     std::vector<DeferredFunctionBody>& deferredFunctionBodies)
{
     // auto& classScope = this->GetContext().contextSequence.back()->GetScope<ClassScope>();

     if (const auto* spec = dynamic_cast<const ast::AccessSpecifierNode*>(member.get()))
     {
          access = ToAccess(spec->Value());
          return;
     }
     if (const auto* declaration = dynamic_cast<const ast::MemberDeclarationNode*>(member.get()))
     {
          ParseMemberDeclaration(*declaration, id, access, fields, deferredInitialisers, deferredFunctionBodies);
          return;
     }
     if (const auto* nested = dynamic_cast<const ast::ClassType*>(member.get())) // in case the parser emits it bare
     {
          ParseNestedClass(*nested, access, fields);
          return;
     }
     if (const auto* alias = dynamic_cast<const ast::TypeAliasNode*>(member.get()))
     {
          ParseNode(member);
          this->GetContext().contextSequence.back()->GetScope<ClassScope>().memberAccess[alias->AliasName()->ToString(
               0)] = access;
          return;
     }

     throw TracedException("TODO: unsupported member-declaration");
}

void ecpps::ir::IR::ParseClassNode(const ast::ClassType& node)
{
     if (const auto* definition = dynamic_cast<const ast::ClassDefNode*>(&node))
     {
          DefineClass(*definition);
          return;
     }
     auto& scope = this->GetContext().contextSequence.back()->GetScope();
     if (node.Value().empty())
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                                                          "A class declaration requires a name", node.Source()));
          return;
     }
     const std::string name{node.Value().View()};
     if (const auto it = scope.classes.find(name); it != scope.classes.end())
     {
          CheckClassKey(GetTypeContext().Class(it->second), node);
          return;
     }
     DeclareClassInScope(scope, node);
}

void ecpps::ir::IR::ParseNestedClass(const ast::ClassType& nested, const typeSystem::Access access,
                                     std::vector<typeSystem::ClassField>& fields)
{
     auto& classScope = this->GetContext().contextSequence.back()->GetScope<ClassScope>();
     const auto* definition = dynamic_cast<const ast::ClassDefNode*>(&nested);

     if (nested.Value().empty())
     {
          if (definition == nullptr || nested.Key() != ast::ClassKey::Union)
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A declaration must declare something; only anonymous unions may be unnamed members",
                         nested.Source()));
               return;
          }
          const auto anonymousId = DefineClass(*definition);
          typeSystem::ClassField field{};
          field.type =
               GetTypeContext().Get(TypeRequest{.kind = TypeKind::Compound, .data = ClassRequest{.id = anonymousId}});
          field.access = access;
          field.isAnonymousUnion = true;
          field.source = nested.Source();
          fields.push_back(std::move(field));
          return;
     }

     ParseClassNode(nested);
     classScope.memberAccess[std::string(nested.Value().View())] = access;
}

void ecpps::ir::IR::ParseMemberDeclaration(const ast::MemberDeclarationNode& node, const typeSystem::ClassId id,
                                           const typeSystem::Access access, std::vector<typeSystem::ClassField>& fields,
                                           std::vector<DeferredMemberInitialiser>& deferredInitialisers,
                                           std::vector<DeferredFunctionBody>& deferredFunctionBodies)
{
     const auto& specifiers = node.Specifiers();
     auto& classScope = this->GetContext().contextSequence.back()->GetScope<ClassScope>();
     const auto report = [&](std::string message, const Location& source)
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          std::move(message), source));
     };

     if (specifiers.isFriend) throw TracedException("TODO: friend declarations are not implemented yet");
     if (specifiers.definingTypeSpecifier == nullptr)
     {
          const auto className = GetTypeContext().Class(id).name;
          if (node.Declarators().size() != 1 || !node.Declarators().front().IsFunction())
          {
               report("Expected a constructor declaration", node.Source());
               return;
          }

          const auto& declarator = node.Declarators().front();
          const auto* identifier = DeclaratorIdentifier(declarator);
          const std::string destructorName = "~" + className;
          if (identifier != nullptr && identifier->Value().View() == destructorName)
          {
               if (specifiers.functionSpecifier.isVirtual ||
                   (specifiers.storageSpecifier & ast::StorageClassSpecifier::Static))
               {
                    report("A destructor cannot be virtual or static in this compiler yet", node.Source());
                    return;
               }
               const ast::ParametersAndQualifiers* functionSuffix = nullptr;
               for (const auto& suffix : declarator.suffixes)
                    if (const auto* parameters = std::get_if<ast::ParametersAndQualifiers>(&suffix))
                    {
                         functionSuffix = parameters;
                         break;
                    }
               if (functionSuffix == nullptr || !functionSuffix->parameters.empty())
               {
                    report("A destructor must have an empty parameter list", node.Source());
                    return;
               }

               auto& classInfo = GetTypeContext().Class(id);
               auto& classRecord = this->GetContext().classTable->classes[id];
               if (classRecord.destructor != nullptr)
               {
                    report("A destructor is declared more than once", node.Source());
                    return;
               }
               classInfo.Set(ClassProperty::UserDeclaredDestructor);
               classInfo.Set(ClassProperty::NontrivialDestructor,
                             classInfo.Has(ClassProperty::NontrivialDestructor) ||
                                  declarator.functionBody != ast::FunctionBodyKind::Defaulted);
               std::shared_ptr<FunctionScope> function =
                    MakeFunctionScope()
                         .Name(destructorName)
                         .ReturnType(typeSystem::g_void.get())
                         .CallingConvention(abi::ABI::Current().DefaultCallingConventionName())
                         .Linkage(abi::Linkage::External)
                         .IsInline(declarator.functionBody == ast::FunctionBodyKind::Block ||
                                   declarator.functionBody == ast::FunctionBodyKind::Defaulted)
                         .IsStatic(false)
                         .IsFriend(false)
                         .IsExtern(false)
                         .ConstexprSpecifier(ConstexprType::None)
                         .IsDllImportExport(false)
                         .Parameters({})
                         .Source(identifier->Source())
                         .NamespacePath(NamespacePathFromContext())
                         .Build();
               function->parameters.clear();
               function->parentScope = &classScope;
               function->memberClassInfo = &classInfo;
               function->memberAccess = access;
               function->isDeleted = declarator.functionBody == ast::FunctionBodyKind::Deleted;
               function->isDefaulted = declarator.functionBody == ast::FunctionBodyKind::Defaulted;
               function->isMemberFunctionDefined = declarator.functionBody != ast::FunctionBodyKind::None;
               classRecord.destructor = function;
               classRecord.destructorAccess = access;
               classRecord.destructorBodyKind = declarator.functionBody;
               classScope.functions.push_back(function);
               classScope.memberAccess[destructorName] = access;
               if (declarator.functionBody == ast::FunctionBodyKind::Block)
               {
                    deferredFunctionBodies.push_back(
                         {.classId = id, .declarator = &declarator, .function = function, .isConstructor = false});
               }
               return;
          }
          if (identifier == nullptr)
          {
               report(std::format("Invalid constructor or destructor name in class '{}', expected '{}' or '~{}'",
                                  className, className, className),
                      node.Source());
               return;
          }
          const auto memberName = identifier->Value().View();
          if (memberName != className)
          {
               if (memberName.starts_with('~'))
                    report(std::format("Invalid destructor name '{}' in class '{}', expected '~{}'", memberName,
                                       className, className),
                           node.Source());
               else
                    report(std::format("Invalid constructor name '{}' in class '{}', expected '{}'", memberName,
                                       className, className),
                           node.Source());
               return;
          }
          if (specifiers.functionSpecifier.isVirtual ||
              specifiers.storageSpecifier & ast::StorageClassSpecifier::Static)
          {
               report("A constructor cannot be virtual or static", node.Source());
               return;
          }

          const ast::ParametersAndQualifiers* functionSuffix = nullptr;
          for (const auto& suffix : declarator.suffixes)
               if (const auto* parameters = std::get_if<ast::ParametersAndQualifiers>(&suffix))
               {
                    functionSuffix = parameters;
                    break;
               }
          if (functionSuffix == nullptr)
          {
               report("Expected constructor parameter list", node.Source());
               return;
          }

          ClassConstructor constructor{};
          constructor.access = access;
          constructor.isExplicit = specifiers.functionSpecifier.explicitSpecifier.has_value();
          constructor.bodyKind = declarator.functionBody;
          for (const auto& parameter : functionSuffix->parameters)
          {
               if (parameter.defaultInitialiser != nullptr)
               {
                    report("Default constructor arguments are not supported yet",
                           parameter.defaultInitialiser->Source());
                    return;
               }
               const auto* parameterType = ParseType(parameter.type);
               if (parameterType == nullptr)
               {
                    report("Invalid constructor parameter type", parameter.type->Source());
                    return;
               }
               constructor.parameters.push_back({.name = parameter.name != nullptr ? parameter.name->ToString(0) : "",
                                                 .type = parameterType,
                                                 .isVariadic = false});
          }
          if (constructor.parameters.size() == 1 && typeSystem::g_void->CommonWith(constructor.parameters.front().type))
               constructor.parameters.clear();

          auto& classInfo = GetTypeContext().Class(id);
          classInfo.Set(ClassProperty::UserDeclaredConstructors);
          if (constructor.parameters.size() == 1)
               if (const auto* reference = constructor.parameters.front().type->CastTo<typeSystem::ReferenceType>())
                    if (const auto* parameterClass = reference->BaseType()->CastTo<typeSystem::ClassType>();
                        parameterClass != nullptr && parameterClass->Info().id == id)
                    {
                         constructor.isCopyConstructor =
                              reference->GetKind() == typeSystem::ReferenceType::Kind::LValue;
                         constructor.isMoveConstructor =
                              reference->GetKind() == typeSystem::ReferenceType::Kind::RValue;
                         classInfo.Set(ClassProperty::UserDeclaredCopyConstructor,
                                       classInfo.Has(ClassProperty::UserDeclaredCopyConstructor) ||
                                            constructor.isCopyConstructor);
                         classInfo.Set(ClassProperty::UserDeclaredMoveConstructor,
                                       classInfo.Has(ClassProperty::UserDeclaredMoveConstructor) ||
                                            constructor.isMoveConstructor);
                         classInfo.Set(ClassProperty::NontrivialCopyOrMoveConstructor,
                                       classInfo.Has(ClassProperty::NontrivialCopyOrMoveConstructor) ||
                                            ((constructor.isCopyConstructor || constructor.isMoveConstructor) &&
                                             declarator.functionBody != ast::FunctionBodyKind::Defaulted &&
                                             declarator.functionBody != ast::FunctionBodyKind::Deleted));
                    }
          auto& constructors = this->GetContext().classTable->classes[id].constructors;
          const bool duplicate =
               std::ranges::any_of(constructors,
                                   [&](const ClassConstructor& existing)
                                   {
                                        if (existing.parameters.size() != constructor.parameters.size()) return false;
                                        for (std::size_t index = 0; index < constructor.parameters.size(); index++)
                                             if (existing.parameters[index].type != constructor.parameters[index].type)
                                                  return false;
                                        return true;
                                   });
          if (duplicate)
          {
               report("Constructor is declared more than once", node.Source());
               return;
          }

          auto function =
               MakeFunctionScope()
                    .Name(className)
                    .ReturnType(typeSystem::g_void.get())
                    .CallingConvention(abi::ABI::Current().DefaultCallingConventionName())
                    .Linkage(abi::Linkage::External)
                    .IsInline(specifiers.isInline || declarator.functionBody == ast::FunctionBodyKind::Block ||
                              declarator.functionBody == ast::FunctionBodyKind::Defaulted)
                    .IsStatic(false)
                    .IsFriend(false)
                    .IsExtern(false)
                    .ConstexprSpecifier(ConstexprType::None)
                    .IsDllImportExport(false)
                    .NamespacePath(NamespacePathFromContext())
                    .Parameters(constructor.parameters)
                    .Source(identifier->Source())
                    .Build();
          function->parameters = constructor.parameters;
          function->parentScope = &classScope;
          function->memberClassInfo = &GetTypeContext().Class(id);
          function->memberAccess = access;
          function->isDeleted = declarator.functionBody == ast::FunctionBodyKind::Deleted;
          function->isDefaulted = declarator.functionBody == ast::FunctionBodyKind::Defaulted;
          function->isMemberFunctionDefined = declarator.functionBody != ast::FunctionBodyKind::None;
          constructor.function = std::shared_ptr<FunctionScope>(std::move(function));

          if (declarator.functionBody == ast::FunctionBodyKind::Block ||
              declarator.functionBody == ast::FunctionBodyKind::Defaulted)
          {
               if ((constructor.isCopyConstructor || constructor.isMoveConstructor) &&
                   declarator.functionBody == ast::FunctionBodyKind::Defaulted)
               {
                    constructors.push_back(std::move(constructor));
                    return;
               }
               deferredFunctionBodies.push_back({.classId = id,
                                                 .declarator = &declarator,
                                                 .function = constructor.function,
                                                 .isConstructor = true});
          }
          constructors.push_back(std::move(constructor));
          return;
     }

     const auto* classSpecifier = dynamic_cast<const ast::ClassType*>(specifiers.definingTypeSpecifier.get());

     if (node.Declarators().empty())
     {
          if (classSpecifier != nullptr) ParseNestedClass(*classSpecifier, access, fields);
          else
               report("A declaration must declare something", node.Source());
          return;
     }

     const bool isMutable = specifiers.storageSpecifier & ast::StorageClassSpecifier::Mutable;

     const auto* declaredType = ParseType(specifiers.definingTypeSpecifier);
     if (declaredType == nullptr)
     {
          report(std::format("Unknown type {} in member declaration", specifiers.definingTypeSpecifier->ToString(0)),
                 node.Source());
          return;
     }
     if (const auto* definition = dynamic_cast<const ast::ClassDefNode*>(classSpecifier);
         definition != nullptr && !definition->Value().empty())
          classScope.memberAccess[std::string(definition->Value().View())] = access;

     for (const auto& declarator : node.Declarators())
     {
          const auto* identifier = DeclaratorIdentifier(declarator);
          const ast::Declarator* declaratorId = &declarator;
          while (declaratorId->nested != nullptr) declaratorId = declaratorId->nested.get();
          const auto* operatorId =
               dynamic_cast<const ast::OperatorFunctionId*>(declaratorId->declaredIdExpression.get());
          if (identifier == nullptr && operatorId == nullptr)
          {
               report("Expected an unqualified identifier in member declarator", node.Source());
               continue;
          }
          if (operatorId != nullptr && operatorId->GetOperator() != ast::Operator::Assignment)
          {
               report("Only operator= member functions are supported yet", declarator.location);
               continue;
          }
          const std::string name = identifier != nullptr ? std::string{identifier->Value().View()}
                                                         : "operator" + ast::ToString(operatorId->GetOperator());
          const Location functionNameSource = identifier != nullptr ? identifier->Source() : declarator.location;

          if (!specifiers.isTypedef && LastDerivation(declarator) == Derivation::Function)
          {
               if (specifiers.storageSpecifier & ast::StorageClassSpecifier::ThreadLocal ||
                   specifiers.storageSpecifier & ast::StorageClassSpecifier::Extern || isMutable)
               {
                    report("Invalid storage class specifier on a member function", node.Source());
                    continue;
               }
               if (specifiers.functionSpecifier.isVirtual || declarator.isOverride || declarator.isFinal ||
                   declarator.isPure)
               {
                    report("Virtual member functions are not supported yet", functionNameSource);
                    continue;
               }
               if (specifiers.functionSpecifier.explicitSpecifier.has_value())
               {
                    report("'explicit' is only supported on constructors", functionNameSource);
                    continue;
               }
               if (specifiers.constexprSpecifier != ast::ConstantExpressionSpecifier::None)
               {
                    report("'constexpr', 'consteval' and 'constinit' member functions are not supported",
                           functionNameSource);
                    continue;
               }
               if (name == "main")
               {
                    report("'main' cannot be declared as a member function", functionNameSource);
                    continue;
               }

               const auto suffix =
                    std::ranges::find_if(declarator.suffixes,
                                         [](const ast::DeclaratorSuffix& item)
                                         {
                                              return std::holds_alternative<ast::ParametersAndQualifiers>(item);
                                         });
               if (suffix == declarator.suffixes.end())
               {
                    report("Expected a parameter list for member function", functionNameSource);
                    continue;
               }
               const auto& functionType = std::get<ast::ParametersAndQualifiers>(*suffix);
               if (declarator.nested != nullptr)
               {
                    report("Nested member function return declarators are not supported yet", functionNameSource);
                    continue;
               }
               const auto functionSuffixIndex = static_cast<std::size_t>(suffix - declarator.suffixes.begin());
               const auto resolvedReturnType =
                    ResolveDeclaratorType(declarator, declaredType, name, false, functionSuffixIndex);
               if (resolvedReturnType.failed || resolvedReturnType.type == nullptr) continue;
               if (resolvedReturnType.type->CastTo<typeSystem::ArrayType>() != nullptr)
               {
                    report("A function cannot return an array type", functionNameSource);
                    continue;
               }
               const auto* memberFunctionReturnType = resolvedReturnType.type;
               const bool isStatic = specifiers.storageSpecifier & ast::StorageClassSpecifier::Static;
               if (isStatic && (functionType.isConst || functionType.isVolatile ||
                                functionType.refQualifier != ast::RefQualifier::None))
               {
                    report("A static member function cannot have cv- or ref-qualifiers", functionNameSource);
                    continue;
               }
               if (functionType.isVariadic)
               {
                    report("Variadic member functions are not supported", functionNameSource);
                    continue;
               }

               std::vector<FunctionScope::Parameter> parameters{};
               parameters.reserve(functionType.parameters.size());
               bool invalidParameters = false;
               for (const auto& parameter : functionType.parameters)
               {
                    if (parameter.explicitThis == ast::ExplicitThisSpecifier::Yes)
                    {
                         report("Explicit object parameters are not supported", parameter.type->Source());
                         invalidParameters = true;
                         break;
                    }
                    if (parameter.defaultInitialiser != nullptr)
                    {
                         report("Default member function arguments are not supported yet",
                                parameter.defaultInitialiser->Source());
                         invalidParameters = true;
                         break;
                    }
                    const auto* parameterType = ParseType(parameter.type);
                    if (parameterType == nullptr ||
                        !RequireComplete(parameterType, parameter.type->Source(), "a member function parameter"))
                    {
                         invalidParameters = true;
                         break;
                    }
                    parameters.emplace_back(parameter.name != nullptr ? parameter.name->ToString(0) : "", parameterType,
                                            false);
               }
               if (invalidParameters) continue;
               if (parameters.size() == 1 && parameters.front().type == typeSystem::g_void.get()) parameters.clear();
               bool isCopyAssignment = false;
               bool isMoveAssignment = false;
               if (name == "operator=" && parameters.size() == 1)
                    if (const auto* reference = parameters.front().type->CastTo<typeSystem::ReferenceType>())
                         if (const auto* parameterClass = reference->BaseType()->CastTo<typeSystem::ClassType>();
                             parameterClass != nullptr && parameterClass->Info().id == id)
                         {
                              isCopyAssignment = reference->GetKind() == typeSystem::ReferenceType::Kind::LValue;
                              isMoveAssignment = reference->GetKind() == typeSystem::ReferenceType::Kind::RValue;
                              auto& classInfo = GetTypeContext().Class(id);
                              classInfo.Set(ClassProperty::UserDeclaredCopyAssignment,
                                            classInfo.Has(ClassProperty::UserDeclaredCopyAssignment) ||
                                                 isCopyAssignment);
                              classInfo.Set(ClassProperty::UserDeclaredMoveAssignment,
                                            classInfo.Has(ClassProperty::UserDeclaredMoveAssignment) ||
                                                 isMoveAssignment);
                              classInfo.Set(ClassProperty::NontrivialCopyOrMoveAssignment,
                                            classInfo.Has(ClassProperty::NontrivialCopyOrMoveAssignment) ||
                                                 ((isCopyAssignment || isMoveAssignment) &&
                                                  declarator.functionBody != ast::FunctionBodyKind::Defaulted &&
                                                  declarator.functionBody != ast::FunctionBodyKind::Deleted));
                         }
               if (!typeSystem::g_void->CommonWith(memberFunctionReturnType) &&
                   !RequireComplete(memberFunctionReturnType, node.Source(), "a member function return type"))
                    continue;

               const bool isSpecialAssignment = isCopyAssignment || isMoveAssignment;
               if (declarator.functionBody == ast::FunctionBodyKind::Defaulted && !isSpecialAssignment)
               {
                    report("Only special member functions may be defaulted", functionNameSource);
                    continue;
               }

               abi::MemberFunctionInfo memberInfo{};
               memberInfo.isStatic = isStatic;
               memberInfo.isConst = functionType.isConst;
               memberInfo.isVolatile = functionType.isVolatile;
               if (functionType.refQualifier == ast::RefQualifier::LValue)
                    memberInfo.refQualifier = abi::MemberRefQualifier::LValue;
               else if (functionType.refQualifier == ast::RefQualifier::RValue)
                    memberInfo.refQualifier = abi::MemberRefQualifier::RValue;

               auto& memberFunctions = classScope.functions;
               const auto sameSignature = [&](const std::shared_ptr<FunctionScope>& existing)
               {
                    if (existing->Name() != name || existing->memberClassInfo == nullptr ||
                        existing->memberFunctionInfo.isConst != memberInfo.isConst ||
                        existing->memberFunctionInfo.isVolatile != memberInfo.isVolatile ||
                        existing->memberFunctionInfo.refQualifier != memberInfo.refQualifier ||
                        existing->parameters.size() != parameters.size())
                         return false;
                    for (std::size_t index = 0; index < parameters.size(); index++)
                         if (existing->parameters[index].type != parameters[index].type) return false;
                    return true;
               };
               auto existing = std::ranges::find_if(memberFunctions, sameSignature);
               std::shared_ptr<FunctionScope> function{};
               if (existing != memberFunctions.end())
               {
                    function = *existing;
                    if (function->isStatic != isStatic)
                    {
                         report("A static and non-static member function cannot have the same signature",
                                functionNameSource);
                         continue;
                    }
                    if (declarator.functionBody == ast::FunctionBodyKind::Block && function->isMemberFunctionDefined)
                    {
                         report("Member function is defined more than once", functionNameSource);
                         continue;
                    }
                    if (function->returnType != memberFunctionReturnType)
                    {
                         report("Member function redeclaration has a different return type", functionNameSource);
                         continue;
                    }
                    function->isInline = function->isInline || specifiers.isInline;
               }
               else
               {
                    auto functionScope =
                         MakeFunctionScope()
                              .Name(name)
                              .ReturnType(memberFunctionReturnType)
                              .CallingConvention(abi::ABI::Current().DefaultCallingConventionName())
                              .Linkage(abi::Linkage::External)
                              .IsInline(specifiers.isInline || declarator.functionBody == ast::FunctionBodyKind::Block)
                              .IsStatic(isStatic)
                              .IsFriend(false)
                              .IsExtern(false)
                              .ConstexprSpecifier(specifiers.constexprSpecifier ==
                                                            ast::ConstantExpressionSpecifier::Constexpr
                                                       ? ConstexprType::Constexpr
                                                       : ConstexprType::None)
                              .IsDllImportExport(false)
                              .Parameters(parameters)
                              .Source(functionNameSource)
                              .NamespacePath(NamespacePathFromContext())
                              .Build();
                    functionScope->parameters = parameters;
                    functionScope->parentScope = &classScope;
                    functionScope->memberClassInfo = &GetTypeContext().Class(id);
                    functionScope->returnsByPointer = typeSystem::IsClass(memberFunctionReturnType);
                    functionScope->memberFunctionInfo = memberInfo;
                    functionScope->memberAccess = access;
                    memberFunctions.push_back(std::move(functionScope));
                    function = memberFunctions.back();
               }

               function->returnsByPointer = typeSystem::IsClass(memberFunctionReturnType);
               function->isDefaulted = declarator.functionBody == ast::FunctionBodyKind::Defaulted;
               classScope.memberAccess[name] = access;
               if (declarator.functionBody == ast::FunctionBodyKind::Deleted)
               {
                    function->isDeleted = true;
                    function->isMemberFunctionDefined = true;
               }
               else if (declarator.functionBody == ast::FunctionBodyKind::Block)
               {
                    function->isInline = true;
                    function->isMemberFunctionDefined = true;
                    deferredFunctionBodies.push_back(
                         {.classId = id, .declarator = &declarator, .function = function, .isConstructor = false});
               }
               else if (declarator.functionBody == ast::FunctionBodyKind::Defaulted)
                    function->isMemberFunctionDefined = true;
               continue;
          }

          if (specifiers.storageSpecifier & ast::StorageClassSpecifier::Static)
               throw TracedException("TODO: static data members are not implemented yet");
          if ((specifiers.storageSpecifier & ast::StorageClassSpecifier::ThreadLocal) ||
              (specifiers.storageSpecifier & ast::StorageClassSpecifier::Extern))
          {
               report("'thread_local' and 'extern' cannot be applied to a non-static data member", node.Source());
               continue;
          }
          if (specifiers.isInline || specifiers.functionSpecifier.isVirtual ||
              specifiers.functionSpecifier.explicitSpecifier.has_value() ||
              specifiers.constexprSpecifier != ast::ConstantExpressionSpecifier::None)
          {
               report(
                    "'inline', 'virtual', 'explicit', 'constexpr', 'consteval' and 'constinit' cannot be applied to a "
                    "non-static data member or a typedef",
                    node.Source());
               continue;
          }
          if (declarator.isBitField) throw TracedException("TODO: bit-fields are not implemented yet");

          const auto resolved = ResolveDeclaratorType(declarator, declaredType, name, false);
          if (resolved.failed || resolved.type == nullptr) continue;

          if (specifiers.isTypedef)
          {
               if (const auto existing = classScope.typeAliases.find(name);
                   existing != classScope.typeAliases.end() && existing->second != resolved.type)
               {
                    report("Redefinition of '" + name + "' as a different type", node.Source());
                    continue;
               }
               classScope.typeAliases[name] = resolved.type;
               classScope.memberAccess[name] = access;
               continue;
          }

          if (!RequireComplete(resolved.type, node.Source(), "a non-static data member")) continue;

          if (isMutable)
               if (const auto* qualified = resolved.type->CastTo<typeSystem::QualifiedType>();
                   qualified != nullptr && qualified->IsConst())
               {
                    report("A 'mutable' member cannot have a const-qualified type", node.Source());
                    continue;
               }

          if (std::ranges::any_of(fields,
                                  [&](const typeSystem::ClassField& f)
                                  {
                                       return f.name == name;
                                  }))
          {
               report("Duplicate member '" + name + "'", identifier->Source());
               continue;
          }

          typeSystem::ClassField field{};
          field.name = name;
          field.type = resolved.type;
          field.access = access;
          field.isMutable = isMutable;
          field.source = identifier->Source();
          field.hasDefaultInitialiser = declarator.initialiser != nullptr;
          fields.push_back(std::move(field));
          const auto fieldIndex = fields.size() - 1;
          classScope.memberAccess[name] = access;

          if (declarator.initialiser == nullptr) continue;
          if (declarator.initialisationType == ast::InitialisationType::ParenthesisedExpressionList)
          {
               report("A default member initialiser must use '=' or braces", declarator.initialiser->Source());
               continue;
          }

          deferredInitialisers.push_back({.classId = id,
                                          .fieldIndex = fieldIndex,
                                          .declarator = &declarator,
                                          .memberType = resolved.type,
                                          .source = identifier->Source()});
     }
}

void ecpps::ir::IR::EmitConstructorBody(const std::shared_ptr<FunctionScope>& function, const typeSystem::ClassId id,
                                        const Location source, const std::span<const ast::NodePointer> body)
{
     if (function == nullptr) return;

     IR functionIr{this->_context};
     functionIr.GetContext() = this->GetContext();

     const auto* thisObjectType =
          GetTypeContext().Get(TypeRequest{.kind = TypeKind::Compound, .data = ClassRequest{.id = id}});
     const auto* thisType = PointerTo(thisObjectType);
     auto functionContext = std::make_shared<FunctionContext>(function.get(), function->callingConvention,
                                                              function->returnType, function->Name().value_or(""),
                                                              function->parameters |
                                                                   std::views::transform(
                                                                        [](const FunctionScope::Parameter& parameter)
                                                                        {
                                                                             return parameter.type;
                                                                        }) |
                                                                   ecpps::detail::ToVector);
     functionContext->thisType = thisType;

     struct RestoreContextSequence
     {
          Context* context{};
          std::size_t size{};

          ~RestoreContextSequence(void)
          {
               this->context->contextSequence.resize(this->size);
          }
     };
     RestoreContextSequence restoreContextSequence{.context = &functionIr.GetContext(),
                                                   .size = functionIr.GetContext().contextSequence.size()};
     functionIr.GetContext().contextSequence.push_back(functionContext);

     auto locals = std::make_shared<std::vector<FunctionScope::LocalEntity>>();
     locals->reserve(function->parameters.size() + 1);
     locals->push_back(FunctionScope::LocalEntity{FunctionScope::LocalEntity::ThisTag{}});
     function->SetLocals(locals);

     functionIr.EmitThisPrologue(thisType, source);
     std::uint64_t parameterIndex = 1;
     for (const auto& parameter : function->parameters)
     {
          locals->emplace_back(Variable{parameter.name, parameter.type, StorageDuration::Automatic});

          auto* activeFunction = dynamic_cast<FunctionContext*>(functionIr.GetContext().contextSequence.back().get());
          runtime_assert(activeFunction != nullptr, "Expected constructor function context during parameter emission");
          RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
          const auto* classType = parameter.type->CastTo<typeSystem::ClassType>();
          const auto width = classType != nullptr ? abi::ABI::Current().PointerSize() * typeSystem::CharWidth
                                                  : parameter.type->Width();
          const auto alignment = classType != nullptr ? abi::ABI::Current().PointerSize() : parameter.type->Alignment();
          auto allocation = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator)
                    SingleAssignRegisterNode(activeFunction->GetNextRegisterIndex(), info, width, alignment, source)};
          auto* allocationPointer = allocation.get();
          activeFunction->RegisterParamAllocReg(parameter.name, allocationPointer);
          functionIr._built.push_back(std::unique_ptr<AllocationNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator) AllocationNode(std::move(allocation), source)});
          functionIr._built.push_back(std::unique_ptr<ParameterNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator)
                    ParameterNode(function.get(), parameterIndex++, allocationPointer, source)});
          functionIr.RegisterByValueParameterForDestruction(parameter.type, allocationPointer, source);
     }

     Expression thisExpression =
          std::make_unique<PRValue>(thisType,
                                    std::unique_ptr<high::ThisExpressionNode, IRDeleter>{
                                         new (*functionIr.GetContext().nodeAllocator) high::ThisExpressionNode(source)},
                                    false);
     const auto* objectAddress = functionIr.LowerExpressionLoaded(std::move(thisExpression), functionIr._built);
     if (objectAddress == nullptr) return;

     functionIr._objectInitialisationStack.push_back(
          {.type = thisObjectType, .address = objectAddress, .allowLocalLookup = true});
     struct RestoreObjectInitialisation
     {
          std::vector<ActiveObjectInitialisation>* stack{};

          ~RestoreObjectInitialisation(void)
          {
               this->stack->pop_back();
          }
     } restoreObjectInitialisation{.stack = &functionIr._objectInitialisationStack};

     functionIr.InitialiseClassMembers(thisObjectType, objectAddress, source);
     for (const auto& statement : body) functionIr.ParseNode(statement);
     if (functionIr._built.empty() || functionIr._built.back()->Kind() != NodeKind::Return)
     {
          functionIr.EmitTemporaryDestructorsFrom(0);
          functionIr.EmitDestructorsFrom(0);
          functionIr._built.push_back(std::unique_ptr<SSAReturnNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator) SSAReturnNode(nullptr, source)});
     }

     this->_built.push_back(std::unique_ptr<ProcedureNode, IRDeleter>{
          new (*this->GetContext().nodeAllocator)
               ProcedureNode(function.get(), std::move(locals), std::move(functionIr._built), source)});
}

void ecpps::ir::IR::EmitMemberFunctionBody(const ast::MemberDeclarator& declarator,
                                           const std::shared_ptr<FunctionScope>& function, const typeSystem::ClassId id)
{
     if (function == nullptr || declarator.functionBody != ast::FunctionBodyKind::Block) return;

     IR functionIr{this->_context};
     functionIr.GetContext() = this->GetContext();

     typeSystem::NonowningTypePointer thisType{};
     if (!function->isStatic)
     {
          const auto* classType =
               GetTypeContext().Get(TypeRequest{.kind = TypeKind::Compound,
                                                .qualifiers = QualifiersFrom(function->memberFunctionInfo.isConst,
                                                                             function->memberFunctionInfo.isVolatile),
                                                .data = ClassRequest{.id = id}});
          thisType = PointerTo(classType);
     }

     auto functionContext = std::make_shared<FunctionContext>(function.get(), function->callingConvention,
                                                              function->returnType, function->Name().value_or(""),
                                                              function->parameters |
                                                                   std::views::transform(
                                                                        [](const FunctionScope::Parameter& parameter)
                                                                        {
                                                                             return parameter.type;
                                                                        }) |
                                                                   ecpps::detail::ToVector);
     functionContext->thisType = thisType;

     struct RestoreContextSequence
     {
          Context* context{};
          std::size_t size{};

          ~RestoreContextSequence(void)
          {
               this->context->contextSequence.resize(this->size);
          }
     };
     RestoreContextSequence restoreContextSequence{.context = &functionIr.GetContext(),
                                                   .size = functionIr.GetContext().contextSequence.size()};
     functionIr.GetContext().contextSequence.push_back(functionContext);

     auto locals = std::make_shared<std::vector<FunctionScope::LocalEntity>>();
     locals->reserve(function->parameters.size() + static_cast<std::size_t>(!function->isStatic));
     if (!function->isStatic) locals->push_back(FunctionScope::LocalEntity{FunctionScope::LocalEntity::ThisTag{}});
     function->SetLocals(locals);

     std::uint64_t returnBufferParameterIndex{};
     if (function->returnsByPointer)
     {
          auto* activeFunction = dynamic_cast<FunctionContext*>(functionIr.GetContext().contextSequence.back().get());
          runtime_assert(activeFunction != nullptr, "Expected member function context during return buffer emission");
          RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
          auto buffer = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator)
                    SingleAssignRegisterNode(activeFunction->GetNextRegisterIndex(), info,
                                             abi::ABI::Current().PointerSize() * typeSystem::CharWidth,
                                             abi::ABI::Current().PointerSize(), declarator.location)};
          auto* bufferPointer = buffer.get();
          activeFunction->returnBuffer = bufferPointer;
          functionIr._built.push_back(std::unique_ptr<AllocationNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator) AllocationNode(std::move(buffer), declarator.location)});
          functionIr._built.push_back(std::unique_ptr<ParameterNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator)
                    ParameterNode(function.get(), 0, bufferPointer, declarator.location)});
          returnBufferParameterIndex = 1;
     }

     if (!function->isStatic) functionIr.EmitThisPrologue(thisType, declarator.location);
     std::uint64_t parameterIndex = returnBufferParameterIndex + static_cast<std::uint64_t>(!function->isStatic);
     for (const auto& parameter : function->parameters)
     {
          Variable variable{parameter.name, parameter.type, StorageDuration::Automatic};
          locals->emplace_back(std::move(variable));

          auto* activeFunction = dynamic_cast<FunctionContext*>(functionIr.GetContext().contextSequence.back().get());
          runtime_assert(activeFunction != nullptr, "Expected member function context during parameter emission");
          RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
          const auto* classType = parameter.type->CastTo<typeSystem::ClassType>();
          const auto width = classType != nullptr ? abi::ABI::Current().PointerSize() * typeSystem::CharWidth
                                                  : parameter.type->Width();
          const auto alignment = classType != nullptr ? abi::ABI::Current().PointerSize() : parameter.type->Alignment();
          auto allocation = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator) SingleAssignRegisterNode(
                    activeFunction->GetNextRegisterIndex(), info, width, alignment, declarator.location)};
          auto* allocationPointer = allocation.get();
          activeFunction->RegisterParamAllocReg(parameter.name, allocationPointer);
          functionIr._built.push_back(std::unique_ptr<AllocationNode, IRDeleter>{new (
               *functionIr.GetContext().nodeAllocator) AllocationNode(std::move(allocation), declarator.location)});
          functionIr._built.push_back(std::unique_ptr<ParameterNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator)
                    ParameterNode(function.get(), parameterIndex++, allocationPointer, declarator.location)});
          functionIr.RegisterByValueParameterForDestruction(parameter.type, allocationPointer, declarator.location);
     }

     for (const auto& statement : declarator.body) functionIr.ParseNode(statement);
     const bool fallsThrough = functionIr._built.empty() || functionIr._built.back()->Kind() != NodeKind::Return;
     if (fallsThrough)
     {
          functionIr.EmitTemporaryDestructorsFrom(0);
          functionIr.EmitDestructorsFrom(0);
     }
     if (fallsThrough && function->Name().has_value() && function->Name()->starts_with("~"))
     {
          Expression thisExpression = std::make_unique<PRValue>(
               thisType,
               std::unique_ptr<high::ThisExpressionNode, IRDeleter>{new (*functionIr.GetContext().nodeAllocator)
                                                                         high::ThisExpressionNode(declarator.location)},
               false);
          const auto* objectAddress = functionIr.LowerExpressionLoaded(std::move(thisExpression), functionIr._built);
          if (objectAddress != nullptr)
               functionIr.DestroyClassMembersAtAddress(GetTypeContext().Class(id), objectAddress);
     }
     if (fallsThrough && typeSystem::g_void->CommonWith(function->returnType))
          functionIr._built.push_back(std::unique_ptr<SSAReturnNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator) SSAReturnNode(nullptr, declarator.location)});
     else if (fallsThrough)
     {
          DiagnosticSink::Report(
               functionIr.GetContext(),
               std::make_unique<diagnostics::Warning>(
                    "warning",
                    std::format("control may reach the end of non-void function '{}' without returning a value",
                                function->Name().value_or("__unknown_func")),
                    declarator.location));
          functionIr._built.push_back(std::unique_ptr<SSAUnreachableNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator) SSAUnreachableNode(declarator.location)});
     }

     this->_built.push_back(std::unique_ptr<ProcedureNode, IRDeleter>{
          new (*this->GetContext().nodeAllocator)
               ProcedureNode(function.get(), std::move(locals), std::move(functionIr._built), declarator.location)});
}

ecpps::ir::IR::DefaultMemberInitialiserValidator::DefaultMemberInitialiserValidator(IR& ir,
                                                                                    const typeSystem::ClassId ownerId,
                                                                                    Location source,
                                                                                    const std::size_t diagnosticCount)
    : _ir(ir), _ownerId(ownerId), _source(source), _diagnosticCount(diagnosticCount)
{
}

void ecpps::ir::IR::DefaultMemberInitialiserValidator::Report(std::string message, const Location& at)
{
     DiagnosticSink::Report(_ir.GetContext(),
                            diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(std::move(message), at));
}

bool ecpps::ir::IR::DefaultMemberInitialiserValidator::ValidateConstructor(
     const typeSystem::ClassType* classType, const std::vector<const ast::NodePointer*>& argumentNodes,
     const bool copyList, const bool isListInitialisation, const Location& at)
{
     const auto classRecord = _ir.GetContext().classTable->classes.find(classType->Info().id);
     if (classRecord == _ir.GetContext().classTable->classes.end() || classRecord->second.constructors.empty())
     {
          DiagnosticSink::Report(_ir.GetContext(), ecpps::ir::overloadDiagnostics::NoMatchingConstructor(
                                                        classType->Info().DisplayName(), at));
          return false;
     }

     std::vector<Expression> arguments{};
     for (const auto* argumentNode : argumentNodes)
     {
          if (argumentNode == nullptr || *argumentNode == nullptr ||
              dynamic_cast<const ast::InitialiserListNode*>(argumentNode->get()) != nullptr)
          {
               Report("Nested braced constructor arguments are not supported yet", at);
               return false;
          }
          auto argument = _ir.ParseExpression(*argumentNode);
          if (argument == nullptr) return false;
          arguments.push_back(std::move(argument));
     }

     const ClassConstructor* selected = nullptr;
     std::vector<const ClassConstructor*> bestCandidates{};
     std::size_t bestScore = std::numeric_limits<std::size_t>::max();
     bool ambiguous = false;
     for (const auto& candidate : classRecord->second.constructors)
     {
          if (candidate.isMoveConstructor && candidate.function != nullptr && candidate.function->isDefaulted &&
              candidate.bodyKind == ast::FunctionBodyKind::Deleted)
               continue;
          if (candidate.parameters.size() != arguments.size()) continue;
          std::size_t score = 0;
          bool viable = true;
          for (std::size_t index = 0; index < arguments.size(); index++)
          {
               const auto* parameterType = candidate.parameters[index].type;
               const auto& argument = arguments[index];
               if (parameterType->CastTo<typeSystem::ClassType>() != nullptr ||
                   parameterType->CastTo<typeSystem::ArrayType>() != nullptr)
               {
                    viable = false;
                    break;
               }
               if (const auto* reference = parameterType->CastTo<typeSystem::ReferenceType>())
               {
                    const auto* target = reference->BaseType()->CastTo<typeSystem::QualifiedType>();
                    const auto* sourceType = argument->Type()->CastTo<typeSystem::QualifiedType>();
                    const auto* targetIntegral = reference->BaseType()->CastTo<typeSystem::IntegralType>();
                    const auto* sourceIntegral = argument->Type()->CastTo<typeSystem::IntegralType>();
                    const bool same = reference->BaseType() == argument->Type() ||
                                      (targetIntegral != nullptr && sourceIntegral != nullptr &&
                                       targetIntegral->Sign() == sourceIntegral->Sign() &&
                                       targetIntegral->Kind() == sourceIntegral->Kind() &&
                                       IsBoolean(reference->BaseType()) == IsBoolean(argument->Type()));
                    if (same && sourceType != nullptr &&
                        ((sourceType->IsConst() && (target == nullptr || !target->IsConst())) ||
                         (sourceType->IsVolatile() && (target == nullptr || !target->IsVolatile()))))
                    {
                         viable = false;
                         break;
                    }
                    const bool lvalueReference = reference->GetKind() == typeSystem::ReferenceType::Kind::LValue;
                    const bool directCategory =
                         lvalueReference
                              ? argument->IsLValue() || (target != nullptr && target->IsConst() && argument->IsXValue())
                              : argument->IsXValue();
                    const bool convertible = !same && reference->BaseType()->CompareTo(argument->Type()).IsValid();
                    const bool bindTemp = (!lvalueReference || (target != nullptr && target->IsConst())) &&
                                          (argument->IsRValue() || convertible);
                    if (!(same && directCategory) && !bindTemp)
                    {
                         viable = false;
                         break;
                    }
                    score += bindTemp ? 8 : 0;
                    continue;
               }
               const auto conversion = parameterType->CompareTo(argument->Type());
               if (!conversion.IsValid())
               {
                    viable = false;
                    break;
               }
               for (const auto kind : conversion.Sequence()) score += ConversionCost(kind);
          }
          if (!viable) continue;
          if (score < bestScore)
          {
               selected = &candidate;
               bestScore = score;
               ambiguous = false;
               bestCandidates.assign(1, &candidate);
          }
          else if (score == bestScore)
          {
               ambiguous = true;
               bestCandidates.push_back(&candidate);
          }
     }
     if (selected == nullptr || ambiguous)
     {
          std::vector<ecpps::ir::overloadDiagnostics::Candidate> diagnosticCandidates{};
          if (ambiguous)
          {
               diagnosticCandidates.reserve(bestCandidates.size());
               for (const auto* candidate : bestCandidates)
                    diagnosticCandidates.push_back(
                         ConstructorCandidateDiagnostic(*candidate, candidate->access == typeSystem::Access::Public ||
                                                                         classType->Info().id == _ownerId));
          }
          else
          {
               diagnosticCandidates.reserve(classRecord->second.constructors.size());
               for (const auto& candidate : classRecord->second.constructors)
                    if (!candidate.isImplicitlyDeclared)
                         diagnosticCandidates.push_back(ConstructorCandidateDiagnostic(
                              candidate,
                              candidate.access == typeSystem::Access::Public || classType->Info().id == _ownerId));
          }
          auto diagnostic = ambiguous ? ecpps::ir::overloadDiagnostics::AmbiguousConstructor(
                                             classType->Info().DisplayName(), at, diagnosticCandidates)
                                      : ecpps::ir::overloadDiagnostics::NoMatchingConstructor(
                                             classType->Info().DisplayName(), at, diagnosticCandidates);
          DiagnosticSink::Report(_ir.GetContext(), std::move(diagnostic));
          return false;
     }
     if (copyList && selected->isExplicit)
     {
          Report("Copy-list initialisation cannot select an explicit constructor", at);
          return false;
     }
     if (isListInitialisation)
          for (std::size_t index = 0; index < arguments.size(); index++)
          {
               const auto* parameterType = selected->parameters[index].type;
               if (const auto* reference = parameterType->CastTo<typeSystem::ReferenceType>())
                    parameterType = reference->BaseType();
               if (!_ir.IsNarrowingConversion(arguments[index], parameterType)) continue;
               Report(std::format("Narrowing conversion from '{}' to '{}' is not allowed in list "
                                  "initialisation",
                                  arguments[index]->Type()->Name(), parameterType->Name()),
                      (*argumentNodes[index])->Source());
               return false;
          }
     if (selected->access != typeSystem::Access::Public && classType->Info().id != _ownerId)
     {
          DiagnosticSink::Report(_ir.GetContext(), ecpps::ir::overloadDiagnostics::SelectedConstructorFailure(
                                                        "Selected constructor is not accessible", at,
                                                        ConstructorCandidateDiagnostic(*selected, false)));
          return false;
     }
     if (selected->bodyKind == ast::FunctionBodyKind::Deleted)
     {
          DiagnosticSink::Report(_ir.GetContext(), ecpps::ir::overloadDiagnostics::SelectedConstructorFailure(
                                                        "The selected constructor is deleted or unavailable", at,
                                                        ConstructorCandidateDiagnostic(*selected, true)));
          return false;
     }
     return true;
}

bool ecpps::ir::IR::DefaultMemberInitialiserValidator::ValidateEmpty(typeSystem::NonowningTypePointer type)
{
     if (type->CastTo<typeSystem::ReferenceType>() != nullptr)
     {
          Report("An omitted reference member has no initialiser", _source);
          return false;
     }
     if (const auto* array = type->CastTo<typeSystem::ArrayType>()) return ValidateEmpty(array->ElementType());
     if (const auto* classType = type->CastTo<typeSystem::ClassType>())
     {
          if (classType->Info().kind == typeSystem::ClassKind::Union)
          {
               Report("Union initialisation is not supported yet", _source);
               return false;
          }
          if (!_ir.IsAggregateClass(type) && classType->Info().Has(ClassProperty::UserDeclaredConstructors))
          {
               return ValidateConstructor(classType, {}, true, true, _source);
          }
          bool valid = true;
          for (const auto& field : classType->Info().fields)
               if (!field.hasDefaultInitialiser) valid = ValidateEmpty(field.type) && valid;
          return valid;
     }
     return true;
}

bool ecpps::ir::IR::DefaultMemberInitialiserValidator::ValidateClassList(const typeSystem::ClassType* classType,
                                                                         const ast::InitialiserListNode& list,
                                                                         const bool copyList)
{
     if (classType->Info().kind == typeSystem::ClassKind::Union)
     {
          Report("Union initialisation is not supported yet", list.Source());
          return false;
     }
     if (!_ir.IsAggregateClass(classType) && classType->Info().Has(ClassProperty::UserDeclaredConstructors))
     {
          std::vector<const ast::NodePointer*> arguments{};
          for (const auto& clause : list.Initialisers()) arguments.push_back(&clause);
          return ValidateConstructor(classType, arguments, copyList, true, list.Source());
     }
     if (!_ir.IsAggregateClass(classType) && list.Initialisers().empty()) return ValidateEmpty(classType);
     if (!_ir.IsAggregateClass(classType))
     {
          Report(std::format("Class '{}' is not an aggregate and has no matching constructor",
                             classType->Info().DisplayName()),
                 list.Source());
          return false;
     }

     const auto& fields = classType->Info().fields;
     const auto& clauses = list.Initialisers();
     const bool hasDesignators =
          std::ranges::any_of(clauses,
                              [](const ast::NodePointer& clause)
                              {
                                   return dynamic_cast<const ast::DesignatedInitialiserNode*>(clause.get()) != nullptr;
                              });
     if (hasDesignators)
     {
          std::vector<const ast::NodePointer*> explicitInitialisers(fields.size(), nullptr);
          std::size_t previousIndex = 0;
          bool hasPrevious = false;
          for (const auto& clause : clauses)
          {
               const auto* designated = dynamic_cast<const ast::DesignatedInitialiserNode*>(clause.get());
               if (designated == nullptr)
               {
                    Report("Positional and designated initialisers cannot be mixed", clause->Source());
                    return false;
               }
               std::optional<std::size_t> fieldIndex;
               for (std::size_t index = 0; index < fields.size(); index++)
                    if (fields[index].name == designated->Member())
                    {
                         fieldIndex = index;
                         break;
                    }
               if (!fieldIndex.has_value())
               {
                    Report(std::format("'{}' is not a direct data member of '{}'", designated->Member(),
                                       classType->Info().DisplayName()),
                           designated->Source());
                    return false;
               }
               if (explicitInitialisers[*fieldIndex] != nullptr)
               {
                    Report(std::format("Member '{}' is initialised more than once", designated->Member()),
                           designated->Source());
                    return false;
               }
               if (hasPrevious && *fieldIndex <= previousIndex)
               {
                    Report("Designated initialisers must follow member declaration order", designated->Source());
                    return false;
               }
               previousIndex = *fieldIndex;
               hasPrevious = true;
               explicitInitialisers[*fieldIndex] = &designated->Initialiser();
          }

          bool valid = true;
          for (std::size_t index = 0; index < fields.size(); index++)
          {
               if (explicitInitialisers[index] == nullptr)
               {
                    if (!fields[index].hasDefaultInitialiser) valid = ValidateEmpty(fields[index].type) && valid;
                    continue;
               }
               const auto& value = *explicitInitialisers[index];
               const auto initType = dynamic_cast<const ast::InitialiserListNode*>(value.get()) != nullptr
                                          ? ast::InitialisationType::BracedInitialiser
                                          : ast::InitialisationType::EqualInitialiser;
               valid = Validate(fields[index].type, value, initType, true) && valid;
          }
          return valid;
     }

     std::size_t nextClause = 0;
     std::function<bool(typeSystem::NonowningTypePointer, bool)> validateSubobject;
     std::function<bool(const typeSystem::ClassType*)> validatePositionalClass;
     validatePositionalClass = [&](const typeSystem::ClassType* nested)
     {
          bool valid = true;
          for (const auto& field : nested->Info().fields)
               valid = validateSubobject(field.type, field.hasDefaultInitialiser) && valid;
          return valid;
     };
     validateSubobject = [&](typeSystem::NonowningTypePointer subobjectType, const bool hasDefaultInitialiser)
     {
          if (nextClause >= clauses.size()) return hasDefaultInitialiser || ValidateEmpty(subobjectType);

          const auto& clause = clauses[nextClause];
          if (dynamic_cast<const ast::InitialiserListNode*>(clause.get()) != nullptr)
          {
               nextClause++;
               return Validate(subobjectType, clause, ast::InitialisationType::BracedInitialiser, true);
          }
          if (const auto* array = subobjectType->CastTo<typeSystem::ArrayType>())
          {
               bool valid = true;
               for (std::size_t index = 0; index < array->ElementCount() && nextClause < clauses.size(); index++)
               {
                    const auto before = nextClause;
                    valid = validateSubobject(array->ElementType(), false) && valid;
                    if (nextClause == before) break;
               }
               if (nextClause >= clauses.size() && array->ElementCount() > 0)
                    valid = ValidateEmpty(array->ElementType()) && valid;
               return valid;
          }
          if (const auto* nested = subobjectType->CastTo<typeSystem::ClassType>();
              nested != nullptr && _ir.IsAggregateClass(subobjectType))
          {
               if (nested->Info().fields.empty())
               {
                    Report("An empty aggregate subobject cannot be skipped during brace elision", clause->Source());
                    return false;
               }
               return validatePositionalClass(nested);
          }

          nextClause++;
          return Validate(subobjectType, clause, ast::InitialisationType::EqualInitialiser, true);
     };

     const bool valid = validatePositionalClass(classType);
     if (nextClause < clauses.size())
     {
          Report("Too many initialisers for aggregate class", clauses[nextClause]->Source());
          return false;
     }
     return valid;
}

bool ecpps::ir::IR::DefaultMemberInitialiserValidator::Validate(typeSystem::NonowningTypePointer type,
                                                                const ast::NodePointer& value,
                                                                const ast::InitialisationType initialisationType,
                                                                const bool aggregateElement)
{
     if (type->CastTo<typeSystem::ReferenceType>() != nullptr)
     {
          _ir.InitialiseReferenceAtAddress(type, nullptr, &value, initialisationType, value->Source(),
                                           aggregateElement);
          return DiagnosticSink::Count(_ir.GetContext()) == _diagnosticCount;
     }
     if (const auto* array = type->CastTo<typeSystem::ArrayType>())
     {
          const auto* list = dynamic_cast<const ast::InitialiserListNode*>(value.get());
          if (list == nullptr)
          {
               Report("An array default member initialiser must use a braced initialiser", value->Source());
               return false;
          }
          if (list->Initialisers().size() > array->ElementCount())
          {
               Report("Too many initialisers for array member", value->Source());
               return false;
          }
          bool valid = true;
          const auto explicitCount = std::min(array->ElementCount(), list->Initialisers().size());
          for (std::size_t index = 0; index < explicitCount; index++)
          {
               const auto& clause = list->Initialisers()[index];
               const auto initType = dynamic_cast<const ast::InitialiserListNode*>(clause.get()) != nullptr
                                          ? ast::InitialisationType::BracedInitialiser
                                          : ast::InitialisationType::EqualInitialiser;
               valid = Validate(array->ElementType(), clause, initType, true) && valid;
          }
          if (array->ElementCount() > explicitCount) valid = ValidateEmpty(array->ElementType()) && valid;
          return valid;
     }
     if (const auto* classType = type->CastTo<typeSystem::ClassType>())
     {
          const auto* list = dynamic_cast<const ast::InitialiserListNode*>(value.get());
          if (list == nullptr)
          {
               if (classType->Info().Has(ClassProperty::UserDeclaredConstructors))
                    return ValidateConstructor(classType, {&value},
                                               initialisationType == ast::InitialisationType::EqualInitialiser, false,
                                               value->Source());
               Report(std::format("Class '{}' needs constructor or copy initialisation support",
                                  classType->Info().DisplayName()),
                      value->Source());
               return false;
          }
          return ValidateClassList(classType, *list, initialisationType == ast::InitialisationType::EqualInitialiser);
     }

     const ast::NodePointer* scalarValue = &value;
     if (const auto* list = dynamic_cast<const ast::InitialiserListNode*>(value.get()))
     {
          if (list->Initialisers().empty()) return true;
          if (list->Initialisers().size() != 1)
          {
               Report("A scalar initialiser list must contain exactly one initialiser", value->Source());
               return false;
          }
          scalarValue = &list->Initialisers().front();
     }

     auto expression = _ir.ParseExpression(*scalarValue);
     if (expression == nullptr) return false;
     if (aggregateElement && _ir.IsNarrowingConversion(expression, type))
     {
          Report(std::format("Narrowing conversion from '{}' to '{}' is not allowed in aggregate "
                             "initialisation",
                             expression->Type()->Name(), type->Name()),
                 (*scalarValue)->Source());
          return false;
     }
     return _ir.ConvertTo(std::move(expression), type) != nullptr;
}

void ecpps::ir::IR::StoreDefaultMemberInitialiser(const typeSystem::ClassId id, const std::size_t fieldIndex,
                                                  const ast::MemberDeclarator& declarator,
                                                  const typeSystem::NonowningTypePointer memberType,
                                                  const Location& source)
{
     bool validInitialiser = false;
     const auto* initialiser = &declarator.initialiser;
     if (initialiser != nullptr && *initialiser != nullptr && memberType != nullptr)
     {
          TypeRequest classRequest;
          classRequest.kind = TypeKind::Compound;
          ClassRequest classRequestData;
          classRequestData.id = id;
          classRequest.data = classRequestData;
          const auto* ownerType = GetTypeContext().Get(classRequest);
          ActiveObjectInitialisation activeObject;
          activeObject.type = ownerType;
          this->_objectInitialisationStack.push_back(activeObject);
          const auto builtSize = this->_built.size();
          const auto diagnosticCount = DiagnosticSink::Count(this->GetContext());

          struct RestoreValidationState
          {
               std::vector<ActiveObjectInitialisation>* objects{};
               std::vector<NodePointer>* built{};
               std::size_t builtSize{};
               ~RestoreValidationState(void)
               {
                    objects->pop_back();
                    built->resize(builtSize);
               }
          };
          RestoreValidationState restore;
          restore.objects = &this->_objectInitialisationStack;
          restore.built = &this->_built;
          restore.builtSize = builtSize;

          DefaultMemberInitialiserValidator validator(*this, id, source, diagnosticCount);
          validInitialiser = validator.Validate(memberType, *initialiser, declarator.initialisationType, false);
     }

     DefaultMemberInitialiser record;
     record.fieldIndex = fieldIndex;
     record.initialiser = &declarator.initialiser;
     record.initialisationType = declarator.initialisationType;
     record.invalid = !validInitialiser;
     record.source = source;
     this->GetContext().classTable->classes[id].memberInitialisers.push_back(record);
}
