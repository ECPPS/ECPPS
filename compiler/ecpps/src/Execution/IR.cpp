#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/ClassType.h>
#include <TypeSystem/TypeBase.h>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include "../Parsing/ASTs/Type.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "Constexpr.h"
#include "Context.h"
#include "Execution/Diagnostics.h"
#include "Execution/Entities.h"
#include "Expressions.h"
#include "HighLevel.h"
#include "IR.Internal.h"
#include "Machine/Mangling.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
#include "Parsing/Tokeniser.h"
#include "Procedural.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Error.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

[[nodiscard]] static bool CanReachFunctionEnd(const std::vector<IRNodePointer>& body)
{
     std::unordered_map<std::string, std::size_t> labels{};
     for (std::size_t index = 0; index < body.size(); index++)
          if (const auto* label = dynamic_cast<const ecpps::ir::SSALabelNode*>(body[index].get()))
               labels[label->Name()] = index;

     std::vector<bool> visited(body.size() + 1, false);
     std::vector<std::size_t> pending{0};
     while (!pending.empty())
     {
          const auto index = pending.back();
          pending.pop_back();
          if (index == body.size()) return true;
          if (visited[index]) continue;
          visited[index] = true;

          const auto& node = body[index];
          if (node->Kind() == ecpps::ir::NodeKind::Return || node->Kind() == ecpps::ir::NodeKind::Unreachable) continue;

          const auto addLabelTarget = [&pending, &labels](const std::string& name)
          {
               const auto label = labels.find(name);
               if (label != labels.end()) pending.push_back(label->second);
          };
          if (const auto* jump = dynamic_cast<const ecpps::ir::SSAGotoNode*>(node.get()))
          {
               addLabelTarget(jump->Name());
               continue;
          }
          if (const auto* branch = dynamic_cast<const ecpps::ir::SSABranchNode*>(node.get()))
          {
               addLabelTarget(branch->TrueLabel());
               addLabelTarget(branch->FalseLabel());
               continue;
          }
          pending.push_back(index + 1);
     }
     return false;
}

namespace
{
     bool FunctionParameterTypesMatch(ecpps::typeSystem::NonowningTypePointer left,
                                      ecpps::typeSystem::NonowningTypePointer right)
     {
          if (left == right) return true;
          const auto* leftClass = left != nullptr ? left->CastTo<ecpps::typeSystem::ClassType>() : nullptr;
          const auto* rightClass = right != nullptr ? right->CastTo<ecpps::typeSystem::ClassType>() : nullptr;
          return leftClass != nullptr && rightClass != nullptr && leftClass->Info().id == rightClass->Info().id;
     }

     struct FunctionSignatureMatcher
     {
          std::string_view name;
          std::span<const ecpps::ir::FunctionScope::Parameter> parameters;

          [[nodiscard]] bool operator()(const std::shared_ptr<ecpps::ir::FunctionScope>& candidate) const
          {
               if (candidate->memberClassInfo != nullptr || candidate->Name() != this->name ||
                   candidate->parameters.size() != this->parameters.size())
                    return false;
               return std::ranges::equal(candidate->parameters, this->parameters, FunctionParameterTypesMatch,
                                         &ecpps::ir::FunctionScope::Parameter::type,
                                         &ecpps::ir::FunctionScope::Parameter::type);
          }
     };
} // namespace

std::vector<IRNodePointer> ecpps::ir::IR::Parse(Context& context, const std::vector<ASTNodePointer>& ast)
{
     IR ir{&context};
     ecpps::ir::detail::InitialiseBuiltinTypes(context);
     ir.GetContext().contextSequence.push_back(std::make_unique<NamespaceContext>(ir.GetContext().globalScope.get()));
     for (const auto& node : ast) ir.ParseNode(node);
     auto built = std::move(ir._built);
     ir.GetContext().contextSequence.pop_back();
     return built;
}

void ecpps::ir::IR::ParseNode(const ast::NodePointer& node)
{
     if (auto* const functionDefinitionNode = dynamic_cast<ast::FunctionDefinitionNode*>(node.get());
         functionDefinitionNode != nullptr)
     {
          ParseFunctionDefinition(*functionDefinitionNode);
          return;
     }
     if (auto* const functionDeclarationNode = dynamic_cast<ast::FunctionDeclarationNode*>(node.get());
         functionDeclarationNode != nullptr)
     {
          ParseFunctionDeclaration(*functionDeclarationNode);
          return;
     }

     if (auto* const returnNode = dynamic_cast<ast::ReturnNode*>(node.get()); returnNode != nullptr)
     {
          ParseReturn(*returnNode);
          return;
     }
     if (auto* const variableDeclarationNode = dynamic_cast<ast::VariableDeclarationNode*>(node.get());
         variableDeclarationNode != nullptr)
     {
          ParseVariableDeclaration(*variableDeclarationNode);
          return;
     }

     if (auto* const namespaceNode = dynamic_cast<ast::NamespaceNode*>(node.get()); namespaceNode != nullptr)
     {
          ParseNamespace(*namespaceNode);
          return;
     }

     if (auto* const ifNode = dynamic_cast<ast::IfStatementNode*>(node.get()); ifNode != nullptr)
     {
          ParseIfStatement(*ifNode);
          return;
     }

     if (auto* const whileNode = dynamic_cast<ast::WhileStatementNode*>(node.get()); whileNode != nullptr)
     {
          ParseWhileStatement(*whileNode);
          return;
     }
     if (auto* const forNode = dynamic_cast<ast::ForStatementNode*>(node.get()); forNode != nullptr)
     {
          ParseForStatement(*forNode);
          return;
     }
     if (auto* const doWhileNode = dynamic_cast<ast::DoWhileStatementNode*>(node.get()); doWhileNode != nullptr)
     {
          ParseDoWhileStatement(*doWhileNode);
          return;
     }
     if (auto* const breakNode = dynamic_cast<ast::BreakNode*>(node.get()); breakNode != nullptr)
     {
          ParseBreakNode(*breakNode);
          return;
     }
     if (auto* const continueNode = dynamic_cast<ast::ContinueNode*>(node.get()); continueNode != nullptr)
     {
          ParseContinueNode(*continueNode);
          return;
     }

     if (auto* const aliasNode = dynamic_cast<ast::TypeAliasNode*>(node.get()); aliasNode != nullptr)
     {
          const auto* targetType = ParseType(aliasNode->TargetType());
          if (targetType == nullptr)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Invalid target type in using declaration", aliasNode->Source()));
               return;
          }

          const std::string aliasName = aliasNode->AliasName()->ToString(0);

          auto& currentScope = this->GetContext().contextSequence.back()->GetScope();
          currentScope.typeAliases[aliasName] = targetType;

          return;
     }
     if (auto* const labelNode = dynamic_cast<ast::LabelNode*>(node.get()); labelNode != nullptr)
     {
          ParseLabelNode(*labelNode);
          return;
     }
     if (auto* const gotoNode = dynamic_cast<ast::GotoNode*>(node.get()); gotoNode != nullptr)
     {
          ParseGotoNode(*gotoNode);
          return;
     }
     if (auto* const classNode = dynamic_cast<ast::ClassType*>(node.get()); classNode != nullptr)
     {
          ParseClassNode(*classNode);
          return;
     }

     auto expression = ParseExpression(node);
     if (expression == nullptr) return;

     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     const auto temporaryDepth = function == nullptr ? 0 : function->temporaryObjects.size();
     LowerExpression(std::move(expression), this->_built); // TODO: warn on nodiscard
     if (function != nullptr)
     {
          EmitTemporaryDestructorsFrom(temporaryDepth);
          function->temporaryObjects.resize(temporaryDepth);
     }
}

void ecpps::ir::IR::ParseFunctionDeclaration(const ast::FunctionDeclarationNode& node)
{
     std::vector<FunctionScope::Parameter> parameters{};
     const auto& astParams = node.Signature().parameters.parameters;
     parameters.reserve(astParams.size());
     for (const auto& param : astParams)
     {
          parameters.emplace_back(param.name ? param.name->ToString(0) : "", ParseType(param.type), false);
     }

     const auto* returnType = this->ParseType(node.Signature().type);
     if (returnType == nullptr || std::ranges::any_of(parameters,
                                                      [](const FunctionScope::Parameter& parameter)
                                                      {
                                                           return parameter.type == nullptr;
                                                      }))
          return; // TODO: Fallback to int, and maybe eventually guess the type?

     abi::Linkage linkage = abi::Linkage::External;
     if (node.Signature().externOptional.has_value())
     {
          const std::string_view languageLinkage =
               node.Signature().externOptional->View(); // NOLINT(bugprone-unchecked-optional-access)
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                                           "Invalid language linkage specification", node.Source()));
          }
     }
     else if (node.Signature().isInline ||
              node.Signature().constexprSpecifier != ast::ConstantExpressionSpecifier::None)
          linkage = ecpps::abi::Linkage::Internal;
     // TODO: Error on conflicting linkage specification

     bool isDllImportExport{};
     std::string dllImportName{};

     for (const auto& attribute : node.Signature().attributes)
     {
          if (attribute->Name() == "dllexport" || attribute->Name() == "dllimport")
          {
               isDllImportExport = true;
               if (attribute->Name() == "dllimport" && attribute->Arguments().Size() > 0)
               {
                    const auto& firstArg = *attribute->Arguments().begin();
                    if (std::holds_alternative<ecpps::StringLiteral>(firstArg.value))
                    {
                         dllImportName = std::get<ecpps::StringLiteral>(firstArg.value).value.View();
                    }
               }
          }
     }
     const auto namespacePath = NamespacePathFromContext();

     const std::string name = node.Signature().name == nullptr ? "__unknown_func" : node.Signature().name->ToString(0);
     std::shared_ptr<FunctionScope> functionScope{
          MakeFunctionScope()
               .Name(name)
               .ReturnType(returnType)
               .CallingConvention(node.Signature().callingConvention)
               .Linkage(linkage)
               .IsDllImportExport(isDllImportExport)
               .DllImportName(dllImportName)
               .IsExtern(node.Signature().isExtern)
               .IsFriend(node.Signature().isFriend)
               .IsInline(node.Signature().isInline)
               .IsStatic(false) // TODO: node.Signature().isStatic
               .ConstexprSpecifier(node.Signature().constexprSpecifier == ast::ConstantExpressionSpecifier::Constexpr
                                        ? ecpps::ir::ConstexprType::Constexpr
                                        : ecpps::ir::ConstexprType::None)
               .Parameters(parameters)
               .Source(node.Source())
               .NamespacePath(namespacePath)
               .Build()};
     functionScope->parameters = parameters;
     functionScope->returnsByPointer = typeSystem::IsClass(returnType);
     auto& functions = this->GetContext().contextSequence.back()->GetScope().functions;
     const auto existing =
          std::ranges::find_if(functions, FunctionSignatureMatcher{.name = name, .parameters = parameters});
     if (existing == functions.end()) functions.push_back(std::move(functionScope));
     else if ((*existing)->returnType != returnType)
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Function declaration has a different return type from its previous declaration", node.Source()));
}

bool ecpps::ir::IR::ParseConstructorDefinition(const ast::FunctionDefinitionNode& node)
{
     const auto* qualifiedName = dynamic_cast<const ast::QualifiedIdNode*>(node.Signature().name.get());
     if (qualifiedName == nullptr || qualifiedName->Path().size() < 2) return false;

     const auto& path = qualifiedName->Path();
     const auto* constructorName = dynamic_cast<const ast::IdentifierNode*>(path.back().get());
     const auto* className = dynamic_cast<const ast::IdentifierNode*>(path[path.size() - 2].get());
     if (constructorName == nullptr || className == nullptr || constructorName->Value() != className->Value())
          return false;

     std::vector<std::string> qualifiers{};
     const bool isGlobalQualified = dynamic_cast<const ast::GlobalScopeNode*>(path.front().get()) != nullptr;
     for (std::size_t index = static_cast<std::size_t>(isGlobalQualified); index + 1 < path.size(); index++)
     {
          const auto* part = dynamic_cast<const ast::IdentifierNode*>(path[index].get());
          if (part == nullptr) return false;
          qualifiers.emplace_back(part->Value().View());
     }

     struct ResolvedClass
     {
          typeSystem::ClassId id;
          ClassScope* scope;
     };
     const auto resolveFrom = [&](Scope* initialScope) -> std::optional<ResolvedClass>
     {
          Scope* currentScope = initialScope;
          for (std::size_t index = 0; index < qualifiers.size(); index++)
          {
               const auto& qualifier = qualifiers[index];
               if (const auto classIt = currentScope->classes.find(qualifier); classIt != currentScope->classes.end())
               {
                    const auto classRecord = this->GetContext().classTable->classes.find(classIt->second);
                    if (classRecord == this->GetContext().classTable->classes.end() ||
                        classRecord->second.scope == nullptr)
                         return std::nullopt;
                    if (index + 1 == qualifiers.size())
                         return ResolvedClass{.id = classIt->second, .scope = classRecord->second.scope};
                    currentScope = classRecord->second.scope;
                    continue;
               }

               const auto* namespaceScope = dynamic_cast<const NamespaceScope*>(currentScope);
               if (namespaceScope == nullptr) return std::nullopt;
               const auto namespaceIt =
                    std::ranges::find_if(namespaceScope->subNamespaces,
                                         [&qualifier](const std::unique_ptr<NamespaceScope>& candidate)
                                         {
                                              return candidate->Name() == qualifier;
                                         });
               if (namespaceIt == namespaceScope->subNamespaces.end()) return std::nullopt;
               currentScope = namespaceIt->get();
          }
          return std::nullopt;
     };

     std::optional<ResolvedClass> resolvedClass{};
     if (isGlobalQualified) resolvedClass = resolveFrom(this->GetContext().globalScope.get());
     else
          for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
          {
               resolvedClass = resolveFrom(&context->GetScope());
               if (resolvedClass.has_value()) break;
          }
     if (!resolvedClass.has_value()) return false;

     Context definitionContext = this->GetContext();
     definitionContext.contextSequence.push_back(
          std::make_shared<ClassContext>(resolvedClass->scope, resolvedClass->id));
     IR definitionIr{this->_context};
     definitionIr.GetContext() = std::move(definitionContext);

     std::vector<FunctionScope::Parameter> parameters{};
     parameters.reserve(node.Signature().parameters.parameters.size());
     for (const auto& parameter : node.Signature().parameters.parameters)
     {
          if (parameter.defaultInitialiser != nullptr)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Default constructor arguments are not supported yet",
                                           parameter.defaultInitialiser->Source()));
               return true;
          }
          const auto* parameterType = definitionIr.ParseType(parameter.type);
          if (parameterType == nullptr) return true;
          parameters.push_back({.name = parameter.name == nullptr ? "" : parameter.name->ToString(0),
                                .type = parameterType,
                                .isVariadic = false});
     }
     if (parameters.size() == 1 && *parameters.front().type == typeSystem::g_void.get()) parameters.clear();

     auto classRecord = this->GetContext().classTable->classes.find(resolvedClass->id);
     if (classRecord == this->GetContext().classTable->classes.end()) return true;
     auto constructor = std::ranges::find_if(classRecord->second.constructors,
                                             [&parameters](const ClassConstructor& candidate)
                                             {
                                                  if (candidate.parameters.size() != parameters.size()) return false;
                                                  for (std::size_t index = 0; index < parameters.size(); index++)
                                                       if (candidate.parameters[index].type != parameters[index].type)
                                                            return false;
                                                  return true;
                                             });
     if (constructor == classRecord->second.constructors.end())
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      std::format("No matching constructor declaration for '{}'",
                                                  GetTypeContext().Class(resolvedClass->id).DisplayName()),
                                      node.Source()));
          return true;
     }
     if (node.BodyKind() == ast::FunctionBodyKind::Defaulted && !constructor->parameters.empty())
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Only default constructors can be defaulted", node.Source()));
          return true;
     }
     if (constructor->bodyKind != ast::FunctionBodyKind::None || constructor->function->isMemberFunctionDefined)
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Constructor is defined more than once", node.Source()));
          return true;
     }

     constructor->bodyKind = node.BodyKind();
     constructor->function->isMemberFunctionDefined = true;
     constructor->function->isInline = constructor->function->isInline || node.Signature().isInline;
     constructor->function->source = constructorName->Source();
     for (std::size_t index = 0; index < parameters.size(); index++)
          constructor->function->parameters[index].name = std::move(parameters[index].name);

     definitionIr.EmitConstructorBody(constructor->function, resolvedClass->id, node.Source(),
                                      std::span<const ast::NodePointer>{node.Body().begin(), node.Body().end()});
     this->_built.insert(this->_built.end(), std::make_move_iterator(definitionIr._built.begin()),
                         std::make_move_iterator(definitionIr._built.end()));
     return true;
}

void ecpps::ir::IR::ParseFunctionDefinition(const ast::FunctionDefinitionNode& node)
{
     if (ParseConstructorDefinition(node)) return;

     std::vector<FunctionScope::Parameter> parameters{};
     parameters.reserve(node.Signature().parameters.parameters.size());
     for (const auto& param : node.Signature().parameters.parameters)
     {
          parameters.emplace_back(param.name ? param.name->ToString(0) : "", ParseType(param.type), false);
     }
     if (parameters.size() == 1 && *parameters.at(0).type == typeSystem::g_void.get()) parameters.clear();

     for (const auto& param : parameters)
     {
          if (!RequireComplete(param.type, node.Source(), "a function parameter")) return;
     }

     IR ir{this->_context};
     const auto* returnType = this->ParseType(node.Signature().type);
     if (returnType == nullptr || std::ranges::any_of(parameters,
                                                      [](const FunctionScope::Parameter& parameter)
                                                      {
                                                           return parameter.type == nullptr;
                                                      }))
          return; // TODO: Fallback to int, and maybe eventually guess the type?
     abi::Linkage linkage = abi::Linkage::External;

     if (returnType != nullptr && !typeSystem::g_void->CommonWith(returnType))
     {
          if (!RequireComplete(returnType, node.Source(), "a function return type")) return;
     }

     if (node.Signature().externOptional.has_value())
     {
          const std::string_view languageLinkage =
               node.Signature().externOptional->View(); // NOLINT(bugprone-unchecked-optional-access)
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                                           "Invalid language linkage specification", node.Source()));
          }
     }
     else if (node.Signature().isInline ||
              node.Signature().constexprSpecifier != ast::ConstantExpressionSpecifier::None)
          linkage = abi::Linkage::Internal;
     // TODO: Error on conflicting linkage specification

     bool isDllImportExport{};
     std::string dllImportName{};

     for (const auto& attribute : node.Signature().attributes)
     {
          if (attribute->Name() == "dllexport" || attribute->Name() == "dllimport")
          {
               isDllImportExport = true;
               if (attribute->Name() == "dllimport" && attribute->Arguments().Size() > 0)
               {
                    const auto& firstArg = *attribute->Arguments().begin();
                    if (std::holds_alternative<ecpps::StringLiteral>(firstArg.value))
                    {
                         dllImportName = std::get<ecpps::StringLiteral>(firstArg.value).value.View();
                    }
               }
          }
     }

     const auto namespacePath = NamespacePathFromContext();

     const std::string name = node.Signature().name == nullptr ? "__unknown_func" : node.Signature().name->ToString(0);
     std::shared_ptr<FunctionScope> functionScope{
          MakeFunctionScope()
               .Name(name)
               .ReturnType(returnType)
               .CallingConvention(node.Signature().callingConvention)
               .Linkage(linkage)
               .IsDllImportExport(isDllImportExport)
               .DllImportName(dllImportName)
               .IsExtern(node.Signature().isExtern)
               .IsFriend(node.Signature().isFriend)
               .IsInline(node.Signature().isInline)
               .IsStatic(false) // TODO: node.Signature().isStatic
               .ConstexprSpecifier(node.Signature().constexprSpecifier == ast::ConstantExpressionSpecifier::Constexpr
                                        ? ecpps::ir::ConstexprType::Constexpr
                                        : ecpps::ir::ConstexprType::None)
               .Parameters(parameters)
               .Source(node.Source())
               .NamespacePath(namespacePath)
               .Build()};
     functionScope->parameters = parameters;
     functionScope->returnsByPointer = typeSystem::IsClass(returnType);
     functionScope->linkage = linkage;
     functionScope->isMemberFunctionDefined = true;
     auto& functions = this->GetContext().contextSequence.back()->GetScope().functions;
     const auto matchingDeclaration =
          std::ranges::find_if(functions, FunctionSignatureMatcher{.name = name, .parameters = parameters});
     if (matchingDeclaration != functions.end())
     {
          if ((*matchingDeclaration)->returnType != returnType)
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Function definition has a different return type from its declaration", node.Source()));
               return;
          }
          if ((*matchingDeclaration)->isMemberFunctionDefined)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Function is defined more than once", node.Source()));
               return;
          }
          functionScope = *matchingDeclaration;
          functionScope->parameters = parameters;
          functionScope->source = node.Source();
          functionScope->returnsByPointer = typeSystem::IsClass(returnType);
          functionScope->callingConvention = node.Signature().callingConvention;
          functionScope->linkage = linkage;
          functionScope->isMemberFunctionDefined = true;
          functionScope->isInline = node.Signature().isInline;
          functionScope->isExtern = node.Signature().isExtern;
          functionScope->isFriend = node.Signature().isFriend;
          functionScope->isDllImportExport = isDllImportExport;
          functionScope->dllImportName = dllImportName;
     }
     else
          functions.push_back(functionScope);
     auto functionContext =
          std::make_shared<FunctionContext>(functionScope.get(), node.Signature().callingConvention, returnType, name,
                                            parameters |
                                                 std::views::transform(
                                                      [](const FunctionScope::Parameter& parameter) -> decltype(auto)
                                                      {
                                                           return parameter.type;
                                                      }) |
                                                 ecpps::detail::ToVector);

     ir.GetContext() = this->GetContext();
     auto* vFunctionScope = functionScope.get();

     ir.GetContext().contextSequence.push_back(std::move(functionContext));
     std::uint64_t paramIndex{};
     std::shared_ptr<std::vector<FunctionScope::LocalEntity>> locals =
          std::make_shared<std::vector<FunctionScope::LocalEntity>>();
     locals->reserve(parameters.size());
     vFunctionScope->SetLocals(locals);

     auto& allocator = *this->GetContext().nodeAllocator;

     if (vFunctionScope->returnsByPointer)
     {
          auto* funcCtx = dynamic_cast<FunctionContext*>(ir.GetContext().contextSequence.back().get());
          runtime_assert(funcCtx != nullptr, "Expected function context during return buffer emission");
          RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
          auto buffer = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
               funcCtx->GetNextRegisterIndex(), info, abi::ABI::Current().PointerSize() * typeSystem::CharWidth,
               abi::ABI::Current().PointerSize(), node.Source())};
          auto* bufferPointer = buffer.get();
          funcCtx->returnBuffer = bufferPointer;
          ir._built.push_back(std::unique_ptr<AllocationNode, IRDeleter>{
               new (allocator) AllocationNode(std::move(buffer), node.Source())});
          ir._built.push_back(std::unique_ptr<ParameterNode, IRDeleter>{
               new (allocator) ParameterNode(vFunctionScope, paramIndex++, bufferPointer, node.Source())});
     }

     for (const auto& param : parameters)
     {
          Variable paramVariable{param.name, param.type, StorageDuration::Automatic};
          FunctionScope::LocalEntity localEntity{std::move(paramVariable)};
          locals->push_back(std::move(localEntity));

          RegisterPriorityInfo paramInfo{.regClass = RegisterClass::LocalVariable};
          auto* funcCtx = dynamic_cast<FunctionContext*>(ir.GetContext().contextSequence.back().get());
          runtime_assert(funcCtx != nullptr, "Expected function context during parameter emission");

          const auto* classType = param.type->CastTo<typeSystem::ClassType>();
          const auto width =
               classType != nullptr ? abi::ABI::Current().PointerSize() * typeSystem::CharWidth : param.type->Width();
          const auto alignment = classType != nullptr ? abi::ABI::Current().PointerSize() : param.type->Alignment();
          auto allocReg = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
               funcCtx->GetNextRegisterIndex(), paramInfo, width, alignment, node.Source())};
          auto* allocRegPtr = allocReg.get();

          funcCtx->RegisterParamAllocReg(param.name, allocRegPtr);

          ir._built.push_back(std::unique_ptr<AllocationNode, IRDeleter>{
               new (allocator) AllocationNode(std::move(allocReg), node.Source())});

          ir._built.push_back(std::unique_ptr<ParameterNode, IRDeleter>{
               new (allocator) ParameterNode(vFunctionScope, paramIndex++, allocRegPtr, node.Source())});
          ir.RegisterByValueParameterForDestruction(param.type, allocRegPtr, node.Source());
     }

     for (const auto& line : node.Body()) ir.ParseNode(line);

     const bool canReachEnd = CanReachFunctionEnd(ir._built);
     const bool endsWithReturn = !ir._built.empty() && ir._built.back()->Kind() == NodeKind::Return;
     const bool isVoidReturnType = returnType != nullptr && typeSystem::g_void->CommonWith(returnType);
     if (canReachEnd && isVoidReturnType && !endsWithReturn)
     {
          ir.EmitTemporaryDestructorsFrom(0);
          ir.EmitDestructorsFrom(0);
          ir._built.push_back(
               std::unique_ptr<SSAReturnNode, IRDeleter>{new (allocator) SSAReturnNode(nullptr, node.Source())});
     }

     if (canReachEnd && name == "main" && !isVoidReturnType && !endsWithReturn)
     {
          ir.EmitTemporaryDestructorsFrom(0);
          ir.EmitDestructorsFrom(0);
          auto zeroReg = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
               dynamic_cast<FunctionContext*>(ir.GetContext().contextSequence.back().get())->GetNextRegisterIndex(),
               RegisterPriorityInfo{.regClass = RegisterClass::Temporary}, returnType->Width(), returnType->Alignment(),
               node.Source())};
          auto* zeroPtr = zeroReg.get();
          ir._built.push_back(std::unique_ptr<SSAImmNode, IRDeleter>{
               new (allocator) SSAImmNode(std::move(zeroReg), 0, node.Source())});
          ir._built.push_back(
               std::unique_ptr<SSAReturnNode, IRDeleter>{new (allocator) SSAReturnNode(zeroPtr, node.Source())});
     }
     else if (canReachEnd && !isVoidReturnType)
     {
          DiagnosticSink::Report(
               this->GetContext(),
               std::make_unique<diagnostics::Warning>("warning",
                                                      std::format("control may reach the end of non-void function '{}' "
                                                                  "without returning a value",
                                                                  name),
                                                      node.Source()));
          ir._built.push_back(
               std::unique_ptr<SSAUnreachableNode, IRDeleter>{new (allocator) SSAUnreachableNode(node.Source())});
     }

     this->_built.push_back(std::unique_ptr<ecpps::ir::ProcedureNode, IRDeleter>{
          new (*this->GetContext().nodeAllocator)
               ecpps::ir::ProcedureNode(vFunctionScope, std::move(locals), std::move(ir._built), node.Source())});

     this->GetContext().contextSequence.pop_back();
}

void ecpps::ir::IR::EmitFunctionScopeDestruction(FunctionContext& function, const Location& source)
{
     EmitTemporaryDestructorsFrom(0);
     EmitDestructorsFrom(0);
     const auto& functionScope = function.GetScope<FunctionScope>();
     if (!functionScope.Name().has_value() || !functionScope.Name()->starts_with("~") ||
         functionScope.memberClassInfo == nullptr || function.thisType == nullptr)
          return;

     Expression thisExpression =
          std::make_unique<PRValue>(function.thisType,
                                    std::unique_ptr<high::ThisExpressionNode, IRDeleter>{
                                         new (*this->GetContext().nodeAllocator) high::ThisExpressionNode(source)},
                                    false);
     const auto* objectAddress = LowerExpressionLoaded(std::move(thisExpression), this->_built);
     if (objectAddress != nullptr) DestroyClassMembersAtAddress(*functionScope.memberClassInfo, objectAddress);
}

void ecpps::ir::IR::ParseReturn(const ast::ReturnNode& node)
{
     auto* const function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());

     runtime_assert(function != nullptr, "Function was null when parsing the function");

     auto& allocator = *this->GetContext().nodeAllocator;
     if (node.Value() == nullptr)
     {
          if (!typeSystem::g_void->CommonWith(function->returnType)) // NOLINT(clang-analyzer-core.NullDereference)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Cannot convert from void to type " + function->returnType->Name() +
                                                " (aka " + function->returnType->RawName() + ")",
                                           node.Source()));
          }
          EmitFunctionScopeDestruction(*function, node.Source());
          this->_built.push_back(
               std::unique_ptr<SSAReturnNode, IRDeleter>{new (allocator) SSAReturnNode(nullptr, node.Source())});
          return;
     }

     auto returnExpression = ParseExpression(node.Value());
     if (returnExpression == nullptr) return;

     auto optionalConstexpr = returnExpression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
     if (optionalConstexpr.has_value())
     {
          auto& value = *optionalConstexpr;
          returnExpression = ConstantEvaluationResultToExpression(value, returnExpression->Type(), allocator);
     }
     if (function == nullptr) return;

     if (function->returnType->CastTo<typeSystem::ReferenceType>() != nullptr)
     {
          if (!ConstructorArgumentCost(function->returnType, returnExpression).has_value())
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           std::format("Cannot return '{}' as '{}'", returnExpression->Type()->Name(),
                                                       function->returnType->Name()),
                                           node.Source()));
               return;
          }
          const auto* resultReg =
               BindReferenceExpression(function->returnType, std::move(returnExpression), node.Source(), true);
          EmitFunctionScopeDestruction(*function, node.Source());
          this->_built.push_back(
               std::unique_ptr<SSAReturnNode, IRDeleter>{new (allocator) SSAReturnNode(resultReg, node.Source())});
          return;
     }

     if (typeSystem::IsClass(function->returnType))
     {
          if (function->returnBuffer == nullptr)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Class return requires a caller-provided result buffer", node.Source()));
               return;
          }

          if (const auto* identifier = dynamic_cast<const ast::IdentifierNode*>(node.Value().get());
              identifier != nullptr)
          {
               bool moveEligible = false;
               const auto identifierName = identifier->Value().View();
               for (const auto& local : function->GetScope<FunctionScope>().Locals() | std::views::reverse)
               {
                    const auto* variable = std::get_if<Variable>(&local.local);
                    if (variable == nullptr || variable->Name().value_or("") != identifierName) continue;
                    const auto* reference = variable->type->CastTo<typeSystem::ReferenceType>();
                    const bool isClassObject = reference == nullptr
                                                    ? typeSystem::IsClass(variable->type)
                                                    : reference->GetKind() == typeSystem::ReferenceType::Kind::RValue &&
                                                           typeSystem::IsClass(reference->BaseType());
                    moveEligible = variable->storageDuration == StorageDuration::Automatic && isClassObject;
                    break;
               }

               if (moveEligible)
               {
                    const auto* expressionType = returnExpression->Type();
                    const bool isConstantExpression = returnExpression->IsConstantExpression();
                    auto value = std::move(*returnExpression).Value();
                    returnExpression = std::make_unique<XValue>(expressionType, std::move(value), isConstantExpression);
               }
          }

          RegisterPriorityInfo info{.regClass = RegisterClass::AddressTemporary};
          auto resultAddress =
               std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
                    function->GetNextRegisterIndex(), info, abi::ABI::Current().PointerSize() * typeSystem::CharWidth,
                    abi::ABI::Current().PointerSize(), node.Source())};
          auto* resultAddressPointer = resultAddress.get();
          this->_built.push_back(std::unique_ptr<SSALoadNode, IRDeleter>{
               new (allocator) SSALoadNode(std::move(resultAddress), function->returnBuffer,
                                           detail::AccessFor(PointerTo(function->returnType)), node.Source())});
          InitialiseClassFromExpression(function->returnType, resultAddressPointer, std::move(returnExpression),
                                        node.Source(), false);
          EmitFunctionScopeDestruction(*function, node.Source());
          this->_built.push_back(
               std::unique_ptr<SSAReturnNode, IRDeleter>{new (allocator) SSAReturnNode(nullptr, node.Source())});
          return;
     }

     auto converted = ConvertTo(std::move(returnExpression), function->returnType);
     if (converted == nullptr) return;

     const auto* resultReg = LowerExpressionLoaded(std::move(converted), this->_built);
     EmitFunctionScopeDestruction(*function, node.Source());
     this->_built.push_back(
          std::unique_ptr<SSAReturnNode, IRDeleter>{new (allocator) SSAReturnNode(resultReg, node.Source())});
}

std::vector<std::string> ecpps::ir::IR::NamespacePathFromContext(void) const
{
     std::vector<std::string> path{};
     for (const auto& context : this->GetContext().contextSequence)
     {
          if (auto* const namespaceContext = dynamic_cast<NamespaceContext*>(context.get());
              namespaceContext != nullptr)
          {
               auto* vScope = &namespaceContext->GetScope();

               auto* vNamespaceScope = dynamic_cast<NamespaceScope*>(vScope);
               runtime_assert(vNamespaceScope != nullptr, "Expected a namespace scope in a namespace context");
               if (vNamespaceScope->Name().has_value())
               {
                    const auto& namespaceName = vNamespaceScope->Name().value();
                    path.push_back(namespaceName);
               }
          }
     }
     return path;
}

void ecpps::ir::IR::ParseNamespace(const ast::NamespaceNode& node)
{
     std::string namespaceName;
     if (node.Name() != nullptr)
     {
          namespaceName = node.Name()->ToString(0);
     }

     auto& parentScope = this->GetContext().contextSequence.back()->GetScope();
     auto* parentNamespace = dynamic_cast<NamespaceScope*>(&parentScope);
     runtime_assert(parentNamespace != nullptr, "Parent scope for namespace was not a namespace");

     NamespaceScope* namespacePointer{};
     if (namespaceName.empty())
     {
          auto namespaceUnique = std::make_unique<NamespaceScope>();
          namespacePointer = namespaceUnique.get();
          namespacePointer->parentScope = parentNamespace;
          parentNamespace->subNamespaces.push_back(std::move(namespaceUnique));
     }
     else
     {
          for (const auto& existingNs : parentNamespace->subNamespaces)
          {
               if (existingNs->Name() == namespaceName)
               {
                    namespacePointer = existingNs.get();
                    break;
               }
          }
          if (namespacePointer == nullptr)
          {
               auto namespaceUnique = std::make_unique<NamespaceScope>(namespaceName);
               namespacePointer = namespaceUnique.get();
               namespacePointer->parentScope = parentNamespace;
               parentNamespace->subNamespaces.push_back(std::move(namespaceUnique));
          }
     }

     this->GetContext().contextSequence.push_back(std::make_unique<NamespaceContext>(namespacePointer));

     for (const auto& decl : node.Declarations())
     {
          if (decl != nullptr) ParseNode(decl);
     }
     this->GetContext().contextSequence.pop_back();
}
