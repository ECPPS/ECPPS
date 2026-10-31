#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "../Parsing/ASTs/Type.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "Constexpr.h"
#include "Context.h"
#include "Execution/Entities.h"
#include "Expressions.h"
#include "IR.Internal.h"
#include "Machine/Mangling.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
#include "Parsing/Tokeniser.h"
#include "Procedural.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
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

     LowerExpression(std::move(expression), this->_built); // TODO: warn on nodiscard
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
          const std::string& languageLinkage =
               node.Signature().externOptional.value(); // NOLINT(bugprone-unchecked-optional-access)
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
                         dllImportName = std::get<ecpps::StringLiteral>(firstArg.value).value;
                    }
               }
          }
     }
     const auto namespacePath = NamespacePathFromContext();

     const std::string name = node.Signature().name == nullptr ? "__unknown_func" : node.Signature().name->ToString(0);
     auto functionScope =
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
               .Build();
     functionScope->parameters = parameters;
     this->GetContext().contextSequence.back()->GetScope().functions.push_back(std::move(functionScope));
}

void ecpps::ir::IR::ParseFunctionDefinition(const ast::FunctionDefinitionNode& node)
{
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
          if (RejectClassByValue(param.type, node.Source(), "a by-value parameter")) return;
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
          if (RejectClassByValue(returnType, node.Source(), "a return type")) return;
     }

     if (node.Signature().externOptional.has_value())
     {
          const std::string& languageLinkage =
               node.Signature().externOptional.value(); // NOLINT(bugprone-unchecked-optional-access)
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
                         dllImportName = std::get<ecpps::StringLiteral>(firstArg.value).value;
                    }
               }
          }
     }

     const auto namespacePath = NamespacePathFromContext();

     const std::string name = node.Signature().name == nullptr ? "__unknown_func" : node.Signature().name->ToString(0);
     auto functionScope =
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
               .Build();
     functionScope->parameters = parameters;
     functionScope->linkage = linkage;
     auto functionContext =
          std::make_shared<FunctionContext>(functionScope.get(), node.Signature().callingConvention, returnType, name,
                                            parameters |
                                                 std::views::transform(
                                                      [](const FunctionScope::Parameter& parameter) -> decltype(auto)
                                                      {
                                                           return parameter.type;
                                                      }) |
                                                 std::ranges::to<std::vector>());

     ir.GetContext() = this->GetContext();
     auto* vFunctionScope = functionScope.get();
     this->GetContext().contextSequence.back()->GetScope().functions.push_back(std::move(functionScope));

     ir.GetContext().contextSequence.push_back(std::move(functionContext));
     std::uint64_t paramIndex{};
     std::shared_ptr<std::vector<FunctionScope::LocalEntity>> locals =
          std::make_shared<std::vector<FunctionScope::LocalEntity>>();
     locals->reserve(parameters.size());
     vFunctionScope->SetLocals(locals);

     auto& allocator = *this->GetContext().nodeAllocator;

     for (const auto& param : parameters)
     {
          Variable paramVariable{param.name, param.type, StorageDuration::Automatic};
          FunctionScope::LocalEntity localEntity{std::move(paramVariable)};
          locals->push_back(std::move(localEntity));

          RegisterPriorityInfo paramInfo{.regClass = RegisterClass::LocalVariable};
          auto* funcCtx = dynamic_cast<FunctionContext*>(ir.GetContext().contextSequence.back().get());
          runtime_assert(funcCtx != nullptr, "Expected function context during parameter emission");

          const auto width = param.type->Width();
          auto allocReg = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
               funcCtx->GetNextRegisterIndex(), paramInfo, width, param.type->Alignment(), node.Source())};
          auto* allocRegPtr = allocReg.get();

          funcCtx->RegisterParamAllocReg(param.name, allocRegPtr);

          ir._built.push_back(std::unique_ptr<AllocationNode, IRDeleter>{
               new (allocator) AllocationNode(std::move(allocReg), node.Source())});

          ir._built.push_back(std::unique_ptr<ParameterNode, IRDeleter>{
               new (allocator) ParameterNode(vFunctionScope, paramIndex++, allocRegPtr, node.Source())});
     }

     for (const auto& line : node.Body()) ir.ParseNode(line);

     if (returnType != nullptr && typeSystem::g_void->CommonWith(returnType))
          ir._built.push_back(
               std::unique_ptr<SSAReturnNode, IRDeleter>{new (allocator) SSAReturnNode(nullptr, node.Source())});

     if (name == "main" && (ir._built.empty() || ir._built.back()->Kind() != NodeKind::Return))
     {
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

     this->_built.push_back(std::unique_ptr<ecpps::ir::ProcedureNode, IRDeleter>{
          new (*this->GetContext().nodeAllocator)
               ecpps::ir::ProcedureNode(vFunctionScope, std::move(locals), std::move(ir._built), node.Source())});

     this->GetContext().contextSequence.pop_back();
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot convert from void to type " + function->returnType->Name() + " (aka " +
                              function->returnType->RawName() + ")",
                         node.Source()));
          }
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

     auto converted = ConvertTo(std::move(returnExpression), function->returnType);
     if (converted == nullptr) return;

     const auto* resultReg = LowerExpressionLoaded(std::move(converted), this->_built);
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
