#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <algorithm>
#include <format>
#include <memory>
#include <queue>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "../Machine/ABI.h"
#include "../Parsing/ASTs/Type.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "../TypeSystem/CompoundTypes.h"
#include "Constexpr.h"
#include "Context.h"
#include "ControlFlow.h"
#include "Expressions.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
#include "Procedural.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

namespace
{
     std::string FormatFunctionSignature(const std::shared_ptr<ecpps::ir::FunctionScope>& func)
     {
          std::string signature = func->name + "(";
          bool first = true;
          for (const auto& param : func->parameters)
          {
               if (!first) signature += ", ";
               first = false;
               signature += param.type->Name();
               if (!param.name.empty()) signature += " " + param.name;
          }
          signature += ")";
          if (func->returnType) { signature += " -> " + func->returnType->Name(); }
          return signature;
     }

     std::size_t LevenshteinDistance(const std::string& string1, const std::string& string2)
     {
          const std::size_t size1 = string1.size();
          const std::size_t size2 = string2.size();

          std::vector<std::size_t> previous(size2 + 1);
          std::vector<std::size_t> current(size2 + 1);
          std::vector<std::size_t> previousPrevious(size2 + 1);

          for (std::size_t j = 0; j <= size2; j++) previous[j] = j * 2;

          for (std::size_t i = 0; i < size1; i++)
          {
               current[0] = (i + 1) * 2;

               for (std::size_t j = 0; j < size2; j++)
               {
                    std::size_t cost{};

                    if (string1[i] == string2[j]) cost = 0;
                    else if (std::tolower(string1[i]) == std::tolower(string2[j]))
                         cost = 1;
                    else
                         cost = 2;

                    current[j + 1] = std::min({previous[j + 1] + 2, current[j] + 2, previous[j] + cost});

                    if (i > 0 && j > 0 && std::tolower(string1[i]) == std::tolower(string2[j - 1]) &&
                        std::tolower(string1[i - 1]) == std::tolower(string2[j]))
                         current[j + 1] = std::min(current[j + 1], previousPrevious[j - 1] + 2);
               }

               previousPrevious.swap(previous);
               previous.swap(current);
          }

          return previous[size2] / 2;
     }

     std::vector<std::shared_ptr<ecpps::ir::FunctionScope>> CollectExactMatches(
         const std::string& name, ecpps::SBOQueue<ecpps::ir::ContextPointer>& contextSequence)
     {
          std::vector<std::shared_ptr<ecpps::ir::FunctionScope>> exactMatches;
          for (const auto& context : contextSequence)
               for (const auto& func : context->GetScope().functions)
                    if (func->name == name) { exactMatches.push_back(func); }

          return exactMatches;
     }

     std::vector<std::pair<std::string, std::size_t>> CollectSimilarNames(
         const std::string& name, std::size_t argumentCount,
         ecpps::SBOQueue<ecpps::ir::ContextPointer>& contextSequence)
     {
          std::vector<std::pair<std::string, std::size_t>> similar;
          std::set<std::string> seen;

          for (const auto& context : contextSequence)
          {
               for (const auto& func : context->GetScope().functions)
               {
                    if (seen.contains(func->name)) continue;

                    seen.insert(func->name);

                    const auto nameDistance = LevenshteinDistance(name, func->name);
                    if (nameDistance == 0) continue;

                    if (nameDistance > std::max<std::size_t>(3, name.length() / 3)) continue;

                    const std::size_t paramCount = func->parameters.size();
                    const std::size_t argPenalty =
                        (paramCount > argumentCount) ? (paramCount - argumentCount) : (argumentCount - paramCount);

                    const std::size_t score = nameDistance + (argPenalty * 2);

                    if (score > 5) continue;

                    similar.emplace_back(func->name, score);
               }
          }

          std::ranges::sort(similar, [](const auto& a, const auto& b) { return a.second < b.second; });

          return similar;
     }

     std::vector<std::pair<std::string, std::size_t>> CollectSimilarIdentifierNames(
         const std::string& name, ecpps::SBOQueue<ecpps::ir::ContextPointer>& contextSequence)
     {
          std::vector<std::pair<std::string, std::size_t>> similar;
          std::set<std::string> seen;

          for (const auto& context : contextSequence)
          {
               const auto& scope = context->GetScope();
               if (const auto* const functionScope = dynamic_cast<const ecpps::ir::FunctionScope*>(&scope))
               {
                    for (const auto& local : functionScope->locals)
                    {
                         if (seen.contains(local.name)) continue;
                         seen.insert(local.name);

                         const auto distance = LevenshteinDistance(name, local.name);
                         if (distance > 0 && distance <= std::max<std::size_t>(3, name.length() / 3))
                              similar.emplace_back(local.name, distance);
                    }
               }

               for (const auto& func : scope.functions)
               {
                    if (seen.contains(func->name)) continue;
                    seen.insert(func->name);

                    const auto distance = LevenshteinDistance(name, func->name);
                    if (distance > 0 && distance <= std::max<std::size_t>(3, name.length() / 3))
                         similar.emplace_back(func->name, distance);
               }
          }

          std::ranges::sort(similar, [](const auto& a, const auto& b) { return a.second < b.second; });

          return similar;
     }

     struct CandidateFailureInfo
     {
          std::shared_ptr<ecpps::ir::FunctionScope> function;
          std::vector<std::pair<std::size_t, std::string>> parameterFailures; // index <=> name
     };

     CandidateFailureInfo AnalyseCandidateFailure(const std::shared_ptr<ecpps::ir::FunctionScope>& function,
                                                  const std::vector<ecpps::Expression>& arguments)
     {
          CandidateFailureInfo info{.function = function, .parameterFailures = {}};

          if (arguments.size() != function->parameters.size())
          {
               using std::string_view_literals::operator""sv;
               info.parameterFailures.emplace_back(
                   static_cast<std::size_t>(-1),
                   std::format("Expected {} argument{}, got {}", function->parameters.size(),
                               function->parameters.size() == 1 ? ""sv : "s"sv, arguments.size()));
               return info;
          }

          auto parameterIterator = function->parameters.begin();
          std::size_t paramIndex = 1;
          for (const auto& argument : arguments)
          {
               const auto& parameter = *parameterIterator++;
               if (argument == nullptr)
               {
                    info.parameterFailures.emplace_back(
                        paramIndex, std::format("Parameter {}: Invalid argument expression", paramIndex));
                    paramIndex++;
                    continue;
               }

               const auto& fromType = argument->Type();
               const auto& toType = parameter.type;

               ecpps::typeSystem::ConversionSequence seq = toType->CompareTo(fromType);

               if (!seq.IsValid())
               {
                    std::string reason = std::format("Parameter {} ({}): Cannot convert from '{}' to '{}'", paramIndex,
                                                     parameter.name.empty() ? "<unnamed>" : parameter.name,
                                                     fromType->Name(), toType->Name());
                    info.parameterFailures.emplace_back(paramIndex, reason);
               }

               paramIndex++;
          }

          return info;
     }
} // namespace

std::vector<IRNodePointer> ecpps::ir::IR::Parse(Diagnostics& diagnostics, BumpAllocator& allocator,
                                                const std::vector<ASTNodePointer>& ast)
{
     IR ir{diagnostics, allocator};
     ir._context.globalScope->types.insert(typeSystem::g_void.get());
     ir._context.globalScope->types.insert(typeSystem::g_char.get());
     ir._context.globalScope->types.insert(typeSystem::g_signedChar.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedChar.get());
     ir._context.globalScope->types.insert(typeSystem::g_short.get());
     ir._context.globalScope->types.insert(typeSystem::g_int.get());
     ir._context.globalScope->types.insert(typeSystem::g_long.get());
     ir._context.globalScope->types.insert(typeSystem::g_longLong.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedShort.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedInt.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedLong.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedLongLong.get());
     ir._context.contextSequence.Push(std::make_unique<NamespaceContext>(ir._context.globalScope.get()));
     for (const auto& node : ast) ir.ParseNode(node);
     auto built = std::move(ir._built);
     ir._context.contextSequence.Pop();
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

     if (auto* const aliasNode = dynamic_cast<ast::TypeAliasNode*>(node.get()); aliasNode != nullptr)
     {
          const auto* targetType = ParseType(aliasNode->TargetType());
          if (targetType == nullptr)
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "Invalid target type in using declaration", aliasNode->Source()));
               return;
          }

          const std::string aliasName = aliasNode->AliasName()->ToString(0);

          auto& currentScope = this->_context.contextSequence.Back()->GetScope();
          currentScope.typeAliases[aliasName] = targetType;

          return;
     }

     auto expression = ParseExpression(node);
     if (expression == nullptr) return;

     this->_built.push_back(std::move(*expression).Value()); // TODO: warn on nodiscard
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
     abi::Linkage linkage = abi::Linkage::External;
     if (node.Signature().externOptional.has_value())
     {
          const std::string& languageLinkage =
              node.Signature().externOptional.value(); // NOLINT(bugprone-unchecked-optional-access)
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
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
               // Extract optional undecorated import name from dllimport attribute
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

     auto functionScope =
         MakeFunctionScope()
             .Name(node.Signature().name->ToString(0))
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
             .Build();
     functionScope->parameters = parameters;
     this->_context.contextSequence.Back()->GetScope().functions.push_back(std::move(functionScope));
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

     IR ir{this->_context.diagnostics.get(), *this->_context.nodeAllocator};
     const auto* returnType = this->ParseType(node.Signature().type);
     abi::Linkage linkage = abi::Linkage::External;
     if (node.Signature().externOptional.has_value())
     {
          const std::string& languageLinkage =
              node.Signature().externOptional.value(); // NOLINT(bugprone-unchecked-optional-access)
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
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
               // Extract optional undecorated import name from dllimport attribute
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

     auto functionScope =
         MakeFunctionScope()
             .Name(node.Signature().name->ToString(0))
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
             .Build();
     functionScope->parameters = parameters;
     functionScope->linkage = linkage;
     auto functionContext = std::make_shared<FunctionContext>(
         functionScope.get(), node.Signature().callingConvention, returnType, node.Signature().name->ToString(0),
         parameters |
             std::views::transform([](const FunctionScope::Parameter& parameter) -> decltype(auto)
                                   { return parameter.type; }) |
             std::ranges::to<std::vector>());

     ir._context = this->_context;
     auto* vFunctionScope = functionScope.get();
     ir._context.contextSequence.Push(std::move(functionContext));

     this->_context.contextSequence.Back()->GetScope().functions.push_back(std::move(functionScope));
     std::uint64_t paramIndex{};
     for (const auto& param : parameters)
     {
          vFunctionScope->locals.push_back(
              FunctionScope::Variable{.name = param.name, .type = param.type, .isStatic = false, .isExtern = false});

          auto paramNode =
              std::make_unique<PRValue>(param.type,
                                        std::unique_ptr<ParameterNode, IRDeleter>(new (
                                            *this->_context.nodeAllocator) ParameterNode(paramIndex++, node.Source())),
                                        false);

          ir._built.push_back(std::unique_ptr<ir::StoreNode, IRDeleter>{
              new (*this->_context.nodeAllocator) ir::StoreNode(param.name, std::move(paramNode), node.Source())});
     }

     for (const auto& line : node.Body()) ir.ParseNode(line);
     std::vector<FunctionScope::Variable> locals{};
     locals.reserve(vFunctionScope->locals.size());
     for (const auto& toCopy : vFunctionScope->locals) locals.emplace_back(toCopy);

     const auto functionName = node.Signature().name->ToString(0);

     if (returnType != nullptr && typeSystem::g_void->CommonWith(returnType))
          ir._built.push_back(std::unique_ptr<ir::ReturnNode, IRDeleter>{new (*ir._context.nodeAllocator)
                                                                             ir::ReturnNode(nullptr, node.Source())});

     if (functionName == "main" && (ir._built.empty() || ir._built.back()->Kind() != NodeKind::Return))
          ir._built.push_back(
              std::unique_ptr<ir::ReturnNode, IRDeleter>{new (*ir._context.nodeAllocator) ir::ReturnNode(
                  std::make_unique<PRValue>(typeSystem::g_int.get(),
                                            std::unique_ptr<IntegralNode, IRDeleter>{
                                                new (*ir._context.nodeAllocator) ir::IntegralNode(0, node.Source())},
                                            true),
                  node.Source())});

     this->_built.push_back(std::unique_ptr<ecpps::ir::ProcedureNode, IRDeleter>{
         new (*this->_context.nodeAllocator)
             ecpps::ir::ProcedureNode(linkage, node.Signature().callingConvention, returnType, functionName,
                                      std::move(parameters), std::move(locals), std::move(ir._built), node.Source())});
}

void ecpps::ir::IR::ParseReturn(const ast::ReturnNode& node)
{
     auto* const function = dynamic_cast<FunctionContext*>(this->_context.contextSequence.Back().get());

     runtime_assert(function != nullptr, "Function was null when parsing the function");

     if (node.Value() == nullptr)
     {
          if (!typeSystem::g_void->CommonWith(function->returnType)) // NOLINT(clang-analyzer-core.NullDereference)
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "Cannot convert from void to type " + function->returnType->Name() + " (aka " +
                           function->returnType->RawName() + ")",
                       node.Source()));
          }
          this->_built.push_back(std::unique_ptr<ir::ReturnNode, IRDeleter>{
              new (*this->_context.nodeAllocator) ir::ReturnNode(nullptr, node.Source())});
     }

     auto returnExpression = ParseExpression(node.Value());
     if (returnExpression == nullptr) return;
     auto optionalConstexpr = returnExpression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
     if (optionalConstexpr.has_value())
     {
          auto& value = *optionalConstexpr;
          returnExpression =
              ConstantEvaluationResultToExpression(value, returnExpression->Type(), *this->_context.nodeAllocator);
     }

     auto converted = ConvertTo(std::move(returnExpression), function->returnType);
     this->_built.push_back(std::unique_ptr<ir::ReturnNode, IRDeleter>{
         new (*this->_context.nodeAllocator) ir::ReturnNode(std::move(converted), node.Source())});
}

void ecpps::ir::IR::ParseVariableDeclaration(const ast::VariableDeclarationNode& node)
{
     if (node.GetFlags().isTypedef)
     {
          const auto* declaredType = ParseType(node.Type());
          if (declaredType == nullptr)
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       std::format("Unknown type {} in typedef declaration", node.Type()->ToString(0)), node.Source()));
               return;
          }

          auto& currentScope = this->_context.contextSequence.Back()->GetScope();
          for (const auto& decl : node.Declarators())
          {
               const auto* idNodePtr = decl.name.get();
               const auto* idExpr = dynamic_cast<const ast::IdentifierNode*>(idNodePtr);
               if (idExpr == nullptr)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                            "Expected identifier in typedef declarator", decl.name->Source()));
                    continue;
               }

               const std::string typedefName = idExpr->Value();
               currentScope.typeAliases[typedefName] = declaredType;
          }

          return;
     }

     auto* const functionContext = dynamic_cast<FunctionContext*>(this->_context.contextSequence.Back().get());
     runtime_assert(functionContext != nullptr, "Variable declaration outside of a function is not supported");

     auto& fscope = functionContext->GetScope<FunctionScope>(); // NOLINT(clang-analyzer-core.CallAndMessage)

     const auto* declaredType = ParseType(node.Type());
     if (declaredType == nullptr)
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  std::format("Unknown type {} in variable declaration", node.Type()->ToString(0)), node.Source()));
          return;
     }

     for (const auto& decl : node.Declarators())
     {
          auto* const idNodePtr = decl.name.get();
          const auto* const idExpr = dynamic_cast<const ast::IdentifierNode*>(idNodePtr);
          if (idExpr == nullptr)
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                       "Expected identifier in variable declarator", decl.name->Source()));
               continue;
          }

          const std::string varName = idExpr->Value();
          const auto* variableType = declaredType;
          bool inferLastArrayFromInitialiser = false;
          const auto isArray = !decl.arrayLevels.empty();

          for (const auto& arrayLevel : decl.arrayLevels)
          {
               if (inferLastArrayFromInitialiser)
                    this->_context.diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::TypeError>(
                        std::format("Declaration of '{}' introduces an array of unbounded arrays which is not allowed, "
                                    "only the top-level array might be unbounded",
                                    varName),
                        arrayLevel == nullptr ? node.Source() : arrayLevel->Source()));

               if (arrayLevel == nullptr)
               {
                    inferLastArrayFromInitialiser = true;

                    continue;
               }
               const auto arrayLevelExpression = ParseExpression(arrayLevel);
               if (arrayLevelExpression == nullptr) break; // assume the caller issued diagnostics

               auto constexprArraySize =
                   arrayLevelExpression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
               if (!constexprArraySize.has_value())
               {
                    inferLastArrayFromInitialiser = true; // at least try to be useful...
                    this->_context.diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::TypeError>(
                        std::format("Arrays bounds must be defined by a constant expression", varName),
                        arrayLevel == nullptr ? node.Source() : arrayLevel->Source()));

                    auto& nestedDiagnostics = constexprArraySize.error();

                    while (!nestedDiagnostics.empty())
                    {
                         this->_context.diagnostics.get().diagnosticsList.push_back(std::move(nestedDiagnostics.top()));
                         nestedDiagnostics.pop();
                    }
                    break;
               }
               const auto& arraySize = *constexprArraySize;
               if (!std::holds_alternative<std::uint64_t>(arraySize.variant))
               {
                    inferLastArrayFromInitialiser = true; // at least try to be useful...
                    this->_context.diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::TypeError>(
                        std::format("Arrays bounds must be defined by an integer", varName),
                        arrayLevel == nullptr ? node.Source() : arrayLevel->Source()));
                    break;
               }
               const auto length = std::get<std::uint64_t>(arraySize.variant);

               TypeRequest arrayRequest{};
               arrayRequest.kind = TypeKind::Compound;
               arrayRequest.data = BoundedArrayRequest{.elementType = variableType, .size = length};

               variableType = GetContext().Get(arrayRequest);
          }

          bool duplicate = false;
          for (const auto& v : fscope.locals)
          {
               if (v.name == varName)
               {
                    duplicate = true;
                    break;
               }
          }
          if (!duplicate)
          {
               for (const auto& p : fscope.parameters)
               {
                    if (p.name == varName)
                    {
                         duplicate = true;
                         break;
                    }
               }
          }
          if (duplicate)
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "Redefinition of variable '" + varName + "'", decl.name->Source()));
               continue;
          }

          FunctionScope::Variable varEntry;
          varEntry.name = varName;
          varEntry.type = variableType;
          varEntry.isStatic = node.GetFlags().isStatic;
          varEntry.isExtern = node.GetFlags().isExtern;

          auto& registeredVar = fscope.locals.emplace_back(std::move(varEntry));

          if (inferLastArrayFromInitialiser)
          {
               if (decl.initialiser == nullptr)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                            std::format("Unbounded array '{}' must have an initialiser", varName),
                            decl.name->Source()));
               }
               if (IsEligibleForStringLiteralInitialisation(variableType))
               {
                    if (auto* const stringInitialiser =
                            dynamic_cast<ecpps::ast::StringLiteralNode*>(decl.initialiser.get());
                        stringInitialiser != nullptr)
                    {
                         const auto& string = stringInitialiser->Value();
                         const auto arrayLength = string.length() + 1;
                         const auto* elementType = variableType->CastTo<ecpps::typeSystem::CharacterType>();
                         runtime_assert(elementType != nullptr,
                                        "Expected a character type for string-literal initialisation");
                         TypeRequest arrayRequest{};
                         arrayRequest.kind = TypeKind::Compound;
                         arrayRequest.data = BoundedArrayRequest{.elementType = elementType, .size = arrayLength};

                         variableType = GetContext().Get(arrayRequest);
                         inferLastArrayFromInitialiser = false;

                         std::vector<std::uint32_t> arrayValues{};
                         arrayValues.reserve(arrayLength);
                         for (const auto character : string) arrayValues.emplace_back(character);
                         arrayValues.emplace_back(0);
                         std::unique_ptr<ecpps::ir::IntegerArrayNode, IRDeleter> arrayNode{
                             new (*this->_context.nodeAllocator) ecpps::ir::IntegerArrayNode(
                                 std::move(arrayValues), elementType, decl.initialiser->Source())};
                         auto initialiserExpression =
                             std::make_unique<ecpps::PRValue>(variableType, std::move(arrayNode), true);

                         this->_built.push_back(std::unique_ptr<ir::StoreNode, IRDeleter>{
                             new (*this->_context.nodeAllocator) ir::StoreNode(
                                 registeredVar.name, std::move(initialiserExpression), decl.initialiser->Source())});
                    }
               }
               if (inferLastArrayFromInitialiser)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                            std::format("Array '{}' must be initialised with initialiser-lists", varName),
                            decl.name->Source()));
               }

               registeredVar.type = variableType;
          }
          else if (decl.initialiser)
          {
               auto initExpr = ParseExpression(decl.initialiser);
               if (initExpr == nullptr)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                            "Invalid initialiser for variable '" + varName + "'", decl.initialiser->Source()));
                    continue;
               }
               if (isArray)
               {
                    const auto* arrayType = variableType->CastTo<ecpps::typeSystem::ArrayType>();
                    runtime_assert(arrayType != nullptr, "Expected an array type for array initialiers");

                    if (IsEligibleForStringLiteralInitialisation(arrayType->ElementType()))
                    {
                         if (auto* const stringInitialiser =
                                 dynamic_cast<ecpps::ast::StringLiteralNode*>(decl.initialiser.get());
                             stringInitialiser != nullptr)
                         {
                              const auto& string = stringInitialiser->Value();
                              const auto arrayLength = string.length() + 1;
                              const auto* elementType =
                                  arrayType->ElementType()->CastTo<ecpps::typeSystem::CharacterType>();
                              runtime_assert(elementType != nullptr,
                                             "Expected a character type for string-literal initialisation");

                              if (arrayLength > arrayType->ElementCount())
                              {
                                   this->_context.diagnostics.get().diagnosticsList.push_back(
                                       diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           std::format("Cannot initialise an array with more elements than it can "
                                                       "hold. Provided {} elements for an array of `{}` `{}`s",
                                                       arrayLength, arrayType->ElementCount(),
                                                       arrayType->ElementType()->RawName()),
                                           decl.initialiser->Source()));
                              }

                              std::vector<std::uint32_t> arrayValues{};
                              arrayValues.reserve(arrayLength);
                              for (const auto character : string) arrayValues.emplace_back(character);
                              arrayValues.emplace_back(0);

                              if (arrayLength < arrayType->ElementCount())
                                   arrayValues.resize(arrayType->ElementCount());

                              std::unique_ptr<ecpps::ir::IntegerArrayNode, IRDeleter> arrayNode{
                                  new (*this->_context.nodeAllocator) ecpps::ir::IntegerArrayNode(
                                      std::move(arrayValues), elementType, decl.initialiser->Source())};
                              auto initialiserExpression =
                                  std::make_unique<ecpps::PRValue>(variableType, std::move(arrayNode), true);

                              this->_built.push_back(std::unique_ptr<ir::StoreNode, IRDeleter>{
                                  new (*this->_context.nodeAllocator)
                                      ir::StoreNode(registeredVar.name, std::move(initialiserExpression),
                                                    decl.initialiser->Source())});
                         }
                         continue;
                    }
               }

               auto converted = ConvertTo(std::move(initExpr), declaredType);
               if (converted == nullptr)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                            "Cannot convert initialiser to variable type for '" + varName + "'",
                            decl.initialiser->Source()));
                    continue;
               }

               this->_built.push_back(std::unique_ptr<ir::StoreNode, IRDeleter>{
                   new (*this->_context.nodeAllocator)
                       ir::StoreNode(registeredVar.name, std::move(converted), decl.initialiser->Source())});
          }
          else
          {
               // TODO: default-initialiser
               // for scalars it is an indeterminate Value, but TODO: class types
               // TODO: Error for references
          }
     }
}

void ecpps::ir::IR::ParseNamespace(const ast::NamespaceNode& node)
{
     std::string namespaceName;
     if (node.Name() != nullptr) { namespaceName = node.Name()->ToString(0); }

     // TODO: For full namespace support, we should:
     // - Create a new scope for the namespace
     // - Track the namespace in the symbol table
     // - Support qualified name lookups
     // Although this is not within the scope of this PR

     for (const auto& decl : node.Declarations())
     {
          if (decl != nullptr) ParseNode(decl);
     }
}

Expression ecpps::ir::IR::ParseAdditiveExpression(Expression left, ast::Operator operator_, Expression right,
                                                  const Location& source) const
{
     runtime_assert(operator_ == ast::Operator::Plus || operator_ == ast::Operator::Minus, "Invalid additive operator");

     const auto* leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     const auto* rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

     const auto* leftPointer = left->Type()->CastTo<typeSystem::PointerType>();
     const auto* rightPointer = right->Type()->CastTo<typeSystem::PointerType>();

     // E1 = E2 where one is a pointer and one is an integer
     if ((leftPointer != nullptr && rightIntegral != nullptr) || (leftIntegral != nullptr && rightPointer != nullptr))
     {
          const bool isPlus = operator_ == ast::Operator::Plus;
          if (leftIntegral != nullptr)
          {
               if (!isPlus)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                            std::format("Cannot subtract a pointer of type `{}` from an integer of type `{}`",
                                        rightPointer->RawName(), leftIntegral->RawName()),
                            source));
                    return nullptr;
               }

               std::swap(left, right);
               std::swap(leftIntegral, rightIntegral);
               std::swap(leftPointer, rightPointer);
          }

          rightIntegral = typeSystem::PromoteInteger(rightIntegral);

          if (right->Type() != rightIntegral)
          {
               const auto innerSource = right->Value()->Source();
               const auto wasConstexpr = right->IsConstantExpression();

               right = std::make_unique<PRValue>(rightIntegral,
                                                 std::unique_ptr<ConvertNode, IRDeleter>{
                                                     new (*this->_context.nodeAllocator)
                                                         ConvertNode(std::move(right), rightIntegral, innerSource)},
                                                 wasConstexpr);
          }

          if (!isPlus) // ptr - int
          {
               return std::make_unique<PRValue>(leftPointer,
                                                std::unique_ptr<SubtractionNode, IRDeleter>{
                                                    new (*this->_context.nodeAllocator)
                                                        SubtractionNode(std::move(left), std::move(right), source)},
                                                false);
          }

          // ptr + int
          return std::make_unique<PRValue>(
              leftPointer,
              std::unique_ptr<AdditionNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                           AdditionNode(std::move(left), std::move(right), source)},
              false);
     }

     if (leftPointer != nullptr && rightPointer != nullptr)
     {
          if (operator_ != ast::Operator::Minus)
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build("Cannot add two pointers", source));
               return nullptr;
          }

          if (leftPointer->BaseType() != rightPointer->BaseType())
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "Cannot subtract pointers to different types (" + left->Type()->Name() + " and " +
                           right->Type()->Name() + ")",
                       source));
               return nullptr;
          }

          TypeRequest typeRequest{};
          typeRequest.kind = TypeKind::Fundamental;
          typeRequest.data = PlatformIntegerRequest{.kind = PlatformIntegerKind::PtrDiff};
          const auto* resultType = GetContext().Get(typeRequest);

          return std::make_unique<PRValue>(resultType,
                                           std::unique_ptr<SubtractionNode, IRDeleter>{
                                               new (*this->_context.nodeAllocator)
                                                   SubtractionNode(std::move(left), std::move(right), source)},
                                           false);
     }

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes

          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "Cannot perform this binary operation on " + left->Type()->Name() + " and " + right->Type()->Name(),
                  left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     if (left->Type() != leftIntegral) // got promoted
     {
          const auto innerSource = left->Value()->Source();
          const auto wasConstexpr = left->IsConstantExpression();

          left = std::make_unique<PRValue>(
              leftIntegral,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(left), leftIntegral, innerSource)},
              wasConstexpr);
     }

     if (right->Type() != rightIntegral) // got promoted
     {
          const auto innerSource = right->Value()->Source();
          const auto wasConstexpr = right->IsConstantExpression();

          right = std::make_unique<PRValue>(
              rightIntegral,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(right), rightIntegral, innerSource)},
              wasConstexpr);
     }

     const auto* resultType = leftIntegral->CommonWith(rightIntegral);
     if (resultType == nullptr)
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "Cannot find a common integral type between " + left->Type()->Name() + " and " + left->Type()->Name(),
                  left->Value()->Source()));
          return nullptr;
     }

     if (operator_ == ast::Operator::Plus)
          return std::make_unique<PRValue>(
              resultType,
              std::unique_ptr<AdditionNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                           AdditionNode(std::move(left), std::move(right), source)},
              false);

     return std::make_unique<PRValue>(
         resultType,
         std::unique_ptr<SubtractionNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                         SubtractionNode(std::move(left), std::move(right), source)},
         false);
}

Expression ecpps::ir::IR::ParseMultiplicativeExpression(Expression left, ast::Operator operator_, Expression right,
                                                        const Location& source) const
{
     runtime_assert(operator_ == ast::Operator::Asterisk || operator_ == ast::Operator::Solidus ||
                        operator_ == ast::Operator::Percent,
                    "Operator was not multiplicative in a multiplicative-expression");

     const auto* leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     const auto* rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes

          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "Cannot perform this binary operation on " + left->Type()->Name() + " and " + right->Type()->Name(),
                  left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     if (left->Type() != leftIntegral) // got promoted
     {
          const auto innerSource = left->Value()->Source();
          const auto wasConstexpr = left->IsConstantExpression();

          left = std::make_unique<PRValue>(
              leftIntegral,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(left), leftIntegral, innerSource)},
              wasConstexpr);
     }

     if (right->Type() != rightIntegral) // got promoted
     {
          const auto innerSource = right->Value()->Source();
          const auto wasConstexpr = right->IsConstantExpression();

          right = std::make_unique<PRValue>(
              rightIntegral,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(right), rightIntegral, innerSource)},
              wasConstexpr);
     }

     const auto* resultType = leftIntegral->CommonWith(rightIntegral);
     if (resultType == nullptr)
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "Cannot find a common integral type between " + left->Type()->Name() + " and " + left->Type()->Name(),
                  left->Value()->Source()));
          return nullptr;
     }

     if (operator_ == ast::Operator::Asterisk)
          return std::make_unique<PRValue>(resultType,
                                           std::unique_ptr<MultiplicationNode, IRDeleter>{
                                               new (*this->_context.nodeAllocator)
                                                   MultiplicationNode(std::move(left), std::move(right), source)},
                                           false);

     if (operator_ == ast::Operator::Solidus)
          return std::make_unique<PRValue>(
              resultType,
              std::unique_ptr<DivideNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                         DivideNode(std::move(left), std::move(right), source)},
              false);

     return std::make_unique<PRValue>(
         resultType,
         std::unique_ptr<ModuloNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                    ModuloNode(std::move(left), std::move(right), source)},
         false);
}

Expression ecpps::ir::IR::ParseShiftExpression([[maybe_unused]] Expression left,
                                               [[maybe_unused]] ast::Operator operator_,
                                               [[maybe_unused]] Expression right,
                                               [[maybe_unused]] const Location& source)
{
     runtime_assert(operator_ == ast::Operator::LeftShift || operator_ == ast::Operator::RightShift,
                    "Invalid operator for a shift-expression");

     throw ecpps::TracedException(std::logic_error("Not implemented"));
}

// indirection
Expression ecpps::ir::IR::ParseDereferenceExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");
     if (operand->Type() == nullptr) return nullptr;

     if (IsArray(operand->Type()))
     {
          const auto* arrayType = operand->Type()->CastTo<typeSystem::ArrayType>();

          runtime_assert(arrayType != nullptr, "Expected an array type");

          TypeRequest pointerRequest{};
          pointerRequest.kind = TypeKind::Compound;
          pointerRequest.data = PointerRequest{.elementType = arrayType->ElementType()};

          const auto* variableType = GetContext().Get(pointerRequest);
          operand = ConvertTo(std::move(operand), variableType);

          if (operand == nullptr) return nullptr;
     }
     const auto* pointerType = operand->Type()->CastTo<typeSystem::PointerType>();

     if (pointerType == nullptr)
     {
          // TODO: Classes

          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "Cannot perform this unary operation on " + operand->Type()->Name(), operand->Value()->Source()));

          return nullptr;
     }

     // TODO: lvalue-to-rvalue conversions
     // if (!operand->IsPRValue())
     // {

     //      this->_context.diagnostics.get().diagnosticsList.push_back(
     //          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
     //              "A prvalue is required for an indirection", operand->Value()->Source()));

     //      return nullptr;
     // }

     return std::make_unique<LValue>(pointerType->BaseType(),
                                     std::unique_ptr<DereferenceNode, IRDeleter>{new (
                                         *this->_context.nodeAllocator) DereferenceNode(std::move(operand), source)},
                                     false);
}

Expression ecpps::ir::IR::ParseAddressOfExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     if (!operand->IsLValue())
     {

          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "An lvalue is required for a the address-of operator", operand->Value()->Source()));

          return nullptr;
     }

     // TODO:
     // 1. pointer-to-member
     // 2. function pointer

     TypeRequest pointerRequest{};
     pointerRequest.kind = TypeKind::Compound;
     pointerRequest.data = PointerRequest{.elementType = operand->Type()};
     const auto* pointerType = GetContext().Get(pointerRequest);

     return std::make_unique<PRValue>(pointerType,
                                      std::unique_ptr<AddressOfNode, IRDeleter>{new (
                                          *this->_context.nodeAllocator) AddressOfNode(std::move(operand), source)},
                                      false);
}

Expression ecpps::ir::IR::ParseSubscriptExpression(Expression left, Expression right, const Location& source) const
{
     runtime_assert(left != nullptr && right != nullptr, "Operands were null");

     const auto* leftPointer = left->Type()->CastTo<typeSystem::PointerType>();
     const auto* rightPointer = right->Type()->CastTo<typeSystem::PointerType>();
     const auto* leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     const auto* rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();
     const bool leftIsArray = IsArray(left->Type());
     const bool rightIsArray = IsArray(right->Type());

     if (((leftPointer == nullptr && !leftIsArray) || rightIntegral == nullptr) &&
         ((rightPointer == nullptr && !rightIsArray) || leftIntegral == nullptr))
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  std::format("Cannot subscript types `{}` and `{}`", left->Type()->Name(), right->Type()->Name()),
                  source));
          return nullptr;
     }

     // Decay array operands to pointers
     if (leftIsArray)
     {
          const auto* arrayType = left->Type()->CastTo<typeSystem::ArrayType>();
          runtime_assert(arrayType != nullptr, "Expected an array type");

          TypeRequest pointerRequest{};
          pointerRequest.kind = TypeKind::Compound;
          pointerRequest.data = PointerRequest{.elementType = arrayType->ElementType()};
          const auto* pointerType = GetContext().Get(pointerRequest);

          left = ConvertTo(std::move(left), pointerType);
          if (left == nullptr) return nullptr;
     }

     if (rightIsArray)
     {
          const auto* arrayType = right->Type()->CastTo<typeSystem::ArrayType>();
          runtime_assert(arrayType != nullptr, "Expected an array type");

          TypeRequest pointerRequest{};
          pointerRequest.kind = TypeKind::Compound;
          pointerRequest.data = PointerRequest{.elementType = arrayType->ElementType()};
          const auto* pointerType = GetContext().Get(pointerRequest);

          right = ConvertTo(std::move(right), pointerType);
          if (right == nullptr) return nullptr;
     }

     bool arrayOperandIsLValue = false;
     if (leftIsArray && left->IsLValue()) arrayOperandIsLValue = true;
     else if (rightIsArray && right->IsLValue())
          arrayOperandIsLValue = true;

     auto additionResult = ParseAdditiveExpression(std::move(left), ast::Operator::Plus, std::move(right), source);
     if (additionResult == nullptr) return nullptr;

     auto dereferenceResult = ParseDereferenceExpression(std::move(additionResult), source);
     if (dereferenceResult == nullptr) return nullptr;

     if (!arrayOperandIsLValue && !dereferenceResult->IsLValue())
     {
          return std::make_unique<XValue>(dereferenceResult->Type(), std::move(*dereferenceResult).Value(),
                                          dereferenceResult->IsConstantExpression());
     }
     return std::make_unique<LValue>(dereferenceResult->Type(), std::move(*dereferenceResult).Value(),
                                     dereferenceResult->IsConstantExpression());
}

Expression ecpps::ir::IR::ParsePreIncrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "A modifiable lvalue is required for a the builtin pre-increment operator",
                       operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<LValue>(
              operandType,
              std::unique_ptr<AdditionAssignNode, IRDeleter>{new (*this->_context.nodeAllocator) AdditionAssignNode(
                  std::move(operand),
                  std::make_unique<PRValue>(operandType,
                                            std::unique_ptr<IntegralNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                                                         IntegralNode(1, source)},
                                            true),
                  source)},
              false);
     }

     throw TracedException("Not implemented");
}

Expression ecpps::ir::IR::ParsePostIncrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "A modifiable lvalue is required for a the builtin post-increment operator",
                       operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<PRValue>(
              operandType,
              std::unique_ptr<PostIncrementNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                                PostIncrementNode(std::move(operand), 1, source)},
              false);
     }

     throw TracedException("Not implemented");
}
Expression ecpps::ir::IR::ParsePreDecrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "A modifiable lvalue is required for a the builtin pre-decrement operator",
                       operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<LValue>(
              operandType,
              std::unique_ptr<SubtractionAssignNode, IRDeleter>{
                  new (*this->_context.nodeAllocator) SubtractionAssignNode(
                      std::move(operand),
                      std::make_unique<PRValue>(operandType,
                                                std::unique_ptr<IntegralNode, IRDeleter>{
                                                    new (*this->_context.nodeAllocator) IntegralNode(1, source)},
                                                true),
                      source)},
              false);
     }

     throw TracedException("Not implemented");
}

Expression ecpps::ir::IR::ParsePostDecrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "A modifiable lvalue is required for a the builtin post-decrement operator",
                       operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<PRValue>(
              operandType,
              std::unique_ptr<PostDecrementNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                                PostDecrementNode(std::move(operand), 1, source)},
              false);
     }

     throw TracedException("Not implemented");
}

Expression ecpps::ir::IR::ParseUnaryExpression(const ast::UnaryOperatorNode& node)
{
     const auto operator_ = node.Value();
     auto operand = ParseExpression(node.Operand());
     if (operand == nullptr) return nullptr;

     switch (operator_)
     {
     case ast::Operator::Plus:
     case ast::Operator::Minus: throw TracedException(std::logic_error("Not implemented"));
     case ast::Operator::Asterisk: return this->ParseDereferenceExpression(std::move(operand), node.Source());
     case ast::Operator::Ampersand: return this->ParseAddressOfExpression(std::move(operand), node.Source());
     case ast::Operator::Increment:
          return node.UnaryType() == ast::UnaryOperatorType::Prefix
                     ? this->ParsePreIncrementExpression(std::move(operand), node.Source())
                     : this->ParsePostIncrementExpression(std::move(operand), node.Source());
     case ast::Operator::Decrement:
          return node.UnaryType() == ast::UnaryOperatorType::Prefix
                     ? this->ParsePreDecrementExpression(std::move(operand), node.Source())
                     : this->ParsePostDecrementExpression(std::move(operand), node.Source());
     default: throw TracedException(std::logic_error("Invalid unary operator"));
     }
}

Expression ecpps::ir::IR::ParseBinaryExpression(const ast::BinaryOperatorNode& node)
{
     const auto operator_ = node.Value();
     auto left = ParseExpression(node.Left());
     auto right = ParseExpression(node.Right());
     if (left == nullptr || right == nullptr) return nullptr;

     switch (operator_)
     {
     case ast::Operator::Plus:
     case ast::Operator::Minus:
          return this->ParseAdditiveExpression(std::move(left), operator_, std::move(right), node.Source());
     case ast::Operator::Asterisk:
     case ast::Operator::Percent:
     case ast::Operator::Solidus:
          return this->ParseMultiplicativeExpression(std::move(left), operator_, std::move(right), node.Source());
     case ast::Operator::LeftShift:
     case ast::Operator::RightShift:
          return ecpps::ir::IR::ParseShiftExpression(std::move(left), operator_, std::move(right), node.Source());
     case ast::Operator::Subscript:
          return this->ParseSubscriptExpression(std::move(left), std::move(right), node.Source());
     default: throw TracedException(std::logic_error("Invalid binary operator"));
     }
}

struct CompareByPriority
{
     bool operator()(const std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>& a,
                     const std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>& b) const
     {
          return a.first < b.first;
     }
};

Expression ecpps::ir::IR::ParseCallExpression(const ast::CallOperatorNode& node)
{
     // fast/hot path for identifiers
     if (auto* const identifierFunction = dynamic_cast<ast::IdentifierNode*>(node.Function().get()))
     {
          const std::string& name = identifierFunction->Value();
          std::priority_queue<
              std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>,
              std::vector<std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>>,
              CompareByPriority>
              candidates{};
          // TODO: Traverse contexts
          for (const auto& context : this->_context.contextSequence)
          {
               std::vector<Expression> arguments = node.Arguments() |
                                                   std::views::transform([this](const ast::NodePointer& argument)
                                                                         { return this->ParseExpression(argument); }) |
                                                   std::ranges::to<std::vector>();

               for (const auto& candidate : context->GetScope().functions)
               {
                    if (candidate->name != name) continue;

                    const auto match = ecpps::ir::IR::MatchFunction(candidate, arguments);
                    if (!match) continue;
                    candidates.emplace(match, candidate);
               }

               if (candidates.empty()) continue;
               // TODO: Ambiguity
               const auto& candidate = candidates.top().second;
               auto moveRange = std::ranges::subrange(std::make_move_iterator(arguments.begin()),
                                                      std::make_move_iterator(arguments.end()));

               std::vector<Expression> evaluatedArguments = std::views::zip(candidate->parameters, moveRange) |
                                                            std::views::transform(
                                                                [this](auto&& pair)
                                                                {
                                                                     auto&& [param, arg] = pair;
                                                                     return ConvertTo(std::move(arg), param.type);
                                                                }) |
                                                            std::ranges::to<std::vector>();

               auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{
                   new (*this->_context.nodeAllocator)
                       FunctionCallNode(candidate, std::move(evaluatedArguments), node.Source())};

               // TODO: Check for references; lvalue reference => lvalue; rvalue reference => xvalue
               return std::make_unique<PRValue>(candidate->returnType, std::move(call), false);
          }

          std::string errorMessage = "Unresolved function " + name;

          std::vector<Expression> argumentsForAnalysis =
              node.Arguments() |
              std::views::transform([this](const ast::NodePointer& argument)
                                    { return this->ParseExpression(argument); }) |
              std::ranges::to<std::vector>();

          auto exactMatches = CollectExactMatches(name, this->_context.contextSequence);
          if (!exactMatches.empty())
          {
               auto mainError = ecpps::diagnostics::DiagnosticsBuilder<diagnostics::UnresolvedSymbolError>{}.Build(
                   name, errorMessage, identifierFunction->Source());

               std::vector<const ast::Node*> argumentNodes;
               argumentNodes.reserve(node.Arguments().Size());
               for (const auto& argNode : node.Arguments()) argumentNodes.push_back(argNode.get());
               for (const auto& func : exactMatches)
               {
                    auto candidateNote = std::make_unique<diagnostics::Information>(
                        "candidate", "Candidate: " + FormatFunctionSignature(func), func->source);

                    auto failureInfo = AnalyseCandidateFailure(func, argumentsForAnalysis);
                    for (const auto& [paramIndex, failure] : failureInfo.parameterFailures)
                    {
                         Location argSource = (std::cmp_not_equal(paramIndex, -1) && paramIndex > 0 &&
                                               paramIndex <= argumentNodes.size())
                                                  ? argumentNodes[paramIndex - 1]->Source()
                                                  : identifierFunction->Source();

                         auto failureNote = std::make_unique<diagnostics::Information>("note", failure, argSource);

                         if (std::cmp_not_equal(paramIndex, -1) && paramIndex > 0 &&
                             paramIndex <= argumentsForAnalysis.size() && paramIndex <= func->parameters.size())
                         {
                              const auto& arg = argumentsForAnalysis[paramIndex - 1];
                              const auto& param = func->parameters[paramIndex - 1];

                              if (arg != nullptr)
                              {
                                   const auto& fromType = arg->Type();
                                   const auto& toType = param.type;

                                   if ((ecpps::typeSystem::IsPointer(fromType) ||
                                        ecpps::typeSystem::IsArray(fromType)) &&
                                       !ecpps::typeSystem::IsPointer(toType) && !ecpps::typeSystem::IsArray(toType))
                                        failureNote->SubDiagnostics().push_back(
                                            std::make_unique<diagnostics::Information>(
                                                "note", "Did you mean to dereference the argument?", argSource));
                              }
                         }

                         candidateNote->SubDiagnostics().push_back(std::move(failureNote));
                    }

                    mainError->SubDiagnostics().push_back(std::move(candidateNote));
               }

               this->_context.diagnostics.get().diagnosticsList.push_back(std::move(mainError));
          }
          else
          {
               auto mainError = std::make_unique<diagnostics::UnresolvedSymbolError>(name, errorMessage,
                                                                                     identifierFunction->Source());

               auto similarNames =
                   CollectSimilarNames(name, argumentsForAnalysis.size(), this->_context.contextSequence);
               if (!similarNames.empty())
               {
                    auto didYouMeanNote = std::make_unique<diagnostics::Information>(
                        "note", "Did you mean:", identifierFunction->Source());

                    for (const auto& [similarName, distance] : similarNames)
                    {
                         didYouMeanNote->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                             "", std::format("    {} [distance={}]", similarName, distance),
                             identifierFunction->Source()));
                    }

                    mainError->SubDiagnostics().push_back(std::move(didYouMeanNote));
               }

               this->_context.diagnostics.get().diagnosticsList.push_back(std::move(mainError));
          }

          return nullptr;
     }

     // TODO: More
     return nullptr;
}

Expression ecpps::ir::IR::ParseStringLiteral(const ast::StringLiteralNode& expression) const
{
     const auto length = expression.Value().length();
     const auto* elementType =
         GetContext().Get(TypeRequest{.kind = TypeKind::Fundamental,
                                      .qualifiers = typeSystem::Qualifiers::Const,
                                      .data = StandardSignedIntegerRequest{.isCharWithoutSign = true}});
     std::vector<std::uint32_t> values{};
     values.reserve(length + 1);
     for (const auto character : expression.Value()) values.emplace_back(character);
     values.emplace_back(0);

     TypeRequest arrayRequest{};
     arrayRequest.kind = TypeKind::Compound;
     arrayRequest.data = BoundedArrayRequest{.elementType = elementType, .size = length + 1};

     const auto* arrayType = GetContext().Get(arrayRequest);

     auto node = std::unique_ptr<IntegerArrayNode, ecpps::BumpAllocator::Deleter>(
         new (*this->_context.nodeAllocator)
             IntegerArrayNode(std::move(values), elementType->CastTo<typeSystem::IntegralType>(), expression.Source()));
     return std::make_unique<PRValue>(arrayType, std::move(node), true);
}

Expression ecpps::ir::IR::ParseIdExpression(const ast::IdentifierNode& expression)
{
     // TODO: Proper name lookup please. Also CONTEXT MATTERS REALLY! Overload resolution! Hello?

     const std::string& name = expression.Value();

     for (const auto& context : this->_context.contextSequence)
     {
          const auto& scope = context->GetScope();
          if (const auto* const functionScope = dynamic_cast<const FunctionScope*>(&scope))
          {
               for (const auto& local : functionScope->locals)
               {
                    // TODO: Constexpr evaluation
                    if (local.name == name)
                    {
                         return std::make_unique<LValue>(
                             local.type,
                             std::unique_ptr<LoadNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                                      LoadNode(local.name, expression.Source())},
                             false);
                    }
               }
          }
     }

     std::string errorMessage = "Unresolved identifier " + name;

     auto mainError = std::make_unique<diagnostics::UnresolvedSymbolError>(name, errorMessage, expression.Source());

     auto similarNames = CollectSimilarIdentifierNames(name, this->_context.contextSequence);
     if (!similarNames.empty())
     {
          auto didYouMeanNote =
              std::make_unique<diagnostics::Information>("note", "Did you mean:", expression.Source());

          for (const auto& [similarName, distance] : similarNames)
          {
               didYouMeanNote->SubDiagnostics().push_back(
                   std::make_unique<diagnostics::Information>("", "    " + similarName, expression.Source()));
          }

          mainError->SubDiagnostics().push_back(std::move(didYouMeanNote));
     }

     this->_context.diagnostics.get().diagnosticsList.push_back(std::move(mainError));

     return nullptr;
}

Expression ecpps::ir::IR::ParseExpression(const ast::NodePointer& expression)
{
     if (expression == nullptr) return nullptr;

     if (auto* const integerLiteral = dynamic_cast<ast::IntegerLiteralNode*>(expression.get());
         integerLiteral != nullptr)
          return std::make_unique<PRValue>(typeSystem::g_int.get(),
                                           std::unique_ptr<ir::IntegralNode, IRDeleter>{
                                               new (*this->_context.nodeAllocator)
                                                   ir::IntegralNode(integerLiteral->Value(), expression->Source())},
                                           true);

     if (auto* const characterLiteral = dynamic_cast<ast::CharacterLiteralNode*>(expression.get());
         characterLiteral != nullptr)
          return std::make_unique<PRValue>(typeSystem::g_char.get(),
                                           std::unique_ptr<ir::IntegralNode, IRDeleter>{
                                               new (*this->_context.nodeAllocator)
                                                   ir::IntegralNode(characterLiteral->Value(), expression->Source())},
                                           true);
     if (auto* const binaryExpression = dynamic_cast<ast::BinaryOperatorNode*>(expression.get());
         binaryExpression != nullptr)
          return this->ParseBinaryExpression(*binaryExpression);
     if (auto* const unaryExpression = dynamic_cast<ast::UnaryOperatorNode*>(expression.get());
         unaryExpression != nullptr)
          return this->ParseUnaryExpression(*unaryExpression);
     if (auto* const functionCall = dynamic_cast<ast::CallOperatorNode*>(expression.get()); functionCall != nullptr)
          return this->ParseCallExpression(*functionCall);
     if (auto* const identifier = dynamic_cast<ast::IdentifierNode*>(expression.get()); identifier != nullptr)
          return this->ParseIdExpression(*identifier);
     if (auto* const stringLiteral = dynamic_cast<ast::StringLiteralNode*>(expression.get()); stringLiteral != nullptr)
          return this->ParseStringLiteral(*stringLiteral);

     this->_context.diagnostics.get().diagnosticsList.push_back(
         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
             expression->ToString(0) + " cannot appear in this context.", expression->Source()));

     return nullptr;
}
[[nodiscard]] ecpps::ir::TypeRequest ecpps::ir::IR::TypeASTToRequest(const ast::NodePointer& type)
{
     auto& typeContext = GetContext();

     const ast::Node* base = type.get();
     ecpps::ir::TypeRequest request{};

     if (const auto* basicType = dynamic_cast<const ast::BasicType*>(base); basicType != nullptr)
     {
          const auto& value = basicType->Value();

          const auto qualifiers = basicType->IsConst() && basicType->IsVolatile()
                                      ? typeSystem::Qualifiers::ConstVolatile
                                  : basicType->IsConst()    ? typeSystem::Qualifiers::Const
                                  : basicType->IsVolatile() ? typeSystem::Qualifiers::Volatile
                                                            : typeSystem::Qualifiers::None;

          const auto scopeCount = this->_context.contextSequence.Size();
          for (std::size_t i = scopeCount; i > 0; --i)
          {
               const auto& scope = this->_context.contextSequence[i - 1]->GetScope();
               const auto aliasIt = scope.typeAliases.find(value);
               if (aliasIt != scope.typeAliases.end())
               {
                    const auto* targetType = aliasIt->second;
                    // TODO: Apply qualifiers from basicType to the resolved type

                    if (const auto* intType = targetType->CastTo<typeSystem::IntegralType>())
                    {
                         request.kind = TypeKind::Fundamental;
                         StandardSignedIntegerRequest signedRequest{};
                         signedRequest.signedness = intType->Sign();
                         signedRequest.size = intType->Kind();
                         signedRequest.isCharWithoutSign = (intType == typeSystem::g_char.get());
                         request.data = signedRequest;
                    }
                    else if (const auto* ptrType = targetType->CastTo<typeSystem::PointerType>())
                    {
                         request.kind = TypeKind::Compound;
                         request.data = PointerRequest{.elementType = ptrType->BaseType()};
                    }
                    else if (targetType == typeSystem::g_void.get())
                    {
                         request.kind = TypeKind::Fundamental;
                         request.data = VoidRequest{};
                    }
                    else
                    {
                         throw TracedException("Type aliases for this kind of type not yet implemented");
                    }

                    if (basicType->IsConst() && basicType->IsVolatile())
                         request.qualifiers = typeSystem::Qualifiers::ConstVolatile;
                    else if (basicType->IsConst())
                         request.qualifiers = typeSystem::Qualifiers::Const;
                    else if (basicType->IsVolatile())
                         request.qualifiers = typeSystem::Qualifiers::Volatile;

                    return request;
               }
          }

          request.kind = TypeKind::Fundamental;

          if (basicType->IsConst() && basicType->IsVolatile())
               request.qualifiers = typeSystem::Qualifiers::ConstVolatile;
          else if (basicType->IsConst())
               request.qualifiers = typeSystem::Qualifiers::Const;
          else if (basicType->IsVolatile())
               request.qualifiers = typeSystem::Qualifiers::Volatile;

          if (value == "void")
          {
               request.data = VoidRequest{};
               return request;
          }

          StandardSignedIntegerRequest signedRequest{};

          std::vector<std::string_view> tokens;
          {
               std::string_view sv{value};
               while (!sv.empty())
               {
                    const auto pos = sv.find(' ');
                    tokens.emplace_back(sv.substr(0, pos));
                    if (pos == std::string_view::npos) break;
                    sv.remove_prefix(pos + 1);
               }
          }

          const auto has = [&](std::string_view t) { return std::ranges::contains(tokens, t); };

          const auto longCount = std::ranges::count(tokens, "long");

          const bool isChar = has("char");
          const bool isShort = has("short");
          const bool isInt = has("int");
          const bool isLong = longCount > 0;

          if (!(isChar || isShort || isInt || isLong))
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build("Invalid type specifier: " + value,
                                                                                   basicType->Source()));
          }

          if (has("char") && !has("signed") && !has("unsigned")) { signedRequest.isCharWithoutSign = true; }
          else
          {
               signedRequest.signedness =
                   has("unsigned") ? typeSystem::Signedness::Unsigned : typeSystem::Signedness::Signed;

               if (has("char")) signedRequest.size = typeSystem::TypeKind::Char;
               else if (has("short"))
                    signedRequest.size = typeSystem::TypeKind::Short;
               else if (longCount == 1)
                    signedRequest.size = typeSystem::TypeKind::Long;
               else if (longCount >= 2)
                    signedRequest.size = typeSystem::TypeKind::LongLong;
               else
                    signedRequest.size = typeSystem::TypeKind::Int;
          }
          request.data = signedRequest;
          return request;
     }
     if (const auto* pointerType = dynamic_cast<const ast::PointerType*>(base); pointerType != nullptr)
     {
          const auto baseTypeRequest = TypeASTToRequest(pointerType->BaseType());
          const auto* baseType = typeContext.Get(baseTypeRequest);
          request.kind = TypeKind::Compound;
          // TODO: Qualifiers

          request.data = PointerRequest{.elementType = baseType};
          return request;
     }
     if (const auto* qualifiedType = dynamic_cast<const ast::QualifiedType*>(base); qualifiedType != nullptr)
     {
          // TODO: Qualified name lookup
          if (qualifiedType->Sections().Size() == 0)
          {
               const auto* unqualified = qualifiedType->UnqualifiedType().get();
               if (dynamic_cast<const ast::BasicType*>(unqualified))
                    return TypeASTToRequest(qualifiedType->UnqualifiedType());
          }

          throw TracedException("Qualified types with namespaces not yet implemented");
     }

     throw TracedException("not implemented");
}

ecpps::typeSystem::NonowningTypePointer ecpps::ir::IR::ParseType(const ast::NodePointer& type)
{
     auto& typeContext = GetContext();
     const auto request = TypeASTToRequest(type);

     return typeContext.Get(request);
}

Expression ecpps::ir::IR::ConvertTo(Expression expression, typeSystem::NonowningTypePointer toType) const
{
     if (expression == nullptr || toType == nullptr) return nullptr;

     const auto constexprResult = expression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
     if (constexprResult.has_value())
     {
          const auto& value = *constexprResult;
          try
          {
               expression =
                   ConstantEvaluationResultToExpression(value, expression->Type(), *this->_context.nodeAllocator);
          }
          catch (...)
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   std::make_unique<diagnostics::ConstantEvaluationWarning>(
                       "Failed to use the constant expression evaluation result", expression->Value()->Source()));
          }
     }

     const auto comparison = toType->CompareTo(expression->Type());

     if (!comparison.IsValid())
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "Cannot convert from " + expression->Type()->Name() + " (aka " + expression->Type()->RawName() +
                      ") to type " + toType->Name() + " (aka " + toType->RawName() + ")",
                  expression->Value()->Source()));
          return nullptr;
     }

     if (comparison.Sequence().Size() == 0) return expression;

     if (comparison.Sequence().Size() == 1)
     {
          const auto& conversion = *comparison.Sequence().begin();
          if (conversion == typeSystem::ConversionSequence::ConversionKind::IntegralConversion)
          {
               const auto* intType = toType->CastTo<typeSystem::IntegralType>();
               runtime_assert(intType != nullptr,
                              std::format("Presumed integral type {} turned out not to be one", toType->RawName()));
               return ConvertIntegral(std::move(expression), intType);
          }
          if (conversion == typeSystem::ConversionSequence::ConversionKind::ArrayToPointer)
          {
               const auto* pointerType = toType->CastTo<typeSystem::PointerType>();
               runtime_assert(pointerType != nullptr,
                              std::format("Presumed pointer type {} turned out not to be one", toType->RawName()));

               runtime_assert(expression->IsRValue() || expression->IsLValue(), "Expected an rvalue or an lvalue for "
                                                                                "the array-to-pointer conversion. See "
                                                                                "[conv.array]"); // [conv.array]

               const auto wasConstexpr = expression->IsConstantExpression();
               if (expression->IsPRValue())
               {
                    NodePointer decayNode{};

                    if (auto* const intArray = dynamic_cast<IntegerArrayNode*>(expression->Value().get()))
                    {
                         const auto source = intArray->Source();

                         decayNode = std::unique_ptr<TemporaryIntegerArrayDecayNode, IRDeleter>(
                             new (*this->_context.nodeAllocator)
                                 TemporaryIntegerArrayDecayNode(std::move(expression), source));
                    }
                    else
                         throw TracedException("array-to-pointer is not supported yet");

                    return std::make_unique<PRValue>(pointerType, std::move(decayNode), wasConstexpr);
               }

               if (expression->IsLValue())
               {
                    NodePointer decayNode{};

                    if (auto* const loadNode = dynamic_cast<LoadNode*>(expression->Value().get()); loadNode != nullptr)
                    {
                         const auto source = loadNode->Source();

                         decayNode = std::unique_ptr<LoadArrayDecayNode, IRDeleter>(
                             new (*this->_context.nodeAllocator) LoadArrayDecayNode(std::move(expression), source));
                    }
                    else
                         throw TracedException("array-to-pointer is not supported yet");

                    return std::make_unique<PRValue>(pointerType, std::move(decayNode), wasConstexpr);
               }

               throw TracedException(std::logic_error("Unsupported conversion"));
          }
          if (conversion == typeSystem::ConversionSequence::ConversionKind::PointerConversion ||
              conversion == typeSystem::ConversionSequence::ConversionKind::QualifierConversion)
          {
               const auto wasConstexpr = expression->IsConstantExpression();
               const auto source = expression->Value()->Source();
               const bool isPRValue = expression->IsPRValue();
               const bool isXValue = expression->IsXValue();
               const bool isLValue = expression->IsLValue();

               auto castNode = std::unique_ptr<PointerConversionNode, IRDeleter>(
                   new (*this->_context.nodeAllocator) PointerConversionNode(std::move(expression), toType, source));

               if (isPRValue) return std::make_unique<PRValue>(toType, std::move(castNode), wasConstexpr);
               if (isXValue) return std::make_unique<XValue>(toType, std::move(castNode), wasConstexpr);
               if (isLValue) return std::make_unique<LValue>(toType, std::move(castNode), wasConstexpr);

               throw TracedException(std::logic_error("Unknown value category"));
          }
     }

     throw TracedException(std::logic_error("Unsupported conversion"));
}

bool ecpps::ir::IR::IsEligibleForStringLiteralInitialisation(typeSystem::NonowningTypePointer type) const
{
     return IsCharacter(type);
}

Expression ecpps::ir::IR::ConvertIntegral(Expression expression, const typeSystem::IntegralType* type) const
{
     const auto& expressionType = expression->Type();
     const auto& source = expression->Value()->Source();

     if (auto* const integralNode = dynamic_cast<IntegralNode*>(expression->Value().get()); integralNode != nullptr)
     {
          // promotion
          return std::make_unique<PRValue>(type, std::move(std::move(*expression).Value()),
                                           expression->IsConstantExpression());
     }
     if (IsArithmetic(expressionType))
          return std::make_unique<PRValue>(
              type,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(expression), type, source)},
              false);
     return nullptr; // TODO: Return implicit conversion node
}

ecpps::ir::MatchingScore ecpps::ir::IR::MatchFunction(const std::shared_ptr<FunctionScope>& function,
                                                      const std::vector<Expression>& arguments)
{
     if (arguments.size() != function->parameters.size()) return MatchingScore::NotMatching; // TODO: default arguments

     MatchingScore totalScore = MatchingScore::MaxScore;

     auto parameterIterator = function->parameters.begin();
     for (const auto& argument : arguments)
     {
          const auto& parameter = *parameterIterator++;
          if (argument == nullptr) continue;
          const auto& fromType = argument->Type();
          const auto& toType = parameter.type;

          ecpps::typeSystem::ConversionSequence seq = toType->CompareTo(fromType);

          ImplicitConversion::RefBindingKind refKind = ImplicitConversion::RefBindingKind::None;
          // if (IsReference(toType)) // TODO: Implement references
          //{
          //      const auto& referenceType = dynamic_cast<typeSystem::ReferenceType&>(toType);
          //      if (referenceType.IsLValueReference())
          //      {
          //           if (argument->IsLValue()) refKind = ImplicitConversion::RefBindingKind::LValueRef;
          //           else
          //                refKind = ImplicitConversion::RefBindingKind::BindToTemporary;
          //      }
          //      else if (referenceType.IsRValueReference())
          //      {
          //           if (argument->IsXValue()) refKind = ImplicitConversion::RefBindingKind::RValueRef;
          //           else
          //                refKind = ImplicitConversion::RefBindingKind::ConstRValueRef;
          //      }
          // }

          const bool valid = seq.IsValid() && refKind != ImplicitConversion::RefBindingKind::IllFormed;
          if (!valid) return MatchingScore::NotMatching;

          const ImplicitConversion conversion{std::move(seq), refKind, true};

          totalScore += conversion.Rank();
     }

     return totalScore;
}

ecpps::ir::MatchingScore ecpps::ir::ImplicitConversion::Rank(void) const noexcept
{
     if (!this->isValid || !this->typeSequence.IsValid()) return MatchingScore::MaxScore; // invalid

     MatchingScore cost = MatchingScore::NotMatching;

     for (const auto kind : this->typeSequence.Sequence()) cost += std::to_underlying(kind);

     switch (this->refKind)
     {
     case RefBindingKind::None: break;
     case RefBindingKind::LValueRef: cost += 2; break;
     case RefBindingKind::ConstLValueRef: cost += 3; break;
     case RefBindingKind::RValueRef: cost += 4; break;
     case RefBindingKind::ConstRValueRef: cost += 5; break;
     case RefBindingKind::BindToTemporary: cost += 6; break;
     case RefBindingKind::IllFormed: return MatchingScore::MaxScore; // invalid
     }

     return cost;
}

ecpps::ir::ImplicitConversion ecpps::ir::MatchImplicitConversion(const Expression& expression,
                                                                 typeSystem::NonowningTypePointer type)
{
     ImplicitConversion::RefBindingKind referenceKind{};
     // if not a reference
     referenceKind = expression->IsPRValue() ? ImplicitConversion::RefBindingKind::BindToTemporary
                                             : ImplicitConversion::RefBindingKind::None;
     // TODO: references (the sole reason this function even exists)

     return ImplicitConversion{type->CompareTo(expression->Type()), referenceKind, true};
}
