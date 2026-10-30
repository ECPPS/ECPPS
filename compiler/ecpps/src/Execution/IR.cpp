#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <algorithm>
#include <bit>
#include <cctype>
#include <deque>
#include <format>
#include <functional>
#include <limits>
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
#include "Execution/MemoryAccess.h"
#include "Expressions.h"
#include "HighLevel.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
#include "Procedural.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"
#include "TypeSystem/ClassInfo.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

namespace
{
     [[nodiscard]] std::string FormatTypeName(const ecpps::typeSystem::NonowningTypePointer type)
     {
          if (type == nullptr) return "__unknown";
          std::string name = type->Name();
          while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())) != 0) name.pop_back();
          return name;
     }

     [[nodiscard, maybe_unused]] ecpps::ir::MemoryAccess AccessFor(const ecpps::typeSystem::NonowningTypePointer type)
     {
          ecpps::ir::MemoryAccess access{};
          access.alignment = static_cast<std::uint32_t>(type->Alignment());
          if (const auto* qualified = type->CastTo<ecpps::typeSystem::QualifiedType>())
               access.isVolatile = qualified->IsVolatile();
          return access;
     }
     [[nodiscard]] std::size_t ConversionCost(const ecpps::typeSystem::ConversionSequence::ConversionKind kind) noexcept
     {
          using Kind = ecpps::typeSystem::ConversionSequence::ConversionKind;
          switch (kind)
          {
          case Kind::LvalueToRValue:
          case Kind::ArrayToPointer:
          case Kind::FunctionToPointer:
          case Kind::Materialisation:
          case Kind::QualifierConversion: return 0;
          case Kind::IntegralPromotion:
          case Kind::FloatingPointPromotion: return 1;
          case Kind::IntegralConversion:
          case Kind::FloatingPointConversion:
          case Kind::FloatingIntegralConversion:
          case Kind::PointerConversion:
          case Kind::MemberFunctionConversion:
          case Kind::FunctionPointerConversion: return 2;
          case Kind::BooleanConversion: return 3;
          case Kind::UserDefined: return 4;
          case Kind::Ellipsis: return 5;
          }
          return 5;
     }
     std::string FormatFunctionSignature(const std::shared_ptr<ecpps::ir::FunctionScope>& func)
     {
          std::string signature;
          if (func->memberClassInfo != nullptr)
          {
               if (func->isStatic) signature += "static ";
               signature += func->memberClassInfo->DisplayName() + "::";
          }
          signature += func->Name().value_or("__unknown") + "(";
          bool first = true;
          for (const auto& param : func->parameters)
          {
               if (!first) signature += ", ";
               first = false;
               signature += FormatTypeName(param.type);
               if (!param.name.empty()) signature += " " + param.name;
          }
          signature += ")";
          if (func->memberClassInfo != nullptr && !func->isStatic)
          {
               if (func->memberFunctionInfo.isConst) signature += " const";
               if (func->memberFunctionInfo.isVolatile) signature += " volatile";
               switch (func->memberFunctionInfo.refQualifier)
               {
               case ecpps::abi::MemberRefQualifier::None: break;
               case ecpps::abi::MemberRefQualifier::LValue: signature += " &"; break;
               case ecpps::abi::MemberRefQualifier::RValue: signature += " &&"; break;
               }
          }
          if (func->returnType)
          {
               signature += " -> " + FormatTypeName(func->returnType);
          }
          return signature;
     }
     [[nodiscard]] bool IsIndirectLValue(const ecpps::Expression& expression)
     {
          return expression != nullptr &&
                 dynamic_cast<const ecpps::ir::high::DereferenceNode*>(expression->Value().get()) != nullptr;
     }
     constexpr std::string_view IndirectStoreUnsupported =
          "Modifying an object through a pointer is not supported yet (the IR has no indirect store)";

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
          const std::string& name, std::deque<ecpps::ir::ContextPointer>& contextSequence)
     {
          std::vector<std::shared_ptr<ecpps::ir::FunctionScope>> exactMatches;
          for (const auto& context : contextSequence)
               for (const auto& func : context->GetScope().functions)
                    if (func->Name() == name) exactMatches.push_back(func);

          return exactMatches;
     }

     std::vector<std::pair<std::string, std::size_t>> CollectSimilarNames(
          const std::string& name, std::size_t argumentCount, std::deque<ecpps::ir::ContextPointer>& contextSequence)
     {
          std::vector<std::pair<std::string, std::size_t>> similar;
          std::set<std::string> seen;

          for (const auto& context : contextSequence)
          {
               for (const auto& func : context->GetScope().functions)
               {
                    runtime_assert(func != nullptr, "Function pointer cannot be null");
                    runtime_assert(func->Name().has_value(), "Function must have a name");
                    const auto& functionName = func->Name().value(); // NOLINT

                    if (seen.contains(functionName)) continue;

                    seen.insert(functionName);

                    const auto nameDistance = LevenshteinDistance(name, functionName);
                    if (nameDistance == 0) continue;

                    if (nameDistance > std::max<std::size_t>(3, name.length() / 3)) continue;

                    const std::size_t paramCount = func->parameters.size();
                    const std::size_t argPenalty =
                         (paramCount > argumentCount) ? (paramCount - argumentCount) : (argumentCount - paramCount);

                    const std::size_t score = nameDistance + (argPenalty * 2);

                    if (score > 5) continue;

                    similar.emplace_back(functionName, score);
               }
          }

          std::ranges::sort(similar,
                            [](const auto& a, const auto& b)
                            {
                                 return a.second < b.second;
                            });

          return similar;
     }

     std::vector<std::pair<std::string, std::size_t>> CollectSimilarIdentifierNames(
          const std::string& name, std::deque<ecpps::ir::ContextPointer>& contextSequence)
     {
          std::vector<std::pair<std::string, std::size_t>> similar;
          std::set<std::string> seen;

          for (const auto& context : contextSequence)
          {
               const auto& scope = context->GetScope();
               if (const auto* const functionScope = dynamic_cast<const ecpps::ir::FunctionScope*>(&scope))
               {
                    for (const auto& local : functionScope->Locals())
                    {
                         const auto localName = local.Name();
                         if (seen.contains(localName)) continue;
                         seen.insert(localName);

                         const auto distance = LevenshteinDistance(name, localName);
                         if (distance > 0 && distance <= std::max<std::size_t>(3, localName.length() / 3))
                              similar.emplace_back(localName, distance);
                    }
               }

               for (const auto& func : scope.functions)
               {
                    runtime_assert(func != nullptr, "Function pointer cannot be null");
                    runtime_assert(func->Name().has_value(), "Function must have a name");
                    const auto& functionName = func->Name().value(); // NOLINT
                    if (seen.contains(functionName)) continue;
                    seen.insert(functionName);

                    const auto distance = LevenshteinDistance(name, functionName);
                    if (distance > 0 && distance <= std::max<std::size_t>(3, functionName.length() / 3))
                         similar.emplace_back(functionName, distance);
               }
          }

          std::ranges::sort(similar,
                            [](const auto& a, const auto& b)
                            {
                                 return a.second < b.second;
                            });

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
          }

          const std::size_t matchedCount = std::min(arguments.size(), function->parameters.size());
          for (std::size_t index = 0; index < matchedCount; index++)
          {
               const auto& argument = arguments[index];
               const auto& parameter = function->parameters[index];
               const std::size_t argumentIndex = index + 1;
               if (argument == nullptr)
               {
                    info.parameterFailures.emplace_back(
                         argumentIndex,
                         std::format("Argument {} cannot be checked because its expression is invalid", argumentIndex));
                    continue;
               }

               const auto& fromType = argument->Type();
               const auto& toType = parameter.type;

               ecpps::typeSystem::ConversionSequence seq = toType->CompareTo(fromType);

               if (!seq.IsValid())
               {
                    std::string reason =
                         std::format("Argument {} of type '{}' cannot convert to parameter '{}' of type '{}'",
                                     argumentIndex, FormatTypeName(fromType),
                                     parameter.name.empty() ? "<unnamed>" : parameter.name, FormatTypeName(toType));
                    info.parameterFailures.emplace_back(argumentIndex, std::move(reason));
               }
          }

          for (std::size_t index = matchedCount; index < arguments.size(); index++)
               info.parameterFailures.emplace_back(
                    index + 1, std::format("Argument {} has no corresponding parameter", index + 1));

          for (std::size_t index = matchedCount; index < function->parameters.size(); index++)
          {
               const auto& parameter = function->parameters[index];
               info.parameterFailures.emplace_back(
                    index + 1,
                    std::format("Missing argument {} for parameter '{}' of type '{}'", index + 1,
                                parameter.name.empty() ? "<unnamed>" : parameter.name, FormatTypeName(parameter.type)));
          }

          return info;
     }

     bool DeclaresNonType(const ecpps::ir::Scope& scope, const std::string& name)
     {
          for (const auto& func : scope.functions)
               if (func->Name() == name) return true;
          if (const auto* fn = dynamic_cast<const ecpps::ir::FunctionScope*>(&scope))
               for (const auto& local : fn->Locals())
                    if (local.Name() == name) return true;
          return false;
     }

     ecpps::typeSystem::ClassKind ToClassKind(const ecpps::ast::ClassKey key)
     {
          switch (key)
          {
          case ecpps::ast::ClassKey::Class: return ecpps::typeSystem::ClassKind::Class;
          case ecpps::ast::ClassKey::Struct: return ecpps::typeSystem::ClassKind::Struct;
          case ecpps::ast::ClassKey::Union: return ecpps::typeSystem::ClassKind::Union;
          }
          return ecpps::typeSystem::ClassKind::Struct;
     }

     ecpps::typeSystem::Access ToAccess(const ecpps::ast::AccessSpecifier spec)
     {
          switch (spec)
          {
          case ecpps::ast::AccessSpecifier::Public: return ecpps::typeSystem::Access::Public;
          case ecpps::ast::AccessSpecifier::Protected: return ecpps::typeSystem::Access::Protected;
          case ecpps::ast::AccessSpecifier::Private: return ecpps::typeSystem::Access::Private;
          }
          return ecpps::typeSystem::Access::Private;
     }

     ecpps::typeSystem::Qualifiers QualifiersFrom(const bool isConst, const bool isVolatile)
     {
          using ecpps::typeSystem::Qualifiers;
          if (isConst && isVolatile) return Qualifiers::ConstVolatile;
          return isConst ? Qualifiers::Const : isVolatile ? Qualifiers::Volatile : Qualifiers::None;
     }

     ecpps::ir::TypeRequest ErrorRequest(std::vector<ecpps::diagnostics::DiagnosticsMessage> diagnostics = {})
     {
          ecpps::ir::TypeRequest request{};
          request.kind = ecpps::ir::TypeKind::Error;
          request.data = ecpps::ir::InvalidRequest(nullptr, std::move(diagnostics));
          return request;
     }
     [[nodiscard]] bool IsValidIntegerSpecifierCombination(const std::vector<std::string_view>& tokens)
     {
          const auto count = [&](const std::string_view keyword)
          {
               return std::ranges::count(tokens, keyword);
          };
          const auto chars = count("char");
          const auto shorts = count("short");
          const auto ints = count("int");
          const auto longs = count("long");
          const auto isSigned = count("signed");
          const auto isUnsigned = count("unsigned");
          if (chars > 1 || shorts > 1 || ints > 1 || isSigned > 1 || isUnsigned > 1 || longs > 2) return false;
          if (isSigned != 0 && isUnsigned != 0) return false;
          if (chars != 0 && (shorts != 0 || ints != 0 || longs != 0)) return false;
          if (shorts != 0 && longs != 0) return false;
          return true;
     }

     enum struct Derivation : std::uint_fast8_t
     {
          None,
          Pointer,
          Reference,
          Array,
          Function
     };

     Derivation LastDerivation(const ecpps::ast::Declarator& declarator)
     {
          if (declarator.nested != nullptr)
               if (const auto inner = LastDerivation(*declarator.nested); inner != Derivation::None) return inner;
          if (!declarator.suffixes.empty())
               return std::holds_alternative<ecpps::ast::ArrayBound>(declarator.suffixes.front())
                           ? Derivation::Array
                           : Derivation::Function;
          if (!declarator.ptrOperators.empty())
               return declarator.ptrOperators.back().kind == ecpps::ast::PtrOperator::Kind::Pointer ||
                                declarator.ptrOperators.back().kind == ecpps::ast::PtrOperator::Kind::PointerToMember
                           ? Derivation::Pointer
                           : Derivation::Reference;
          return Derivation::None;
     }
     const ecpps::ast::IdentifierNode* DeclaratorIdentifier(const ecpps::ast::Declarator& declarator)
     {
          const auto* level = &declarator;
          while (level->nested != nullptr) level = level->nested.get();
          return dynamic_cast<const ecpps::ast::IdentifierNode*>(level->declaredIdExpression.get());
     }

} // namespace

const ecpps::ir::SingleAssignRegisterNode* ecpps::ir::IR::LowerExpressionLoaded(Expression expression,
                                                                                std::vector<NodePointer>& built)
{
     if (expression == nullptr || expression->Value() == nullptr) return nullptr;

     const bool isVariableReference = dynamic_cast<const ReferenceNode*>(expression->Value().get()) != nullptr;
     const auto source = expression->Value()->Source();
     const auto* type = expression->Type();

     const auto* resultReg = LowerExpression(std::move(expression), built);
     if (!isVariableReference || resultReg == nullptr) return resultReg;

     auto* const function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());

     runtime_assert(function != nullptr, "Function was null when parsing the function");

     auto& allocator = *this->GetContext().nodeAllocator;

     const auto idx = function->GetNextRegisterIndex();
     RegisterPriorityInfo info{.regClass = RegisterClass::Temporary};
     auto* loadedResult = new (allocator) SingleAssignRegisterNode(idx, info, type->Width(), type->Alignment(), source);

     built.push_back(std::unique_ptr<SSALoadNode, IRDeleter>{new (allocator) SSALoadNode(
          std::unique_ptr<SingleAssignRegisterNode, IRDeleter>(loadedResult), resultReg, AccessFor(type), source)});
     return loadedResult;
}
const ecpps::ir::SingleAssignRegisterNode* ecpps::ir::IR::LowerExpression(Expression expression,
                                                                          std::vector<IRNodePointer>& built)
{
     if (expression == nullptr) return nullptr;

     auto& allocator = *this->GetContext().nodeAllocator;
     auto* const functionCtx = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(functionCtx != nullptr, "LowerExpression called outside of a function context");

     if (functionCtx == nullptr) return nullptr;

     auto makeReg = [&](Location source, std::size_t width,
                        std::size_t align) -> std::unique_ptr<SingleAssignRegisterNode, IRDeleter>
     {
          const auto idx = functionCtx->GetNextRegisterIndex();
          RegisterPriorityInfo info{.regClass = RegisterClass::Temporary};
          return std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{
               new (allocator) SingleAssignRegisterNode(idx, info, width, align, source)};
     };

     NodeBase* valueNode = expression->Value().get();
     const Location source = valueNode->Source();

     struct StorageSlot
     {
          const SingleAssignRegisterNode* reg{};
          bool indirect{};
     };

     const auto lowerSlot = [&](Expression lvalue) -> StorageSlot
     {
          if (lvalue == nullptr) return {};
          if (auto* const dereference = dynamic_cast<high::DereferenceNode*>(lvalue->Value().get()))
          {
               const auto* pointerReg = LowerExpressionLoaded(std::move(*dereference).Operand(), built);
               return StorageSlot{.reg = pointerReg, .indirect = true};
          }
          const auto* objectReg = LowerExpression(std::move(lvalue), built);
          return StorageSlot{.reg = objectReg, .indirect = false};
     };

     const auto loadSlot = [&](const StorageSlot& slot) -> const SingleAssignRegisterNode*
     {
          auto loaded = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* loadedPtr = loaded.get();
          if (slot.indirect)
               built.push_back(std::unique_ptr<SSADerefNode, IRDeleter>{
                    new (allocator) SSADerefNode(std::move(loaded), slot.reg, source)});
          else
               built.push_back(std::unique_ptr<SSALoadNode, IRDeleter>{
                    new (allocator) SSALoadNode(std::move(loaded), slot.reg, AccessFor(expression->Type()), source)});
          return loadedPtr;
     };

     const auto storeSlot = [&](const StorageSlot& slot, const SingleAssignRegisterNode* value)
     {
          if (slot.indirect)
               built.push_back(std::unique_ptr<SSAIndirectStoreNode, IRDeleter>{
                    new (allocator) SSAIndirectStoreNode(slot.reg, value, source)});
          else
               built.push_back(
                    std::unique_ptr<SSAStoreNode, IRDeleter>{new (allocator) SSAStoreNode(slot.reg, value, source)});
     };

     if (auto* const integralNode = dynamic_cast<IntegralNode*>(valueNode))
     {
          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSAImmNode, IRDeleter>{
               new (allocator) SSAImmNode(std::move(result), integralNode->Value(), source)});
          return resultPtr;
     }

     if (auto* const loadNode = dynamic_cast<LoadNode*>(valueNode))
     {
          const std::string& varName = loadNode->Address();
          const SingleAssignRegisterNode* allocReg = ResolveAllocReg(varName);
          if (allocReg == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot resolve allocation register for variable '" + varName + "'", source));
               return nullptr;
          }

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSALoadNode, IRDeleter>{
               new (allocator) SSALoadNode(std::move(result), allocReg, AccessFor(expression->Type()), source)});
          return resultPtr;
     }
     if (auto* const referenceNode = dynamic_cast<ReferenceNode*>(valueNode))
     {
          const std::string& varName = referenceNode->Address();
          const SingleAssignRegisterNode* allocReg = ResolveAllocReg(varName);
          if (allocReg == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot resolve allocation register for variable '" + varName + "'", source));
          }

          return allocReg;
     }

     if (auto* const regRef = dynamic_cast<SSARegisterReferenceNode*>(valueNode))
     {
          return &regRef->Reg();
     }

     if (auto* const addNode = dynamic_cast<high::AdditionNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*addNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*addNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSAAddNode, IRDeleter>{
               new (allocator) SSAAddNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }

     if (auto* const subNode = dynamic_cast<high::SubtractionNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*subNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*subNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSASubNode, IRDeleter>{
               new (allocator) SSASubNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }

     if (auto* const mulNode = dynamic_cast<high::MultiplicationNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*mulNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*mulNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSAMulNode, IRDeleter>{
               new (allocator) SSAMulNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }

     if (auto* const leftShiftNode = dynamic_cast<high::LeftShiftNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*leftShiftNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*leftShiftNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSALeftShiftNode, IRDeleter>{
               new (allocator) SSALeftShiftNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }

     if (auto* const rightShiftNode = dynamic_cast<high::RightShiftNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*rightShiftNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*rightShiftNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSARightShiftNode, IRDeleter>{
               new (allocator) SSARightShiftNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }

     if (auto* const divNode = dynamic_cast<high::DivideNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*divNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*divNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSADivNode, IRDeleter>{
               new (allocator) SSADivNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }

     if (auto* const orNode = dynamic_cast<high::BinaryOrNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*orNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*orNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSABinOrNode, IRDeleter>{
               new (allocator) SSABinOrNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }
     if (auto* const andNode = dynamic_cast<high::BinaryAndNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*andNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*andNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSABinAndNode, IRDeleter>{
               new (allocator) SSABinAndNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }
     if (auto* const xorNode = dynamic_cast<high::BinaryXorNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*xorNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*xorNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSABinXorNode, IRDeleter>{
               new (allocator) SSABinXorNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }

     if (auto* const modNode = dynamic_cast<high::ModuloNode*>(valueNode))
     {
          const auto* leftReg = LowerExpressionLoaded(std::move(*modNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*modNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSAModNode, IRDeleter>{
               new (allocator) SSAModNode(std::move(result), leftReg, rightReg, source)});
          return resultPtr;
     }

     if (auto* const convertNode = dynamic_cast<high::ConvertNode*>(valueNode))
     {
          const auto* targetType = convertNode->TargetType();
          const auto* fromType = convertNode->Operand()->Type();

          const auto* srcReg = LowerExpressionLoaded(std::move(*convertNode).Operand(), built);
          if (srcReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          ConversionType type{};

          const auto* targetIntegral = targetType->CastTo<typeSystem::IntegralType>();
          const bool targetSign = targetIntegral->Sign() == typeSystem::Signedness::Signed;
          const auto targetSize = targetIntegral->Size();

          const auto* fromIntegral = fromType->CastTo<typeSystem::IntegralType>();
          const bool fromSigned = fromIntegral->Sign() == typeSystem::Signedness::Signed;
          const auto fromSize = fromIntegral->Size();

          if (fromSigned == targetSign)
          {
               if (targetSize == fromSize) type = ConversionType::Reinterpret; // warn?
               else if (targetSize > fromSize)
                    type = targetSign ? ConversionType::SignExtension : ConversionType::ZeroExtension;
               else
                    type = ConversionType::Truncate;
          }
          else
          {
               if (targetSize == fromSize) type = ConversionType::Reinterpret;
               else if (targetSize > fromSize)
                    type = targetSign ? ConversionType::SignExtendAndReinterpret
                                      : ConversionType::ZeroExtendAndReinterpret;
               else
                    type = ConversionType::Truncate;
          }

          built.push_back(std::unique_ptr<SSAConvertNode, IRDeleter>{
               new (allocator) SSAConvertNode(std::move(result), srcReg, type, source)});
          return resultPtr;
     }

     if (auto* const ptrConvNode = dynamic_cast<high::PointerConversionNode*>(valueNode))
     {
          if (ptrConvNode->TargetType() == ptrConvNode->Operand()->Type())
               return LowerExpressionLoaded(std::move(*ptrConvNode).Operand(), built);

          const auto* srcReg = LowerExpression(std::move(*ptrConvNode).Operand(), built);
          if (srcReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSAPointerConvertNode, IRDeleter>{
               new (allocator) SSAPointerConvertNode(std::move(result), srcReg, expression->Type(), source)});
          return resultPtr;
     }

     if ([[maybe_unused]] auto* const decayNode = dynamic_cast<TemporaryIntegerArrayDecayNode*>(valueNode))
     {
          auto ownedDecay = std::move(*expression).Value();
          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSAPointerConvertFromDecayNode, IRDeleter>{
               new (allocator) SSAPointerConvertFromDecayNode(std::move(result), std::move(ownedDecay),
                                                              expression->Type(), source)});
          return resultPtr;
     }

     if ([[maybe_unused]] auto* const loadDecayNode = dynamic_cast<LoadArrayDecayNode*>(valueNode))
     {
          auto ownedDecay = std::move(*expression).Value();
          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSAPointerConvertFromDecayNode, IRDeleter>{
               new (allocator) SSAPointerConvertFromDecayNode(std::move(result), std::move(ownedDecay),
                                                              expression->Type(), source)});
          return resultPtr;
     }

     if (auto* const addrOfNode = dynamic_cast<high::AddressOfNode*>(valueNode))
     {
          auto operand = std::move(*addrOfNode).Operand();
          if (operand == nullptr) return nullptr;
          NodeBase* const operandNode = operand->Value().get();

          if (auto* const inner = dynamic_cast<high::DereferenceNode*>(operandNode))
               return LowerExpressionLoaded(std::move(*inner).Operand(), built);

          if (dynamic_cast<ReferenceNode*>(operandNode) == nullptr &&
              dynamic_cast<SSARegisterReferenceNode*>(operandNode) == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot take the address of this expression yet", source));
               return nullptr;
          }

          const auto* objectReg = LowerExpression(std::move(operand), built);
          if (objectReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSAAddressOfNode, IRDeleter>{
               new (allocator) SSAAddressOfNode(std::move(result), objectReg, source)});
          return resultPtr;
     }

     if (auto* const derefNode = dynamic_cast<high::DereferenceNode*>(valueNode))
     {
          const auto* ptrReg = LowerExpressionLoaded(std::move(*derefNode).Operand(), built);
          if (ptrReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSADerefNode, IRDeleter>{
               new (allocator) SSADerefNode(std::move(result), ptrReg, source)});
          return resultPtr;
     }

     if (auto* const callNode = dynamic_cast<FunctionCallNode*>(valueNode))
     {
          std::vector<const SingleAssignRegisterNode*> argRegs;
          argRegs.reserve(callNode->Arguments().size());
          for (auto& argExpr : callNode->MoveArguments())
          {
               const auto* reg = LowerExpressionLoaded(std::move(argExpr), built);
               if (reg == nullptr) return nullptr;
               argRegs.push_back(reg);
          }

          const bool hasResult = expression->Type() != nullptr && !typeSystem::g_void->CommonWith(expression->Type());
          if (hasResult)
          {
               auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
               auto* resultPtr = result.get();
               built.push_back(std::unique_ptr<SSACallNode, IRDeleter>{
                    new (allocator) SSACallNode(std::optional<SSAPointer>{std::move(result)},
                                                callNode->Function().get(), std::move(argRegs), source)});
               return resultPtr;
          }

          built.push_back(std::unique_ptr<SSACallNode, IRDeleter>{
               new (allocator) SSACallNode(std::nullopt, callNode->Function().get(), std::move(argRegs), source)});
          return nullptr;
     }

     if (auto* const addAssign = dynamic_cast<high::AdditionAssignNode*>(valueNode))
     {
          const auto slot = lowerSlot(std::move(*addAssign).Left());
          const auto* rhsReg = LowerExpressionLoaded(std::move(*addAssign).Right(), built);
          if (slot.reg == nullptr || rhsReg == nullptr) return nullptr;

          const auto* current = loadSlot(slot);

          auto addResult = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* addPtr = addResult.get();
          built.push_back(std::unique_ptr<SSAAddNode, IRDeleter>{
               new (allocator) SSAAddNode(std::move(addResult), current, rhsReg, source)});

          storeSlot(slot, addPtr);

          return addPtr; // pre-increment returns the new value
     }

     if (auto* const subAssign = dynamic_cast<high::SubtractionAssignNode*>(valueNode))
     {
          const auto slot = lowerSlot(std::move(*subAssign).Left());
          const auto* rhsReg = LowerExpressionLoaded(std::move(*subAssign).Right(), built);
          if (slot.reg == nullptr || rhsReg == nullptr) return nullptr;

          const auto* current = loadSlot(slot);

          auto subResult = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* subPtr = subResult.get();
          built.push_back(std::unique_ptr<SSASubNode, IRDeleter>{
               new (allocator) SSASubNode(std::move(subResult), current, rhsReg, source)});

          storeSlot(slot, subPtr);

          return subPtr;
     }

     if (auto* const assign = dynamic_cast<high::AssignNode*>(valueNode))
     {
          const auto slot = lowerSlot(std::move(*assign).Left());
          const auto* rhsReg = LowerExpressionLoaded(std::move(*assign).Right(), built);
          if (slot.reg == nullptr || rhsReg == nullptr) return nullptr;

          storeSlot(slot, rhsReg);
          return slot.indirect ? rhsReg : slot.reg;
     }

     if (auto* const postInc = dynamic_cast<high::PostIncrementNode*>(valueNode))
     {
          const auto slot = lowerSlot(std::move(*postInc).Operand());
          if (slot.reg == nullptr) return nullptr;

          const auto* oldPtr = loadSlot(slot);

          auto oneResult = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* onePtr = oneResult.get();
          built.push_back(std::unique_ptr<SSAImmNode, IRDeleter>{
               new (allocator) SSAImmNode(std::move(oneResult), postInc->IncrementValue(), source)});

          auto newResult = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* newPtr = newResult.get();
          built.push_back(std::unique_ptr<SSAAddNode, IRDeleter>{
               new (allocator) SSAAddNode(std::move(newResult), oldPtr, onePtr, source)});

          storeSlot(slot, newPtr);

          return oldPtr;
     }

     if (auto* const postDec = dynamic_cast<high::PostDecrementNode*>(valueNode))
     {
          const auto slot = lowerSlot(std::move(*postDec).Operand());
          if (slot.reg == nullptr) return nullptr;

          const auto* oldPtr = loadSlot(slot);

          auto oneResult = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* onePtr = oneResult.get();
          built.push_back(std::unique_ptr<SSAImmNode, IRDeleter>{
               new (allocator) SSAImmNode(std::move(oneResult), postDec->IncrementValue(), source)});

          auto newResult = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* newPtr = newResult.get();
          built.push_back(std::unique_ptr<SSASubNode, IRDeleter>{
               new (allocator) SSASubNode(std::move(newResult), oldPtr, onePtr, source)});

          storeSlot(slot, newPtr);

          return oldPtr;
     }
     if (auto* const bitwiseComplement = dynamic_cast<high::BitwiseNotNode*>(valueNode))
     {
          const auto* operandReg = LowerExpressionLoaded(std::move(*bitwiseComplement).Operand(), built);
          if (operandReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSABitwiseNotNode, IRDeleter>{
               new (allocator) SSABitwiseNotNode(std::move(result), operandReg, source)});
          return resultPtr;
     }
     if (auto* const arithmeticNegation = dynamic_cast<high::ArithmeticNegationNode*>(valueNode))
     {
          const auto* operandReg = LowerExpressionLoaded(std::move(*arithmeticNegation).Operand(), built);
          if (operandReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSAArithmeticNegationNode, IRDeleter>{
               new (allocator) SSAArithmeticNegationNode(std::move(result), operandReg, source)});
          return resultPtr;
     }

     if ([[maybe_unused]] auto* const paramNode = dynamic_cast<ParameterNode*>(valueNode))
     {
          const std::uint64_t idx = paramNode->Index();
          const auto* paramReg = functionCtx->GetParameterRegister(idx);
          runtime_assert(paramReg != nullptr, "Parameter register not found");
          return paramReg;
     }

     if (auto* const compareNode = dynamic_cast<high::CompareNode*>(valueNode))
     {
          const auto predicate = compareNode->Predicate();
          const auto* leftReg = LowerExpressionLoaded(std::move(*compareNode).Left(), built);
          const auto* rightReg = LowerExpressionLoaded(std::move(*compareNode).Right(), built);
          if (leftReg == nullptr || rightReg == nullptr) return nullptr;

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSACompareNode, IRDeleter>{
               new (allocator) SSACompareNode(std::move(result), leftReg, rightReg, predicate, source)});
          return resultPtr;
     }

     if ([[maybe_unused]] auto* const thisExpression = dynamic_cast<high::ThisExpressionNode*>(valueNode))
     {
          if (!this->_objectInitialisationStack.empty() && this->_objectInitialisationStack.back().address != nullptr)
               return this->_objectInitialisationStack.back().address;

          const auto* thisAlloc = functionCtx->GetThisRegister();
          if (thisAlloc == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "`this` used outside of a non-static member function", source));
               return nullptr;
          }

          auto result = makeReg(source, expression->Type()->Width(), expression->Type()->Alignment());
          auto* resultPtr = result.get();
          built.push_back(std::unique_ptr<SSALoadNode, IRDeleter>{
               new (allocator) SSALoadNode(std::move(result), thisAlloc, AccessFor(expression->Type()), source)});
          return resultPtr;
     }

     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "Cannot lower expression to SSA: unsupported node kind", source));
     return nullptr;
}

const ecpps::ir::SingleAssignRegisterNode* ecpps::ir::IR::ResolveAllocReg(const std::string& name) const
{
     for (const auto& context : this->GetContext().contextSequence)
     {
          if (const auto* funcCtx = dynamic_cast<const FunctionContext*>(context.get()))
          {
               if (const auto* reg = funcCtx->GetAllocRegForName(name)) return reg;
          }
     }
     return nullptr;
}
std::vector<IRNodePointer> ecpps::ir::IR::Parse(Context& context, const std::vector<ASTNodePointer>& ast)
{
     IR ir{&context};
     TypeRequest voidRequest{.kind = TypeKind::Fundamental, .data = VoidRequest{}};
     const auto* voidType = GetTypeContext().Get(voidRequest);
     ir.GetContext().globalScope->types.insert(voidType);
     TypeRequest charRequest{.kind = TypeKind::Fundamental,
                             .data = StandardSignedIntegerRequest{.size = typeSystem::TypeKind::Int,
                                                                  .signedness = typeSystem::Signedness::Signed,
                                                                  .isCharWithoutSign = true}};
     const auto* charType = GetTypeContext().Get(charRequest);
     ir.GetContext().globalScope->types.insert(charType);
     TypeRequest signedCharRequest{.kind = TypeKind::Fundamental,
                                   .data = StandardSignedIntegerRequest{.size = typeSystem::TypeKind::Char,
                                                                        .signedness = typeSystem::Signedness::Signed,
                                                                        .isCharWithoutSign = false}};
     const auto* signedCharType = GetTypeContext().Get(signedCharRequest);
     ir.GetContext().globalScope->types.insert(signedCharType);
     TypeRequest unsignedCharRequest{.kind = TypeKind::Fundamental,
                                     .data =
                                          StandardSignedIntegerRequest{.size = typeSystem::TypeKind::Char,
                                                                       .signedness = typeSystem::Signedness::Unsigned,
                                                                       .isCharWithoutSign = false}};
     const auto* unsignedCharType = GetTypeContext().Get(unsignedCharRequest);
     ir.GetContext().globalScope->types.insert(unsignedCharType);
     TypeRequest shortRequest{.kind = TypeKind::Fundamental,
                              .data = StandardSignedIntegerRequest{.size = typeSystem::TypeKind::Short,
                                                                   .signedness = typeSystem::Signedness::Signed,
                                                                   .isCharWithoutSign = false}};
     const auto* shortType = GetTypeContext().Get(shortRequest);
     ir.GetContext().globalScope->types.insert(shortType);
     TypeRequest intRequest{.kind = TypeKind::Fundamental,
                            .data = StandardSignedIntegerRequest{.size = typeSystem::TypeKind::Int,
                                                                 .signedness = typeSystem::Signedness::Signed,
                                                                 .isCharWithoutSign = false}};
     const auto* intType = GetTypeContext().Get(intRequest);
     ir.GetContext().globalScope->types.insert(intType);
     TypeRequest longRequest{.kind = TypeKind::Fundamental,
                             .data = StandardSignedIntegerRequest{.size = typeSystem::TypeKind::Long,
                                                                  .signedness = typeSystem::Signedness::Signed,
                                                                  .isCharWithoutSign = false}};
     const auto* longType = GetTypeContext().Get(longRequest);
     ir.GetContext().globalScope->types.insert(longType);
     TypeRequest longLongRequest{.kind = TypeKind::Fundamental,
                                 .data = StandardSignedIntegerRequest{.size = typeSystem::TypeKind::LongLong,
                                                                      .signedness = typeSystem::Signedness::Signed,
                                                                      .isCharWithoutSign = false}};
     const auto* longLongType = GetTypeContext().Get(longLongRequest);
     ir.GetContext().globalScope->types.insert(longLongType);
     TypeRequest unsignedShortRequest{.kind = TypeKind::Fundamental,
                                      .data =
                                           StandardSignedIntegerRequest{.size = typeSystem::TypeKind::Short,
                                                                        .signedness = typeSystem::Signedness::Unsigned,
                                                                        .isCharWithoutSign = false}};
     const auto* unsignedShortType = GetTypeContext().Get(unsignedShortRequest);
     ir.GetContext().globalScope->types.insert(unsignedShortType);
     TypeRequest unsignedIntRequest{.kind = TypeKind::Fundamental,
                                    .data = StandardSignedIntegerRequest{.size = typeSystem::TypeKind::Int,
                                                                         .signedness = typeSystem::Signedness::Unsigned,
                                                                         .isCharWithoutSign = false}};
     const auto* unsignedIntType = GetTypeContext().Get(unsignedIntRequest);
     ir.GetContext().globalScope->types.insert(unsignedIntType);
     TypeRequest unsignedLongRequest{.kind = TypeKind::Fundamental,
                                     .data =
                                          StandardSignedIntegerRequest{.size = typeSystem::TypeKind::Long,
                                                                       .signedness = typeSystem::Signedness::Unsigned,
                                                                       .isCharWithoutSign = false}};
     const auto* unsignedLongType = GetTypeContext().Get(unsignedLongRequest);
     ir.GetContext().globalScope->types.insert(unsignedLongType);
     TypeRequest unsignedLongLongRequest{
          .kind = TypeKind::Fundamental,
          .data = StandardSignedIntegerRequest{.size = typeSystem::TypeKind::LongLong,
                                               .signedness = typeSystem::Signedness::Unsigned,
                                               .isCharWithoutSign = false}};
     const auto* unsignedLongLongType = GetTypeContext().Get(unsignedLongLongRequest);
     ir.GetContext().globalScope->types.insert(unsignedLongLongType);
     TypeRequest boolRequest{.kind = TypeKind::Fundamental, .data = BooleanRequest{}};
     const auto* boolType = GetTypeContext().Get(boolRequest);
     ir.GetContext().globalScope->types.insert(boolType);
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

std::optional<std::uint64_t> ecpps::ir::IR::InferArrayLength(const ast::NodePointer& initialiser,
                                                             typeSystem::NonowningTypePointer elementType,
                                                             const std::string& name, const Location& source)
{
     if (initialiser == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Unbounded array '{}' must have an initialiser", name), source));
          return std::nullopt;
     }

     if (const auto* const stringInitialiser = dynamic_cast<const ast::StringLiteralNode*>(initialiser.get());
         stringInitialiser != nullptr && IsEligibleForStringLiteralInitialisation(elementType))
     {
          return static_cast<std::uint64_t>(stringInitialiser->Value().length() + 1);
     }

     if (const auto* const listInitialiser = dynamic_cast<const ast::InitialiserListNode*>(initialiser.get());
         listInitialiser != nullptr)
     {
          std::uint64_t count{};
          for ([[maybe_unused]] const auto& element : listInitialiser->Initialisers()) ++count;

          if (count == 0)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Cannot deduce the bound of array '{}' from an empty initialiser list", name),
                         initialiser->Source()));
               return std::nullopt;
          }
          return count;
     }

     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               std::format("Array '{}' must be initialised with initialiser-lists", name), initialiser->Source()));
     return std::nullopt;
}

Expression ecpps::ir::IR::ParseArrayInitialiser(const ast::NodePointer& initialiser,
                                                typeSystem::NonowningTypePointer arrayTypeBase, const std::string& name)
{
     const auto* arrayType = arrayTypeBase->CastTo<typeSystem::ArrayType>();
     runtime_assert(arrayType != nullptr, "Expected an array type for array initialisers");

     if (const auto* const stringInitialiser = dynamic_cast<const ast::StringLiteralNode*>(initialiser.get());
         stringInitialiser != nullptr && IsEligibleForStringLiteralInitialisation(arrayType->ElementType()))
     {
          auto& allocator = *this->GetContext().nodeAllocator;
          const auto& string = stringInitialiser->Value();
          const auto stringLength = string.length() + 1;
          const auto* elementType = arrayType->ElementType()->CastTo<typeSystem::IntegralType>();
          runtime_assert(elementType != nullptr, "Expected a character type for string-literal initialisation");

          if (stringLength > arrayType->ElementCount())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Cannot initialise an array with more elements than it can "
                                     "hold. Provided {} elements for an array of `{}` `{}`s",
                                     stringLength, arrayType->ElementCount(), arrayType->ElementType()->RawName()),
                         initialiser->Source()));
               return nullptr;
          }

          std::vector<std::uint32_t> arrayValues{};
          arrayValues.reserve(arrayType->ElementCount());
          for (const auto character : string) arrayValues.emplace_back(character);
          arrayValues.emplace_back(0u);
          arrayValues.resize(arrayType->ElementCount(), 0u); // the rest is zero-initialised

          std::unique_ptr<IntegerArrayNode, IRDeleter> arrayNode{
               new (allocator) IntegerArrayNode(std::move(arrayValues), elementType, initialiser->Source())};
          return std::make_unique<PRValue>(arrayTypeBase, std::move(arrayNode), true);
     }

     if (dynamic_cast<const ast::InitialiserListNode*>(initialiser.get()) != nullptr)
          return ParseListInitialisation(initialiser, arrayTypeBase);

     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               std::format("Array '{}' must be initialised with initialiser-lists", name), initialiser->Source()));
     return nullptr;
}

void ecpps::ir::IR::ParseVariableDeclaration(const ast::VariableDeclarationNode& node)
{
     auto& allocator = *this->GetContext().nodeAllocator;

     if (node.GetFlags().isTypedef)
     {
          const auto* declaredType = ParseType(node.Type());
          if (declaredType == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Unknown type {} in typedef declaration", node.Type()->ToString(0)),
                         node.Source()));
               return;
          }

          auto& currentScope = this->GetContext().contextSequence.back()->GetScope();
          for (const auto& decl : node.Declarators())
          {
               const auto* idNodePtr = decl.name.get();
               const auto* idExpr = dynamic_cast<const ast::IdentifierNode*>(idNodePtr);
               if (idExpr == nullptr)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                              "Expected identifier in typedef declarator", decl.name->Source()));
                    continue;
               }

               const std::string typedefName = idExpr->Value();
               currentScope.typeAliases[typedefName] = declaredType;
          }

          return;
     }

     auto* const functionContext = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(functionContext != nullptr, "Variable declaration outside of a function is not supported");

     auto& fscope = functionContext->GetScope<FunctionScope>(); // NOLINT(clang-analyzer-core.CallAndMessage)

     const auto* declaredType = ParseType(node.Type());
     if (declaredType == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                         "Expected identifier in variable declarator", decl.name->Source()));
               continue;
          }

          const std::string varName = idExpr->Value();
          const auto* variableType = declaredType;
          bool inferLastArrayFromInitialiser = false;
          bool arrayBoundsFailed = false;
          const auto isArray = !decl.arrayLevels.empty();

          std::vector<const ast::NodePointer*> arrayLevels{};
          arrayLevels.reserve(decl.arrayLevels.size());
          for (const auto& arrayLevel : decl.arrayLevels) arrayLevels.push_back(&arrayLevel);

          for (std::size_t levelIndex = arrayLevels.size(); levelIndex-- > 0;)
          {
               const auto& arrayLevel = *arrayLevels[levelIndex];

               if (arrayLevel == nullptr)
               {
                    if (levelIndex != 0)
                    {
                         this->GetContext().diagnostics.get().diagnosticsList.push_back(
                              std::make_unique<diagnostics::TypeError>(
                                   std::format("Declaration of '{}' introduces an array of unbounded arrays which is "
                                               "not allowed, only the top-level array might be unbounded",
                                               varName),
                                   node.Source()));
                         arrayBoundsFailed = true;
                         break;
                    }
                    inferLastArrayFromInitialiser = true;
                    continue;
               }

               const auto arrayLevelExpression = ParseExpression(arrayLevel);
               if (arrayLevelExpression == nullptr)
               {
                    arrayBoundsFailed = true;
                    break;
               }

               auto constexprArraySize =
                    arrayLevelExpression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
               if (!constexprArraySize.has_value())
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::TypeError>(
                              std::format("Arrays bounds must be defined by a constant expression", varName),
                              arrayLevel->Source()));

                    auto& nestedDiagnostics = constexprArraySize.error();

                    while (!nestedDiagnostics.empty())
                    {
                         this->GetContext().diagnostics.get().diagnosticsList.push_back(
                              std::move(nestedDiagnostics.top()));
                         nestedDiagnostics.pop();
                    }
                    arrayBoundsFailed = true;
                    break;
               }
               const auto& arraySize = *constexprArraySize;
               if (!std::holds_alternative<std::uint64_t>(arraySize.variant))
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::TypeError>(
                              std::format("Arrays bounds must be defined by an integer", varName),
                              arrayLevel->Source()));
                    arrayBoundsFailed = true;
                    break;
               }
               const auto length = std::get<std::uint64_t>(arraySize.variant);

               TypeRequest arrayRequest{};
               arrayRequest.kind = TypeKind::Compound;
               arrayRequest.data = BoundedArrayRequest{.elementType = variableType, .size = length};

               variableType = GetTypeContext().Get(arrayRequest);
          }

          if (arrayBoundsFailed) continue;

          if (inferLastArrayFromInitialiser)
          {
               const auto inferredLength =
                    InferArrayLength(decl.initialiser, variableType, varName, decl.name->Source());
               if (!inferredLength.has_value()) continue;

               TypeRequest arrayRequest{};
               arrayRequest.kind = TypeKind::Compound;
               arrayRequest.data = BoundedArrayRequest{.elementType = variableType, .size = *inferredLength};
               if (!RequireComplete(variableType, decl.name->Source(), "an array element type"))
               {
                    arrayBoundsFailed = true;
                    break;
               }
               variableType = GetTypeContext().Get(arrayRequest);
          }

          bool duplicate = false;
          for (const auto& v : fscope.Locals())
          {
               if (v.Name() == varName)
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Redefinition of variable '" + varName + "'", decl.name->Source()));
               continue;
          }
          if (!RequireComplete(variableType, decl.name->Source(), "an object definition")) continue;

          auto* const function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
          runtime_assert(function != nullptr, "Function was null when parsing the function");

          Variable varEntry{varName, variableType, StorageDuration::Automatic};
          const auto ssaIndex = function->GetNextRegisterIndex();

          fscope.Locals().emplace_back(FunctionScope::LocalEntity{std::move(varEntry)});
          RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
          auto ssaNodeUnique =
               std::unique_ptr<ir::SingleAssignRegisterNode, IRDeleter>{new (allocator) ir::SingleAssignRegisterNode(
                    ssaIndex, info, variableType->Width(), variableType->Alignment(), decl.name->Source())};
          auto* ssaNode = ssaNodeUnique.get();

          function->RegisterAllocReg(varName, ssaNode);

          this->_built.push_back(std::unique_ptr<ir::AllocationNode, IRDeleter>{
               new (allocator) ir::AllocationNode(std::move(ssaNodeUnique), decl.name->Source())});

          const auto arrayContainsClass = [](typeSystem::NonowningTypePointer type)
          {
               while (true)
               {
                    const auto* arrayType = type->CastTo<typeSystem::ArrayType>();
                    if (arrayType == nullptr) break;
                    type = arrayType->ElementType();
               }
               return typeSystem::IsClass(type);
          };
          const auto diagnoseConstScalarDefaultInitialisation = [&](typeSystem::NonowningTypePointer type)
          {
               const auto* qualifiedType = type->CastTo<typeSystem::QualifiedType>();
               if (qualifiedType == nullptr || !qualifiedType->IsConst()) return;
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A const-qualified scalar object requires an initializer during default-initialization",
                         decl.name->Source()));
          };

          if (typeSystem::IsClass(variableType))
          {
               const auto* objectAddress = AddressOfAllocation(ssaNode, variableType, decl.name->Source());
               if (decl.initialiser == nullptr)
                    InitialiseAtAddress(variableType, objectAddress, nullptr, ast::InitialisationType::Default,
                                        ObjectInitialisationMode::Default, decl.name->Source());
               else
                    InitialiseAtAddress(variableType, objectAddress, &decl.initialiser, decl.initialiserType,
                                        ObjectInitialisationMode::Explicit, decl.initialiser->Source());
               continue;
          }

          if (variableType->CastTo<typeSystem::ReferenceType>() != nullptr)
          {
               if (decl.initialiser == nullptr)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "A reference variable requires an initializer", decl.name->Source()));
                    continue;
               }
               const auto* referenceStorage = AddressOfAllocation(ssaNode, variableType, decl.name->Source());
               InitialiseReferenceAtAddress(variableType, referenceStorage, &decl.initialiser, decl.initialiserType,
                                            decl.initialiser->Source());
               continue;
          }

          if (decl.initialiser == nullptr)
          {
               if (isArray && arrayContainsClass(variableType))
               {
                    const auto* objectAddress = AddressOfAllocation(ssaNode, variableType, decl.name->Source());
                    InitialiseAtAddress(variableType, objectAddress, nullptr, ast::InitialisationType::Default,
                                        ObjectInitialisationMode::Default, decl.name->Source());
               }
               else if (isArray)
               {
                    auto elementType = variableType;
                    while (const auto* arrayType = elementType->CastTo<typeSystem::ArrayType>())
                         elementType = arrayType->ElementType();
                    diagnoseConstScalarDefaultInitialisation(elementType);
               }
               else
                    diagnoseConstScalarDefaultInitialisation(variableType);
               continue;
          }

          if (isArray)
          {
               if (arrayContainsClass(variableType))
               {
                    const auto* objectAddress = AddressOfAllocation(ssaNode, variableType, decl.name->Source());
                    if (dynamic_cast<const ast::InitialiserListNode*>(decl.initialiser.get()) != nullptr)
                         InitialiseAtAddress(variableType, objectAddress, &decl.initialiser, decl.initialiserType,
                                             ObjectInitialisationMode::Explicit, decl.initialiser->Source());
                    else
                         this->GetContext().diagnostics.get().diagnosticsList.push_back(
                              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                   "An array of class objects requires a braced initializer",
                                   decl.initialiser->Source()));
                    continue;
               }
               // Arrays are initialised with a single aggregate store. Elements without an initialiser are
               // value-initialised (zero), which is part of the aggregate that is stored.
               auto arrayInitialiser = ParseArrayInitialiser(decl.initialiser, variableType, varName);
               if (arrayInitialiser == nullptr) continue;

               auto ownedArrayNode = std::move(*arrayInitialiser).Value();
               this->_built.push_back(std::unique_ptr<ir::SSAArrayStoreNode, IRDeleter>{new (
                    allocator) ir::SSAArrayStoreNode(ssaNode, std::move(ownedArrayNode), decl.initialiser->Source())});
               continue;
          }

          auto initExpr = PickInitialiser(decl.initialiser, variableType, decl.initialiserType);
          if (initExpr == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Invalid initialiser for variable '" + varName + "'", decl.initialiser->Source()));
               continue;
          }

          if (const auto* integralInitExpr = dynamic_cast<IntegralNode*>(initExpr->Value().get());
              integralInitExpr != nullptr)
          {
               this->_built.push_back(std::unique_ptr<ir::SSAStoreIntegerNode, IRDeleter>{
                    new (allocator)
                         ir::SSAStoreIntegerNode(ssaNode, integralInitExpr->Value(), decl.initialiser->Source())});
          }
          else
          {
               // The initialiser is read as a VALUE (lvalue-to-rvalue), e.g. `int* p = otherP;` loads otherP.
               const auto* initReg = LowerExpressionLoaded(std::move(initExpr), this->_built);
               if (initReg != nullptr)
               {
                    this->_built.push_back(std::unique_ptr<ir::SSAStoreNode, IRDeleter>{
                         new (allocator) ir::SSAStoreNode(ssaNode, initReg, decl.initialiser->Source())});
               }
          }
     }
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

static std::uint64_t ConvertConstant(std::uint64_t value, const ecpps::typeSystem::IntegralType* from,
                                     const ecpps::typeSystem::IntegralType* to)
{
     const auto fromBits = static_cast<unsigned>(from->Size() * 8); // assumes Size() is bytes
     if (fromBits < 64)
     {
          const std::uint64_t mask = (1ull << fromBits) - 1;
          value &= mask;
          if (from->Sign() == ecpps::typeSystem::Signedness::Signed && ((value >> (fromBits - 1)) & 1)) value |= ~mask;
     }
     const auto toBits = static_cast<unsigned>(to->Size() * 8);
     return toBits < 64 ? value & ((1ull << toBits) - 1) : value;
}
Expression ecpps::ir::IR::ParseAdditiveExpression(Expression left, ast::Operator operator_, Expression right,
                                                  const Location& source) const
{
     runtime_assert(operator_ == ast::Operator::Plus || operator_ == ast::Operator::Minus, "Invalid additive operator");

     const auto* leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     const auto* rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

     const auto* leftPointer = left->Type()->CastTo<typeSystem::PointerType>();
     const auto* rightPointer = right->Type()->CastTo<typeSystem::PointerType>();

     if ((leftPointer != nullptr && rightIntegral != nullptr) || (leftIntegral != nullptr && rightPointer != nullptr))
     {
          const bool isPlus = operator_ == ast::Operator::Plus;
          if (leftIntegral != nullptr)
          {
               if (!isPlus)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
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

          const auto* pointee = leftPointer->BaseType();
          if (!pointee->IsComplete() || pointee->Size() == 0)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot perform pointer arithmetic on a pointer to a void or incomplete type", source));
               return nullptr;
          }
          const auto elementSize = pointee->Size();

          if (elementSize == 0)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot perform pointer arithmetic on a pointer to a void or incomplete type", source));
               return nullptr;
          }

          TypeRequest ptrDiffRequest{};
          ptrDiffRequest.kind = TypeKind::Fundamental;
          ptrDiffRequest.data = PlatformIntegerRequest{.kind = PlatformIntegerKind::PtrDiff};
          const auto* ptrDiffType = GetTypeContext().Get(ptrDiffRequest)->CastTo<typeSystem::IntegralType>();
          runtime_assert(ptrDiffType != nullptr, "ptrdiff_t must be an integral type");

          auto offset = std::move(right);
          const auto constant = offset->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
          const auto* offsetIntegral = offset->Type()->CastTo<typeSystem::IntegralType>();
          if (constant.has_value() && std::holds_alternative<std::uint64_t>(constant->variant))
          {
               const auto widened =
                    ConvertConstant(std::get<std::uint64_t>(constant->variant), offsetIntegral, ptrDiffType);
               offset = std::make_unique<PRValue>(
                    ptrDiffType,
                    std::unique_ptr<IntegralNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                  IntegralNode(widened * elementSize, source)},
                    true); // folded scale
          }
          else
          {
               if (offset->Type() != ptrDiffType)
               {
                    const auto innerSource = offset->Value()->Source();
                    const auto wasConstexpr = offset->IsConstantExpression();

                    offset = std::make_unique<PRValue>(ptrDiffType,
                                                       std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                            new (*this->GetContext().nodeAllocator) high::ConvertNode(
                                                                 std::move(offset), ptrDiffType, innerSource)},
                                                       wasConstexpr);
               }

               if (elementSize != 1)
               {
                    auto scale = std::make_unique<PRValue>(
                         ptrDiffType,
                         std::unique_ptr<IntegralNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                       IntegralNode(elementSize, source)},
                         true);
                    const auto wasConstexpr = offset->IsConstantExpression();
                    offset = std::make_unique<PRValue>(
                         ptrDiffType,
                         std::unique_ptr<high::MultiplicationNode, IRDeleter>{
                              new (*this->GetContext().nodeAllocator)
                                   high::MultiplicationNode(std::move(offset), std::move(scale), source)},
                         wasConstexpr);
               }
          }

          if (!isPlus)
          {
               return std::make_unique<PRValue>(leftPointer,
                                                std::unique_ptr<high::SubtractionNode, IRDeleter>{
                                                     new (*this->GetContext().nodeAllocator) high::SubtractionNode(
                                                          std::move(left), std::move(offset), source)},
                                                false);
          }

          return std::make_unique<PRValue>(leftPointer,
                                           std::unique_ptr<high::AdditionNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::AdditionNode(std::move(left), std::move(offset), source)},
                                           false);
     }

     if (leftPointer != nullptr && rightPointer != nullptr)
     {
          if (operator_ != ast::Operator::Minus)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build("Cannot add two pointers", source));
               return nullptr;
          }

          if (leftPointer->BaseType() != rightPointer->BaseType())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot subtract pointers to different types (" + left->Type()->Name() + " and " +
                              right->Type()->Name() + ")",
                         source));
               return nullptr;
          }

          const auto elementSize = leftPointer->BaseType()->Size();
          if (elementSize == 0)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot subtract pointers to a void or incomplete type", source));
               return nullptr;
          }

          TypeRequest typeRequest{};
          typeRequest.kind = TypeKind::Fundamental;
          typeRequest.data = PlatformIntegerRequest{.kind = PlatformIntegerKind::PtrDiff};
          const auto* resultType = GetTypeContext().Get(typeRequest);

          Expression difference =
               std::make_unique<PRValue>(resultType,
                                         std::unique_ptr<high::SubtractionNode, IRDeleter>{
                                              new (*this->GetContext().nodeAllocator)
                                                   high::SubtractionNode(std::move(left), std::move(right), source)},
                                         false);
          if (elementSize == 1) return difference;

          auto divisor =
               std::make_unique<PRValue>(resultType,
                                         std::unique_ptr<IntegralNode, IRDeleter>{new (
                                              *this->GetContext().nodeAllocator) IntegralNode(elementSize, source)},
                                         true);
          return std::make_unique<PRValue>(
               resultType,
               std::unique_ptr<high::DivideNode, IRDeleter>{new (*this->GetContext().nodeAllocator) high::DivideNode(
                    std::move(difference), std::move(divisor), source)},
               false);
     }

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform this binary operation on " + left->Type()->Name() + " and " + right->Type()->Name(),
                    left->Value()->Source()));

          return nullptr;
     }
     auto operands = UsualArithmeticConversions(std::move(left), std::move(right));
     if (!operands) return nullptr;

     if (operator_ == ast::Operator::Plus)
          return std::make_unique<PRValue>(operands->type,
                                           std::unique_ptr<high::AdditionNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator) high::AdditionNode(
                                                     std::move(operands->left), std::move(operands->right), source)},
                                           false);

     return std::make_unique<PRValue>(operands->type,
                                      std::unique_ptr<high::SubtractionNode, IRDeleter>{
                                           new (*this->GetContext().nodeAllocator) high::SubtractionNode(
                                                std::move(operands->left), std::move(operands->right), source)},
                                      false);
}

Expression ecpps::ir::IR::ParseMultiplicativeExpression(Expression left, ast::Operator operator_, Expression right,
                                                        const Location& source) const
{
     runtime_assert(operator_ == ast::Operator::Asterisk || operator_ == ast::Operator::Solidus ||
                         operator_ == ast::Operator::Percent,
                    "Operator was not multiplicative in a multiplicative-expression");

     switch (operator_)
     {
     case ast::Operator::Asterisk:
          return ParseIntegralBinary<high::MultiplicationNode>(std::move(left), std::move(right), source);
     case ast::Operator::Solidus:
          return ParseIntegralBinary<high::DivideNode>(std::move(left), std::move(right), source);
     default: return ParseIntegralBinary<high::ModuloNode>(std::move(left), std::move(right), source);
     }
}

Expression ecpps::ir::IR::ParseShiftExpression(Expression left, ast::Operator operator_, Expression right,
                                               const Location& source)
{
     runtime_assert(operator_ == ast::Operator::LeftShift || operator_ == ast::Operator::RightShift,
                    "Invalid operator for a shift-expression");

     const auto* leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     const auto* rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform this binary operation on " + left->Type()->Name() + " and " + right->Type()->Name(),
                    left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto* commonType = leftIntegral->CommonWith(rightIntegral);
     if (commonType == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot find a common integral type between " + left->Type()->Name() + " and " +
                         left->Type()->Name(),
                    left->Value()->Source()));
          return nullptr;
     }

     if (left->Type() != commonType)
     {
          const auto innerSource = left->Value()->Source();
          const auto wasConstexpr = left->IsConstantExpression();

          left = std::make_unique<PRValue>(commonType,
                                           std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::ConvertNode(std::move(left), commonType, innerSource)},
                                           wasConstexpr);
     }

     if (right->Type() != commonType)
     {
          const auto innerSource = right->Value()->Source();
          const auto wasConstexpr = right->IsConstantExpression();

          right = std::make_unique<PRValue>(commonType,
                                            std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                 new (*this->GetContext().nodeAllocator)
                                                      high::ConvertNode(std::move(right), commonType, innerSource)},
                                            wasConstexpr);
     }

     if (operator_ == ast::Operator::LeftShift)
          return std::make_unique<PRValue>(commonType,
                                           std::unique_ptr<high::LeftShiftNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::LeftShiftNode(std::move(left), std::move(right), source)},
                                           false);

     return std::make_unique<PRValue>(commonType,
                                      std::unique_ptr<high::RightShiftNode, IRDeleter>{
                                           new (*this->GetContext().nodeAllocator)
                                                high::RightShiftNode(std::move(left), std::move(right), source)},
                                      false);
}

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

          const auto* variableType = GetTypeContext().Get(pointerRequest);
          operand = ConvertTo(std::move(operand), variableType);

          if (operand == nullptr) return nullptr;
     }
     const auto* pointerType = operand->Type()->CastTo<typeSystem::PointerType>();

     if (pointerType == nullptr)
     {
          // TODO: Classes
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform this unary operation on " + operand->Type()->Name(), operand->Value()->Source()));

          return nullptr;
     }

     return std::make_unique<LValue>(
          pointerType->BaseType(),
          std::unique_ptr<high::DereferenceNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                 high::DereferenceNode(std::move(operand), source)},
          false);
}

Expression ecpps::ir::IR::ParseAddressOfExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     if (!operand->IsGLValue())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "A glvalue is required for the address-of operator", operand->Value()->Source()));

          return nullptr;
     }

     TypeRequest pointerRequest{};
     pointerRequest.kind = TypeKind::Compound;
     pointerRequest.data = PointerRequest{.elementType = operand->Type()};
     const auto* pointerType = GetTypeContext().Get(pointerRequest);

     return std::make_unique<PRValue>(
          pointerType,
          std::unique_ptr<high::AddressOfNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                               high::AddressOfNode(std::move(operand), source)},
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
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Cannot subscript types `{}` and `{}`", left->Type()->Name(), right->Type()->Name()),
                    source));
          return nullptr;
     }

     if (leftIsArray)
     {
          const auto* arrayType = left->Type()->CastTo<typeSystem::ArrayType>();
          runtime_assert(arrayType != nullptr, "Expected an array type");

          TypeRequest pointerRequest{};
          pointerRequest.kind = TypeKind::Compound;
          pointerRequest.data = PointerRequest{.elementType = arrayType->ElementType()};
          const auto* pointerType = GetTypeContext().Get(pointerRequest);

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
          const auto* pointerType = GetTypeContext().Get(pointerRequest);

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
Expression ecpps::ir::IR::ParseBinaryOrExpression(Expression left, Expression right, const Location& source) const
{
     return ParseIntegralBinary<high::BinaryOrNode>(std::move(left), std::move(right), source);
}
Expression ecpps::ir::IR::ParseBinaryAndExpression(Expression left, Expression right, const Location& source) const
{
     const auto* leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     const auto* rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform this binary operation on " + left->Type()->Name() + " and " + right->Type()->Name(),
                    left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto* commonType = leftIntegral->CommonWith(rightIntegral);
     if (commonType == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot find a common integral type between " + left->Type()->Name() + " and " +
                         left->Type()->Name(),
                    left->Value()->Source()));
          return nullptr;
     }

     if (left->Type() != commonType)
     {
          const auto innerSource = left->Value()->Source();
          const auto wasConstexpr = left->IsConstantExpression();

          left = std::make_unique<PRValue>(commonType,
                                           std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::ConvertNode(std::move(left), commonType, innerSource)},
                                           wasConstexpr);
     }

     if (right->Type() != commonType)
     {
          const auto innerSource = right->Value()->Source();
          const auto wasConstexpr = right->IsConstantExpression();

          right = std::make_unique<PRValue>(commonType,
                                            std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                 new (*this->GetContext().nodeAllocator)
                                                      high::ConvertNode(std::move(right), commonType, innerSource)},
                                            wasConstexpr);
     }

     return std::make_unique<PRValue>(commonType,
                                      std::unique_ptr<high::BinaryAndNode, IRDeleter>{
                                           new (*this->GetContext().nodeAllocator)
                                                high::BinaryAndNode(std::move(left), std::move(right), source)},
                                      false);
}
Expression ecpps::ir::IR::ParseBinaryXorExpression(Expression left, Expression right, const Location& source) const
{
     const auto* leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     const auto* rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform this binary operation on " + left->Type()->Name() + " and " + right->Type()->Name(),
                    left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto* commonType = leftIntegral->CommonWith(rightIntegral);
     if (commonType == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot find a common integral type between " + left->Type()->Name() + " and " +
                         left->Type()->Name(),
                    left->Value()->Source()));
          return nullptr;
     }

     if (left->Type() != commonType)
     {
          const auto innerSource = left->Value()->Source();
          const auto wasConstexpr = left->IsConstantExpression();

          left = std::make_unique<PRValue>(commonType,
                                           std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::ConvertNode(std::move(left), commonType, innerSource)},
                                           wasConstexpr);
     }

     if (right->Type() != commonType)
     {
          const auto innerSource = right->Value()->Source();
          const auto wasConstexpr = right->IsConstantExpression();

          right = std::make_unique<PRValue>(commonType,
                                            std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                 new (*this->GetContext().nodeAllocator)
                                                      high::ConvertNode(std::move(right), commonType, innerSource)},
                                            wasConstexpr);
     }

     return std::make_unique<PRValue>(commonType,
                                      std::unique_ptr<high::BinaryXorNode, IRDeleter>{
                                           new (*this->GetContext().nodeAllocator)
                                                high::BinaryXorNode(std::move(left), std::move(right), source)},
                                      false);
}

Expression ecpps::ir::IR::ParsePreIncrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");
     if (IsIndirectLValue(operand))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(std::string{IndirectStoreUnsupported},
                                                                               source));
          return nullptr;
     }

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A modifiable lvalue is required for a the builtin pre-increment operator",
                         operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<LValue>(
               operandType,
               std::unique_ptr<high::AdditionAssignNode, IRDeleter>{
                    new (*this->GetContext().nodeAllocator) high::AdditionAssignNode(
                         std::move(operand),
                         std::make_unique<PRValue>(operandType,
                                                   std::unique_ptr<IntegralNode, IRDeleter>{new (
                                                        *this->GetContext().nodeAllocator) IntegralNode(1, source)},
                                                   true),
                         source)},
               false);
     }

     throw TracedException("Not implemented");
}

Expression ecpps::ir::IR::ParsePostIncrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");
     if (IsIndirectLValue(operand))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(std::string{IndirectStoreUnsupported},
                                                                               source));
          return nullptr;
     }

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A modifiable lvalue is required for a the builtin post-increment operator",
                         operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<PRValue>(operandType,
                                           std::unique_ptr<high::PostIncrementNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::PostIncrementNode(std::move(operand), 1, source)},
                                           false);
     }

     throw TracedException("Not implemented");
}

Expression ecpps::ir::IR::ParsePreDecrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");
     if (IsIndirectLValue(operand))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(std::string{IndirectStoreUnsupported},
                                                                               source));
          return nullptr;
     }

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A modifiable lvalue is required for a the builtin pre-decrement operator",
                         operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<LValue>(
               operandType,
               std::unique_ptr<high::SubtractionAssignNode, IRDeleter>{
                    new (*this->GetContext().nodeAllocator) high::SubtractionAssignNode(
                         std::move(operand),
                         std::make_unique<PRValue>(operandType,
                                                   std::unique_ptr<IntegralNode, IRDeleter>{new (
                                                        *this->GetContext().nodeAllocator) IntegralNode(1, source)},
                                                   true),
                         source)},
               false);
     }

     throw TracedException("Not implemented");
}

Expression ecpps::ir::IR::ParsePostDecrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");
     if (IsIndirectLValue(operand))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(std::string{IndirectStoreUnsupported},
                                                                               source));
          return nullptr;
     }

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A modifiable lvalue is required for a the builtin post-decrement operator",
                         operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<PRValue>(operandType,
                                           std::unique_ptr<high::PostDecrementNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::PostDecrementNode(std::move(operand), 1, source)},
                                           false);
     }

     throw TracedException("Not implemented");
}
Expression ecpps::ir::IR::ParseBitwiseNotExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     const auto& operandType = operand->Type();
     if (IsIntegral(operandType))
     {
          const auto* integralType = operandType->CastTo<typeSystem::IntegralType>();

          runtime_assert(integralType != nullptr, "Integral is required here");
          integralType = typeSystem::PromoteInteger(integralType);

          if (operandType != integralType)
          {
               const auto innerSource = operand->Value()->Source();
               const auto wasConstexpr = operand->IsConstantExpression();

               operand = std::make_unique<PRValue>(integralType,
                                                   std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                        new (*this->GetContext().nodeAllocator) high::ConvertNode(
                                                             std::move(operand), integralType, innerSource)},
                                                   wasConstexpr);
          }

          return std::make_unique<PRValue>(
               operandType,
               std::unique_ptr<high::BitwiseNotNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                     high::BitwiseNotNode(std::move(operand), source)},
               false);
     }

     throw TracedException("Not implemented");
}
Expression ecpps::ir::IR::ParseArithmeticNegationExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     const auto& operandType = operand->Type();
     if (IsIntegral(operandType))
     {
          const auto* integralType = operandType->CastTo<typeSystem::IntegralType>();

          runtime_assert(integralType != nullptr, "Integral is required here");
          integralType = typeSystem::PromoteInteger(integralType);

          if (operandType != integralType)
          {
               const auto innerSource = operand->Value()->Source();
               const auto wasConstexpr = operand->IsConstantExpression();

               operand = std::make_unique<PRValue>(integralType,
                                                   std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                        new (*this->GetContext().nodeAllocator) high::ConvertNode(
                                                             std::move(operand), integralType, innerSource)},
                                                   wasConstexpr);
          }

          return std::make_unique<PRValue>(operandType,
                                           std::unique_ptr<high::ArithmeticNegationNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::ArithmeticNegationNode(std::move(operand), source)},
                                           false);
     }

     throw TracedException("Not implemented");
}
Expression ecpps::ir::IR::ParseAssignmentExpression(Expression left, Expression right, const Location& source) const
{
     if (RejectClassByValue(left->Type(), source, "an assignment operand")) return nullptr;

     if (IsIndirectLValue(left))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(std::string{IndirectStoreUnsupported},
                                                                               source));
          return nullptr;
     }

     const auto* leftIntegral = left->Type()->CastTo<typeSystem::QualifiedType>();

     if (leftIntegral == nullptr)
     {
          // TODO: Classes, floating point etc
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform this binary operation on " + left->Type()->Name() + " and " + right->Type()->Name(),
                    left->Value()->Source()));

          return nullptr;
     }
     if (!left->IsLValue())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform this binary operation on an rvalue", left->Value()->Source()));
     }
     if (leftIntegral->IsConst())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Assignment expressions require modifiable lvalues. Cannot perform this binary operation on " +
                         left->Type()->Name() + " and " + right->Type()->Name(),
                    left->Value()->Source()));
     }

     if (right->Type() != left->Type())
     {
          right = ConvertTo(std::move(right), left->Type());
          if (right == nullptr) return nullptr;
     }

     const auto* resultType = left->Type();

     return std::make_unique<LValue>(resultType,
                                     std::unique_ptr<high::AssignNode, IRDeleter>{
                                          new (*this->GetContext().nodeAllocator)
                                               high::AssignNode(std::move(left), std::move(right), source)},
                                     false);
}

Expression ecpps::ir::IR::ParseUnaryExpression(const ast::UnaryOperatorNode& node)
{
     const auto operator_ = node.Value();
     auto operand = ParseExpression(node.Operand());
     if (operand == nullptr) return nullptr;

     switch (operator_)
     {
     case ast::Operator::Plus:
     case ast::Operator::Minus: return this->ParseArithmeticNegationExpression(std::move(operand), node.Source());
     case ast::Operator::Asterisk: return this->ParseDereferenceExpression(std::move(operand), node.Source());
     case ast::Operator::Ampersand: return this->ParseAddressOfExpression(std::move(operand), node.Source());
     case ast::Operator::Tilde: return this->ParseBitwiseNotExpression(std::move(operand), node.Source());
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
     if (left == nullptr) return nullptr;

     if (operator_ == ast::Operator::FullStop || operator_ == ast::Operator::Arrow)
     {
          const auto* memberName = dynamic_cast<const ast::IdentifierNode*>(node.Right().get());
          if (memberName == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Expected a data member name after the member access operator", node.Source()));
               return nullptr;
          }

          typeSystem::NonowningTypePointer objectType = left->Type();
          Expression objectPointer{};
          if (operator_ == ast::Operator::Arrow)
          {
               const auto* pointerType = objectType->CastTo<typeSystem::PointerType>();
               if (pointerType == nullptr)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "The '->' operator requires a pointer to a class type", node.Source()));
                    return nullptr;
               }
               objectType = pointerType->BaseType();
               objectPointer = std::move(left);
          }
          else
          {
               if (!typeSystem::IsClass(objectType) || !left->IsLValue())
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "The '.' operator requires a class lvalue", node.Source()));
                    return nullptr;
               }
               objectPointer = ParseAddressOfExpression(std::move(left), node.Source());
          }

          const auto* classType = objectType->CastTo<typeSystem::ClassType>();
          if (classType == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Member access requires a class type", node.Source()));
               return nullptr;
          }
          for (const auto& field : classType->Info().fields)
               if (!field.name.empty() && field.name == memberName->Value())
                    return this->BuildMemberAccess(std::move(objectPointer), classType->Info(), field, node.Source());

          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Class '{}' has no data member named '{}'", classType->Info().DisplayName(),
                                memberName->Value()),
                    memberName->Source()));
          return nullptr;
     }

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
     case ast::Operator::VerticalLine:
          return this->ParseBinaryOrExpression(std::move(left), std::move(right), node.Source());
     case ast::Operator::Ampersand:
          return this->ParseBinaryAndExpression(std::move(left), std::move(right), node.Source());
     case ast::Operator::CircumflexAccent:
          return this->ParseBinaryXorExpression(std::move(left), std::move(right), node.Source());
     case ast::Operator::Assignment:
          return this->ParseAssignmentExpression(std::move(left), std::move(right), node.Source());
     case ast::Operator::EqualsSign:
     case ast::Operator::NotEqual:
     case ast::Operator::Less:
     case ast::Operator::LessEqual:
     case ast::Operator::Greater:
     case ast::Operator::GreaterEqual:
          return this->ParseRelationalExpression(std::move(left), operator_, std::move(right), node.Source());
     default: throw TracedException(std::logic_error("Invalid binary operator"));
     }
}

struct CompareByPriority
{
     struct Candidate
     {
          ecpps::ir::MatchingScore argumentScore;
          std::size_t objectQualificationPenalty{};
          std::shared_ptr<ecpps::ir::FunctionScope> function;
     };

     bool operator()(const Candidate& a, const Candidate& b) const
     {
          if (a.argumentScore != b.argumentScore) return a.argumentScore < b.argumentScore;
          return a.objectQualificationPenalty > b.objectQualificationPenalty;
     }
};

Expression ecpps::ir::IR::ParseMemberCallExpression(const ast::CallOperatorNode& node,
                                                    const ast::BinaryOperatorNode& memberAccess)
{
     const auto* memberName = dynamic_cast<const ast::IdentifierNode*>(memberAccess.Right().get());
     const auto report = [&](std::string message, const Location& source) -> Expression
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(std::move(message), source));
          return nullptr;
     };
     if (memberName == nullptr)
          return report("Expected a member function name after the member access operator", memberAccess.Source());

     auto receiver = ParseExpression(memberAccess.Left());
     if (receiver == nullptr) return nullptr;

     typeSystem::NonowningTypePointer objectType = receiver->Type();
     Expression receiverPointer{};
     bool receiverIsLvalue = memberAccess.Value() == ast::Operator::Arrow;
     if (memberAccess.Value() == ast::Operator::Arrow)
     {
          const auto* pointerType = objectType->CastTo<typeSystem::PointerType>();
          if (pointerType == nullptr)
               return report("The '->' operator requires a pointer to a class type", memberAccess.Source());
          objectType = pointerType->BaseType();
          receiverPointer = std::move(receiver);
     }
     else if (memberAccess.Value() == ast::Operator::FullStop)
     {
          if (!typeSystem::IsClass(objectType))
               return report("Member function access requires a class object", memberAccess.Source());
          receiverIsLvalue = receiver->IsLValue();
          if (!receiver->IsGLValue())
               return report("A glvalue object is required to call a member function", memberAccess.Source());
          receiverPointer = ParseAddressOfExpression(std::move(receiver), memberAccess.Source());
          if (receiverPointer == nullptr) return nullptr;
     }
     else
          return report("Expected '.' or '->' for member function access", memberAccess.Source());

     const auto* pointerType = receiverPointer->Type()->CastTo<typeSystem::PointerType>();
     const auto* classType =
          pointerType != nullptr ? pointerType->BaseType()->CastTo<typeSystem::ClassType>() : nullptr;
     if (classType == nullptr)
          return report("Member function access requires a pointer to a class", memberAccess.Source());

     const auto classRecord = this->GetContext().classTable->classes.find(classType->Info().id);
     if (classRecord == this->GetContext().classTable->classes.end() || classRecord->second.scope == nullptr)
          return report(std::format("Member functions of '{}' are unavailable", classType->Info().DisplayName()),
                        memberName->Source());

     std::vector<Expression> arguments = node.Arguments() |
                                         std::views::transform(
                                              [this](const ast::NodePointer& argument)
                                              {
                                                   return this->ParseExpression(argument);
                                              }) |
                                         std::ranges::to<std::vector>();
     if (std::ranges::any_of(arguments,
                             [](const Expression& argument)
                             {
                                  return argument == nullptr;
                             }))
          return nullptr;

     const auto& memberFunctions = classRecord->second.scope->functions;
     std::vector<std::shared_ptr<FunctionScope>> sameNameCandidates{};
     for (const auto& function : memberFunctions)
          if (function->Name() == memberName->Value() && function->memberClassInfo != nullptr)
               sameNameCandidates.push_back(function);

     const auto receiverFailuresFor = [&](const std::shared_ptr<FunctionScope>& function)
     {
          std::vector<std::string> failures{};
          if (function->isStatic) return failures;

          if (classType->IsConst() && !function->memberFunctionInfo.isConst)
               failures.emplace_back("The receiver is const, but this candidate is not const-qualified");
          if (classType->IsVolatile() && !function->memberFunctionInfo.isVolatile)
               failures.emplace_back("The receiver is volatile, but this candidate is not volatile-qualified");
          if (function->memberFunctionInfo.refQualifier == abi::MemberRefQualifier::LValue && !receiverIsLvalue)
               failures.emplace_back("The receiver is an rvalue, but this candidate requires an lvalue receiver");
          if (function->memberFunctionInfo.refQualifier == abi::MemberRefQualifier::RValue && receiverIsLvalue)
               failures.emplace_back("The receiver is an lvalue, but this candidate requires an rvalue receiver");
          return failures;
     };

     const auto accessName = [](const typeSystem::Access access)
     {
          switch (access)
          {
          case typeSystem::Access::Public: return std::string{"public"};
          case typeSystem::Access::Protected: return std::string{"protected"};
          case typeSystem::Access::Private: return std::string{"private"};
          }
          return std::string{"inaccessible"};
     };

     const auto makeCandidateNote = [&](const std::shared_ptr<FunctionScope>& function,
                                        const bool includeArgumentFailures, const bool includeReceiverFailures)
     {
          auto candidateNote = std::make_unique<diagnostics::Information>(
               "candidate", "Candidate: " + FormatFunctionSignature(function), function->source);
          if (includeArgumentFailures)
          {
               const auto failureInfo = AnalyseCandidateFailure(function, arguments);
               for (const auto& [argumentIndex, failure] : failureInfo.parameterFailures)
               {
                    Location failureSource = memberName->Source();
                    if (std::cmp_not_equal(argumentIndex, -1) && argumentIndex > 0 &&
                        argumentIndex <= node.Arguments().Size())
                         failureSource = node.Arguments()[argumentIndex - 1]->Source();
                    auto failureNote = std::make_unique<diagnostics::Information>("note", failure, failureSource);
                    if (std::cmp_not_equal(argumentIndex, -1) && argumentIndex > 0 &&
                        argumentIndex <= arguments.size() && argumentIndex <= function->parameters.size())
                    {
                         const auto& argument = arguments[argumentIndex - 1];
                         const auto& parameter = function->parameters[argumentIndex - 1];
                         if (argument != nullptr &&
                             (typeSystem::IsPointer(argument->Type()) || typeSystem::IsArray(argument->Type())) &&
                             !typeSystem::IsPointer(parameter.type) && !typeSystem::IsArray(parameter.type))
                              failureNote->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                                   "note", "Did you mean to dereference the argument?", failureSource));
                    }
                    candidateNote->SubDiagnostics().push_back(std::move(failureNote));
               }
          }
          if (includeReceiverFailures)
               for (const auto& failure : receiverFailuresFor(function))
                    candidateNote->SubDiagnostics().push_back(
                         std::make_unique<diagnostics::Information>("note", failure, memberAccess.Left()->Source()));

          if (!this->CanAccess(function->memberClassInfo->id, function->memberAccess))
               candidateNote->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                    "note",
                    std::format("This candidate is {} and is inaccessible here", accessName(function->memberAccess)),
                    function->source));
          if (function->isDeleted)
               candidateNote->SubDiagnostics().push_back(
                    std::make_unique<diagnostics::Information>("note", "This candidate is deleted", function->source));
          return candidateNote;
     };

     using FunctionCandidate = CompareByPriority::Candidate;
     std::priority_queue<FunctionCandidate, std::vector<FunctionCandidate>, CompareByPriority> candidates{};
     for (const auto& candidate : classRecord->second.scope->functions)
     {
          if (candidate->Name() != memberName->Value() || candidate->memberClassInfo == nullptr) continue;
          std::size_t objectQualificationPenalty{};
          const auto receiverFailures = receiverFailuresFor(candidate);
          if (!candidate->isStatic)
          {
               if (!receiverFailures.empty()) continue;
               if (!classType->IsConst() && candidate->memberFunctionInfo.isConst) objectQualificationPenalty++;
               if (!classType->IsVolatile() && candidate->memberFunctionInfo.isVolatile) objectQualificationPenalty++;
          }
          const auto match = MatchFunction(candidate, arguments);
          if (!match) continue;
          candidates.push({.argumentScore = match,
                           .objectQualificationPenalty = objectQualificationPenalty,
                           .function = candidate});
     }

     const auto addSimilarMembers = [&](diagnostics::Error& diagnostic)
     {
          struct MemberSuggestion
          {
               std::string name{};
               std::size_t score{};
               bool acceptsArguments{};
               bool isDataMember{};
               std::vector<std::shared_ptr<FunctionScope>> functions{};
               Location source{};
          };
          std::vector<MemberSuggestion> suggestions{};
          const std::size_t maximumDistance = std::max<std::size_t>(3, memberName->Value().length() / 3);

          for (const auto& function : memberFunctions)
          {
               if (function->Name() == memberName->Value() || function->memberClassInfo == nullptr) continue;
               const auto functionName = function->Name().value_or("__unknown");
               const auto distance = LevenshteinDistance(memberName->Value(), functionName);
               if (distance == 0 || distance > maximumDistance) continue;

               auto suggestion = std::ranges::find(suggestions, functionName, &MemberSuggestion::name);
               if (suggestion == suggestions.end())
               {
                    suggestions.push_back(MemberSuggestion{.name = functionName,
                                                           .score = std::numeric_limits<std::size_t>::max(),
                                                           .acceptsArguments = false,
                                                           .isDataMember = false,
                                                           .functions = {},
                                                           .source = function->source});
                    suggestion = std::prev(suggestions.end());
               }
               suggestion->functions.push_back(function);
               const auto match = MatchFunction(function, arguments);
               const auto failures = AnalyseCandidateFailure(function, arguments).parameterFailures.size();
               suggestion->acceptsArguments = suggestion->acceptsArguments || static_cast<bool>(match);
               const std::size_t score = distance * 4 + failures * 2 + static_cast<std::size_t>(!match);
               suggestion->score = std::min(suggestion->score, score);
          }

          for (const auto& field : classType->Info().fields)
          {
               if (field.name.empty() || field.name == memberName->Value()) continue;
               const auto distance = LevenshteinDistance(memberName->Value(), field.name);
               if (distance == 0 || distance > maximumDistance) continue;

               auto suggestion = std::ranges::find(suggestions, field.name, &MemberSuggestion::name);
               if (suggestion == suggestions.end())
                    suggestions.push_back(MemberSuggestion{.name = field.name,
                                                           .score = distance * 4 + 1,
                                                           .acceptsArguments = false,
                                                           .isDataMember = true,
                                                           .functions = {},
                                                           .source = field.source});
               else
                    suggestion->isDataMember = true;
          }

          if (suggestions.empty()) return;
          std::ranges::sort(suggestions,
                            [](const MemberSuggestion& left, const MemberSuggestion& right)
                            {
                                 if (left.acceptsArguments != right.acceptsArguments)
                                      return left.acceptsArguments > right.acceptsArguments;
                                 if (left.score != right.score) return left.score < right.score;
                                 return left.name < right.name;
                            });

          auto suggestionGroup = std::make_unique<diagnostics::Information>(
               "suggestions", "Similar members (ranked by spelling and argument fit; * marks the top five)",
               memberName->Source());
          for (std::size_t index = 0; index < suggestions.size(); index++)
          {
               const auto& suggestion = suggestions[index];
               std::string description =
                    std::format("{} member '{}'", index < 5 ? "* Similar" : "Similar", suggestion.name);
               if (!suggestion.functions.empty())
                    description += suggestion.acceptsArguments ? " has a parameter list accepting these arguments"
                                                               : " has no overload accepting these arguments";
               if (suggestion.isDataMember)
                    description += suggestion.functions.empty() ? " is a data member and is not callable"
                                                                : "; it is also a data member and is not callable";
               auto suggestionNote =
                    std::make_unique<diagnostics::Information>("suggestion", description, suggestion.source);
               for (const auto& function : suggestion.functions)
                    suggestionNote->SubDiagnostics().push_back(makeCandidateNote(function, true, true));
               suggestionGroup->SubDiagnostics().push_back(std::move(suggestionNote));
          }
          diagnostic.SubDiagnostics().push_back(std::move(suggestionGroup));
     };

     if (candidates.empty())
     {
          std::string message;
          if (sameNameCandidates.empty())
          {
               const auto dataMember =
                    std::ranges::find(classType->Info().fields, memberName->Value(), &typeSystem::ClassField::name);
               if (dataMember != classType->Info().fields.end())
                    message = std::format("Member '{}' of '{}' is a data member and cannot be called",
                                          memberName->Value(), classType->Info().DisplayName());
               else
                    message = std::format("Class '{}' has no member function named '{}'",
                                          classType->Info().DisplayName(), memberName->Value());
          }
          else
               message = std::format("No overload of member function '{}::{}' matches these arguments",
                                     classType->Info().DisplayName(), memberName->Value());

          auto diagnostic = std::make_unique<diagnostics::TypeError>(std::move(message), memberName->Source());
          for (const auto& candidate : sameNameCandidates)
          {
               auto note = makeCandidateNote(candidate, true, true);
               diagnostic->SubDiagnostics().push_back(std::move(note));
          }
          if (sameNameCandidates.empty())
               for (const auto& field : classType->Info().fields)
                    if (field.name == memberName->Value())
                    {
                         diagnostic->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                              "declaration", "Data member declared here; data members are not callable", field.source));
                         break;
                    }
          addSimilarMembers(*diagnostic);
          this->GetContext().diagnostics.get().diagnosticsList.push_back(std::move(diagnostic));
          return nullptr;
     }

     auto rankedCandidates = candidates;
     const auto selectedCandidate = rankedCandidates.top();
     rankedCandidates.pop();
     auto ambiguity =
          std::make_unique<diagnostics::TypeError>(std::format("Call to member function '{}::{}' is ambiguous",
                                                               classType->Info().DisplayName(), memberName->Value()),
                                                   memberName->Source());
     std::vector<std::shared_ptr<FunctionScope>> tiedBestCandidates{selectedCandidate.function};
     while (!rankedCandidates.empty() && rankedCandidates.top().argumentScore == selectedCandidate.argumentScore &&
            rankedCandidates.top().objectQualificationPenalty == selectedCandidate.objectQualificationPenalty)
     {
          tiedBestCandidates.push_back(rankedCandidates.top().function);
          rankedCandidates.pop();
     }
     if (tiedBestCandidates.size() > 1)
     {
          for (const auto& tiedCandidate : tiedBestCandidates)
               ambiguity->SubDiagnostics().push_back(makeCandidateNote(tiedCandidate, false, false));
          this->GetContext().diagnostics.get().diagnosticsList.push_back(std::move(ambiguity));
          return nullptr;
     }

     const auto& candidate = candidates.top().function;
     const bool isAccessible = this->CanAccess(candidate->memberClassInfo->id, candidate->memberAccess);
     if (!isAccessible || candidate->isDeleted)
     {
          std::string message;
          if (!isAccessible && candidate->isDeleted)
               message = std::format("Selected member function '{}' is inaccessible and deleted",
                                     FormatFunctionSignature(candidate));
          else if (!isAccessible)
               message = std::format("Selected member function '{}' is inaccessible ({})",
                                     FormatFunctionSignature(candidate), accessName(candidate->memberAccess));
          else
               message = std::format("Selected member function '{}' is deleted", FormatFunctionSignature(candidate));
          auto diagnostic = std::make_unique<diagnostics::TypeError>(std::move(message), memberName->Source());
          diagnostic->SubDiagnostics().push_back(makeCandidateNote(candidate, false, false));
          this->GetContext().diagnostics.get().diagnosticsList.push_back(std::move(diagnostic));
          return nullptr;
     }
     if (!RequireComplete(candidate->returnType, node.Source(), "a member function call") ||
         RejectClassByValue(candidate->returnType, node.Source(), "a return value"))
          return nullptr;
     for (const auto& parameter : candidate->parameters)
          if (!RequireComplete(parameter.type, node.Source(), "a member function call") ||
              RejectClassByValue(parameter.type, node.Source(), "a by-value argument"))
               return nullptr;

     std::vector<Expression> evaluatedArguments{};
     evaluatedArguments.reserve(arguments.size() + static_cast<std::size_t>(!candidate->isStatic));
     if (candidate->isStatic) (void)LowerExpression(std::move(receiverPointer), this->_built);
     else
          evaluatedArguments.push_back(std::move(receiverPointer));
     for (std::size_t index = 0; index < arguments.size(); index++)
          evaluatedArguments.push_back(ConvertTo(std::move(arguments[index]), candidate->parameters[index].type));

     auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{new (*this->GetContext().nodeAllocator) FunctionCallNode(
          candidate, std::move(evaluatedArguments), node.Source())};
     return std::make_unique<PRValue>(candidate->returnType, std::move(call), false);
}

Expression ecpps::ir::IR::ParseCallExpression(const ast::CallOperatorNode& node)
{
     if (const auto* memberAccess = dynamic_cast<const ast::BinaryOperatorNode*>(node.Function().get());
         memberAccess != nullptr &&
         (memberAccess->Value() == ast::Operator::FullStop || memberAccess->Value() == ast::Operator::Arrow))
          return this->ParseMemberCallExpression(node, *memberAccess);

     const ast::IdentifierNode* identifierFunction = nullptr;
     std::vector<std::string> qualifiers;
     bool isFromGlobalNamespace = false;

     if (auto* id = dynamic_cast<ast::IdentifierNode*>(node.Function().get()))
     {
          identifierFunction = id;
     }
     else if (auto* qid = dynamic_cast<ast::QualifiedIdNode*>(node.Function().get()))
     {
          const auto& parts = qid->Path();

          if (parts.empty()) return nullptr;
          if (typeid(*parts.front()) == typeid(ast::GlobalScopeNode)) isFromGlobalNamespace = true;

          for (std::size_t i = +isFromGlobalNamespace; i + 1 < parts.size(); i++)
          {
               auto* part = dynamic_cast<ast::IdentifierNode*>(parts[i].get());
               if (!part) return nullptr;
               qualifiers.push_back(part->Value());
          }
          if (isFromGlobalNamespace && parts.size() == 1)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Expected a function name in a call expression, found global scope specifier",
                         node.Function()->Source()));
               return nullptr;
          }
          identifierFunction = dynamic_cast<ast::IdentifierNode*>(parts.back().get());
     }

     if (!identifierFunction)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Expected a function name in a call expression, found: {}",
                                node.Function()->ToString(0)),
                    node.Function()->Source()));
          return nullptr;
     }

     const std::string& name = identifierFunction->Value();
     using FunctionCandidate = CompareByPriority::Candidate;
     std::priority_queue<FunctionCandidate, std::vector<FunctionCandidate>, CompareByPriority> candidates{};

     bool didMatchName = false;
     for (const auto& context : (isFromGlobalNamespace ? std::deque{this->GetContext().contextSequence.front()}
                                                       : this->GetContext().contextSequence) |
                                     std::views::reverse)
     {
          if (didMatchName) break;

          auto* scope = &context->GetScope();
          if (!qualifiers.empty())
          {
               bool matchesQualifiers = true;
               auto* currentScope = scope;
               for (const auto& qualifier : qualifiers)
               {
                    if (const auto className = currentScope->classes.find(qualifier);
                        className != currentScope->classes.end())
                    {
                         const auto classRecord = this->GetContext().classTable->classes.find(className->second);
                         if (classRecord == this->GetContext().classTable->classes.end() ||
                             classRecord->second.scope == nullptr)
                         {
                              matchesQualifiers = false;
                              break;
                         }
                         currentScope = classRecord->second.scope;
                         continue;
                    }
                    const auto* namespaceScope = dynamic_cast<ecpps::ir::NamespaceScope*>(currentScope);
                    if (!namespaceScope)
                    {
                         matchesQualifiers = false;
                         break;
                    }

                    auto it = std::ranges::find_if(namespaceScope->subNamespaces,
                                                   [&qualifier](const std::unique_ptr<ecpps::ir::NamespaceScope>& ns)
                                                   {
                                                        return ns->Name() == qualifier;
                                                   });
                    if (it == namespaceScope->subNamespaces.end())
                    {
                         matchesQualifiers = false;
                         break;
                    }
                    currentScope = it->get();
               }
               if (!matchesQualifiers) continue;
               scope = currentScope;
          }

          std::vector<Expression> arguments = node.Arguments() |
                                              std::views::transform(
                                                   [this](const ast::NodePointer& argument)
                                                   {
                                                        return this->ParseExpression(argument);
                                                   }) |
                                              std::ranges::to<std::vector>();

          for (const auto& candidate : scope->functions)
          {
               if (candidate->Name() != name) continue;

               std::size_t objectQualificationPenalty{};
               if (candidate->memberClassInfo != nullptr)
               {
                    if (!candidate->isStatic)
                    {
                         typeSystem::NonowningTypePointer receiverType{};
                         if (!this->_objectInitialisationStack.empty())
                         {
                              const auto* activeClass =
                                   this->_objectInitialisationStack.back().type->CastTo<typeSystem::ClassType>();
                              if (activeClass != nullptr && activeClass->Info().id == candidate->memberClassInfo->id)
                                   receiverType = this->PointerTo(this->_objectInitialisationStack.back().type);
                         }
                         if (receiverType == nullptr)
                              for (const auto& activeContext : this->GetContext().contextSequence | std::views::reverse)
                              {
                                   const auto* activeFunction =
                                        dynamic_cast<const FunctionContext*>(activeContext.get());
                                   if (activeFunction == nullptr || activeFunction->thisType == nullptr) continue;
                                   const auto* activePointer =
                                        activeFunction->thisType->CastTo<typeSystem::PointerType>();
                                   const auto* activeClass =
                                        activePointer != nullptr
                                             ? activePointer->BaseType()->CastTo<typeSystem::ClassType>()
                                             : nullptr;
                                   if (activeClass != nullptr &&
                                       activeClass->Info().id == candidate->memberClassInfo->id)
                                        receiverType = activeFunction->thisType;
                                   break;
                              }
                         if (receiverType == nullptr) continue;
                         const auto* receiverPointer = receiverType->CastTo<typeSystem::PointerType>();
                         const auto* receiverClass = receiverPointer != nullptr
                                                          ? receiverPointer->BaseType()->CastTo<typeSystem::ClassType>()
                                                          : nullptr;
                         if (receiverClass == nullptr ||
                             (receiverClass->IsConst() && !candidate->memberFunctionInfo.isConst) ||
                             (receiverClass->IsVolatile() && !candidate->memberFunctionInfo.isVolatile) ||
                             candidate->memberFunctionInfo.refQualifier == abi::MemberRefQualifier::RValue)
                              continue;
                         if (!receiverClass->IsConst() && candidate->memberFunctionInfo.isConst)
                              objectQualificationPenalty++;
                         if (!receiverClass->IsVolatile() && candidate->memberFunctionInfo.isVolatile)
                              objectQualificationPenalty++;
                    }
               }

               didMatchName = true;
               const auto match = ecpps::ir::IR::MatchFunction(candidate, arguments);
               if (!match) continue;
               candidates.push({.argumentScore = match,
                                .objectQualificationPenalty = objectQualificationPenalty,
                                .function = candidate});
          }

          if (candidates.empty()) continue;

          auto rankedCandidates = candidates;
          const auto selectedCandidate = rankedCandidates.top();
          rankedCandidates.pop();
          if (!rankedCandidates.empty() && rankedCandidates.top().argumentScore == selectedCandidate.argumentScore &&
              rankedCandidates.top().objectQualificationPenalty == selectedCandidate.objectQualificationPenalty)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Call to function '{}' is ambiguous", name), identifierFunction->Source()));
               return nullptr;
          }
          const auto& candidate = candidates.top().function;

          if (candidate->memberClassInfo != nullptr &&
              !this->CanAccess(candidate->memberClassInfo->id, candidate->memberAccess))
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Member function '{}' is not accessible", name), identifierFunction->Source()));
               return nullptr;
          }

          if (candidate->isDeleted)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Function '{}' is deleted", name), identifierFunction->Source()));
               return nullptr;
          }

          for (const auto& param : candidate->parameters)
               if (!RequireComplete(param.type, node.Source(), "a function call") ||
                   RejectClassByValue(param.type, node.Source(), "a by-value argument"))
                    return nullptr;
          if (candidate->returnType != nullptr && !typeSystem::g_void->CommonWith(candidate->returnType) &&
              (!RequireComplete(candidate->returnType, node.Source(), "a function call") ||
               RejectClassByValue(candidate->returnType, node.Source(), "a return value")))
               return nullptr;

          auto moveRange = std::ranges::subrange(std::make_move_iterator(arguments.begin()),
                                                 std::make_move_iterator(arguments.end()));

          std::vector<Expression> evaluatedArguments{};
          evaluatedArguments.reserve(
               arguments.size() +
               static_cast<std::size_t>(candidate->memberClassInfo != nullptr && !candidate->isStatic));
          if (candidate->memberClassInfo != nullptr && !candidate->isStatic)
          {
               Expression receiver{};
               if (!this->_objectInitialisationStack.empty())
               {
                    const auto* activeClass =
                         this->_objectInitialisationStack.back().type->CastTo<typeSystem::ClassType>();
                    if (activeClass != nullptr && activeClass->Info().id == candidate->memberClassInfo->id)
                         receiver = std::make_unique<PRValue>(
                              this->PointerTo(this->_objectInitialisationStack.back().type),
                              std::unique_ptr<high::ThisExpressionNode, IRDeleter>{
                                   new (*this->GetContext().nodeAllocator)
                                        high::ThisExpressionNode(identifierFunction->Source())},
                              false);
               }
               if (receiver == nullptr)
                    for (const auto& activeContext : this->GetContext().contextSequence | std::views::reverse)
                    {
                         const auto* activeFunction = dynamic_cast<const FunctionContext*>(activeContext.get());
                         if (activeFunction == nullptr || activeFunction->thisType == nullptr) continue;
                         const auto* activePointer = activeFunction->thisType->CastTo<typeSystem::PointerType>();
                         const auto* activeClass = activePointer != nullptr
                                                        ? activePointer->BaseType()->CastTo<typeSystem::ClassType>()
                                                        : nullptr;
                         if (activeClass != nullptr && activeClass->Info().id == candidate->memberClassInfo->id)
                              receiver = std::make_unique<PRValue>(
                                   activeFunction->thisType,
                                   std::unique_ptr<high::ThisExpressionNode, IRDeleter>{
                                        new (*this->GetContext().nodeAllocator)
                                             high::ThisExpressionNode(identifierFunction->Source())},
                                   false);
                         break;
                    }
               if (receiver == nullptr)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "A non-static member function requires an object", identifierFunction->Source()));
                    return nullptr;
               }
               evaluatedArguments.push_back(std::move(receiver));
          }
          for (auto&& pair : std::views::zip(candidate->parameters, moveRange))
          {
               auto&& [parameter, argument] = pair;
               evaluatedArguments.push_back(ConvertTo(std::move(argument), parameter.type));
          }

          auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{
               new (*this->GetContext().nodeAllocator)
                    FunctionCallNode(candidate, std::move(evaluatedArguments), node.Source())};

          return std::make_unique<PRValue>(candidate->returnType, std::move(call), false);
     }

     std::string errorMessage = "Unresolved function " + name;

     std::vector<Expression> argumentsForAnalysis = node.Arguments() |
                                                    std::views::transform(
                                                         [this](const ast::NodePointer& argument)
                                                         {
                                                              return this->ParseExpression(argument);
                                                         }) |
                                                    std::ranges::to<std::vector>();

     auto exactMatches = CollectExactMatches(name, this->GetContext().contextSequence);
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
                    Location argSource =
                         (std::cmp_not_equal(paramIndex, -1) && paramIndex > 0 && paramIndex <= argumentNodes.size())
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

                              if ((ecpps::typeSystem::IsPointer(fromType) || ecpps::typeSystem::IsArray(fromType)) &&
                                  !ecpps::typeSystem::IsPointer(toType) && !ecpps::typeSystem::IsArray(toType))
                                   failureNote->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                                        "note", "Did you mean to dereference the argument?", argSource));
                         }
                    }

                    candidateNote->SubDiagnostics().push_back(std::move(failureNote));
               }

               mainError->SubDiagnostics().push_back(std::move(candidateNote));
          }

          this->GetContext().diagnostics.get().diagnosticsList.push_back(std::move(mainError));
     }
     else
     {
          auto mainError =
               std::make_unique<diagnostics::UnresolvedSymbolError>(name, errorMessage, identifierFunction->Source());

          auto similarNames =
               CollectSimilarNames(name, argumentsForAnalysis.size(), this->GetContext().contextSequence);
          if (!similarNames.empty())
          {
               auto didYouMeanNote =
                    std::make_unique<diagnostics::Information>("note", "Did you mean:", identifierFunction->Source());

               for (const auto& [similarName, distance] : similarNames)
               {
                    didYouMeanNote->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                         "", std::format("    {} [distance={}]", similarName, distance), identifierFunction->Source()));
               }

               mainError->SubDiagnostics().push_back(std::move(didYouMeanNote));
          }

          this->GetContext().diagnostics.get().diagnosticsList.push_back(std::move(mainError));
     }

     return nullptr;
}

Expression ecpps::ir::IR::ParseStringLiteral(const ast::StringLiteralNode& expression) const
{
     const auto length = expression.Value().length();
     const auto* elementType =
          GetTypeContext().Get(TypeRequest{.kind = TypeKind::Fundamental,
                                           .qualifiers = typeSystem::Qualifiers::Const,
                                           .data = StandardSignedIntegerRequest{.isCharWithoutSign = true}});
     std::vector<std::uint32_t> values{};
     values.reserve(length + 1);
     for (const auto character : expression.Value()) values.emplace_back(character);
     values.emplace_back(0u);

     TypeRequest arrayRequest{};
     arrayRequest.kind = TypeKind::Compound;
     arrayRequest.data = BoundedArrayRequest{.elementType = elementType, .size = length + 1};

     const auto* arrayType = GetTypeContext().Get(arrayRequest);

     auto node = std::unique_ptr<IntegerArrayNode, IRDeleter>(new (*this->GetContext().nodeAllocator) IntegerArrayNode(
          std::move(values), elementType->CastTo<typeSystem::IntegralType>(), expression.Source()));
     return std::make_unique<PRValue>(arrayType, std::move(node), true);
}

Expression ecpps::ir::IR::ParseSizeofExpression(const ast::SizeOfNode& expression)
{
     if (expression.CanBeTypeId())
     {
          auto typeRequest = TypeASTToRequest(expression.Value());
          if (typeRequest.kind != TypeKind::Error)
          {
               const auto* type = GetTypeContext().Get(typeRequest);
               if (type == nullptr)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              std::format("Unknown type `{}` in sizeof expression", expression.Value()->ToString(0)),
                              expression.Value()->Source()));
                    return nullptr;
               }

               const auto size = RequireComplete(type, expression.Source(), "a sizeof expression") ? type->Size() : 1;

               TypeRequest sizeTypeRequest{};
               sizeTypeRequest.kind = TypeKind::Fundamental;
               sizeTypeRequest.data = PlatformIntegerRequest{.kind = PlatformIntegerKind::Size};
               const auto* sizeType = GetTypeContext().Get(sizeTypeRequest);
               auto node = std::unique_ptr<IntegralNode, IRDeleter>(new (*this->GetContext().nodeAllocator)
                                                                         IntegralNode(size, expression.Source()));
               return std::make_unique<PRValue>(sizeType, std::move(node), true);
          }
     }
     Expression operand{};
     if (auto* basicTypeNode = dynamic_cast<ast::BasicType*>(expression.Value().get()); basicTypeNode != nullptr)
     {
          const auto& identifierName = basicTypeNode->ToString(0);
          ast::IdentifierNode identifierNode{identifierName, basicTypeNode->Source()};
          operand = ParseIdExpression(identifierNode);
     }
     else
          operand = ParseExpression(expression.Value());

     if (operand == nullptr) return nullptr;

     const auto& operandType = operand->Type();
     if (operandType == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot determine the type of the operand in a sizeof expression", expression.Value()->Source()));
          return nullptr;
     }
     const auto size =
          RequireComplete(operandType, expression.Source(), "a sizeof expression") ? operandType->Size() : 1;
     TypeRequest sizeTypeRequest{};
     sizeTypeRequest.kind = TypeKind::Fundamental;
     sizeTypeRequest.data = PlatformIntegerRequest{.kind = PlatformIntegerKind::Size};
     const auto* sizeType = GetTypeContext().Get(sizeTypeRequest);
     auto node = std::unique_ptr<IntegralNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                               IntegralNode(size, expression.Value()->Source())};
     return std::make_unique<PRValue>(sizeType, std::move(node), true);
}

Expression ecpps::ir::IR::ParseAlignofExpression(const ast::AlignOfNode& expression)
{
     if (expression.CanBeTypeId())
     {
          auto typeRequest = TypeASTToRequest(expression.Value());
          if (typeRequest.kind != TypeKind::Error)
          {
               const auto* type = GetTypeContext().Get(typeRequest);
               if (type == nullptr)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              std::format("Unknown type `{}` in alignof expression", expression.Value()->ToString(0)),
                              expression.Value()->Source()));
                    return nullptr;
               }
               const auto size = type->Alignment();

               TypeRequest sizeTypeRequest{};
               sizeTypeRequest.kind = TypeKind::Fundamental;
               sizeTypeRequest.data = PlatformIntegerRequest{.kind = PlatformIntegerKind::Size};
               const auto* sizeType = GetTypeContext().Get(sizeTypeRequest);
               auto node = std::unique_ptr<IntegralNode, IRDeleter>(new (*this->GetContext().nodeAllocator)
                                                                         IntegralNode(size, expression.Source()));
               return std::make_unique<PRValue>(sizeType, std::move(node), true);
          }
     }
     Expression operand{};
     if (auto* basicTypeNode = dynamic_cast<ast::BasicType*>(expression.Value().get()); basicTypeNode != nullptr)
     {
          const auto& identifierName = basicTypeNode->ToString(0);
          ast::IdentifierNode identifierNode{identifierName, basicTypeNode->Source()};
          operand = ParseIdExpression(identifierNode);
     }
     else
          operand = ParseExpression(expression.Value());

     if (operand == nullptr) return nullptr;

     const auto& operandType = operand->Type();
     if (operandType == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot determine the type of the operand in an alignof expression", expression.Value()->Source()));
          return nullptr;
     }
     const auto size = operandType->Alignment();
     TypeRequest sizeTypeRequest{};
     sizeTypeRequest.kind = TypeKind::Fundamental;
     sizeTypeRequest.data = PlatformIntegerRequest{.kind = PlatformIntegerKind::Size};
     const auto* sizeType = GetTypeContext().Get(sizeTypeRequest);
     auto node = std::unique_ptr<IntegralNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                               IntegralNode(size, expression.Value()->Source())};
     return std::make_unique<PRValue>(sizeType, std::move(node), true);
}

Expression ecpps::ir::IR::ParseThisExpression(const Location& source)
{
     if (!this->_objectInitialisationStack.empty())
     {
          const auto* objectType = this->_objectInitialisationStack.back().type;
          return std::make_unique<PRValue>(this->PointerTo(objectType),
                                           std::unique_ptr<high::ThisExpressionNode, IRDeleter>{new (
                                                *this->GetContext().nodeAllocator) high::ThisExpressionNode(source)},
                                           false);
     }

     for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
     {
          const auto* function = dynamic_cast<const FunctionContext*>(context.get());
          if (function == nullptr || function->thisType == nullptr) continue;
          return std::make_unique<PRValue>(function->thisType,
                                           std::unique_ptr<high::ThisExpressionNode, IRDeleter>{new (
                                                *this->GetContext().nodeAllocator) high::ThisExpressionNode(source)},
                                           false);
     }

     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "'this' is only available in a non-static member function or member initializer", source));
     return nullptr;
}

Expression ecpps::ir::IR::ParseIdExpression(const ast::IdentifierNode& expression)
{
     const std::string& name = expression.Value();

     if (name == "this") return this->ParseThisExpression(expression.Source());

     const auto lookupLocal = [&]() -> Expression
     {
          bool isInnermostContext = true;
          for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
          {
               if (!isInnermostContext && !this->_objectInitialisationStack.empty() &&
                   this->_objectInitialisationStack.back().allowLocalLookup)
                    break;
               isInnermostContext = false;
               const auto* functionScope = dynamic_cast<const FunctionScope*>(&context->GetScope());
               if (functionScope == nullptr) continue;
               for (const auto& local : functionScope->Locals() | std::views::reverse)
               {
                    if (!std::holds_alternative<Variable>(local.local)) continue;
                    const auto& variable = std::get<Variable>(local.local);
                    if (variable.Name().value_or("__unknown_local") != name) continue;
                    if (const auto* referenceType = variable.type->CastTo<typeSystem::ReferenceType>())
                    {
                         auto referenceStorage = std::make_unique<LValue>(
                              PointerTo(referenceType->BaseType()),
                              std::unique_ptr<ReferenceNode, IRDeleter>{
                                   new (*this->GetContext().nodeAllocator) ReferenceNode(
                                        variable.Name().value_or("__unknown_local"), expression.Source())},
                              false);
                         return std::make_unique<LValue>(
                              referenceType->BaseType(),
                              std::unique_ptr<high::DereferenceNode, IRDeleter>{
                                   new (*this->GetContext().nodeAllocator)
                                        high::DereferenceNode(std::move(referenceStorage), expression.Source())},
                              false);
                    }
                    return std::make_unique<LValue>(
                         variable.type,
                         std::unique_ptr<ReferenceNode, IRDeleter>{
                              new (*this->GetContext().nodeAllocator)
                                   ReferenceNode(variable.Name().value_or("__unknown_local"), expression.Source())},
                         false);
               }
          }
          return nullptr;
     };

     if (!this->_objectInitialisationStack.empty() && this->_objectInitialisationStack.back().allowLocalLookup)
          if (auto local = lookupLocal(); local != nullptr) return local;

     if (!this->_objectInitialisationStack.empty())
     {
          const auto& active = this->_objectInitialisationStack.back();
          const auto* classType = active.type->CastTo<typeSystem::ClassType>();
          if (classType != nullptr)
               for (const auto& field : classType->Info().fields)
                    if (!field.name.empty() && field.name == name)
                    {
                         auto objectPointer = std::make_unique<PRValue>(
                              this->PointerTo(active.type),
                              std::unique_ptr<high::ThisExpressionNode, IRDeleter>{new (
                                   *this->GetContext().nodeAllocator) high::ThisExpressionNode(expression.Source())},
                              false);
                         return this->BuildMemberAccess(std::move(objectPointer), classType->Info(), field,
                                                        expression.Source());
                    }
     }
     else
     {
          for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
          {
               const auto* function = dynamic_cast<const FunctionContext*>(context.get());
               if (function == nullptr || function->thisType == nullptr) continue;
               const auto* thisPointer = function->thisType->CastTo<typeSystem::PointerType>();
               const auto* classType =
                    thisPointer != nullptr ? thisPointer->BaseType()->CastTo<typeSystem::ClassType>() : nullptr;
               if (classType == nullptr) break;
               for (const auto& field : classType->Info().fields)
                    if (!field.name.empty() && field.name == name)
                    {
                         auto objectPointer = std::make_unique<PRValue>(
                              function->thisType,
                              std::unique_ptr<high::ThisExpressionNode, IRDeleter>{new (
                                   *this->GetContext().nodeAllocator) high::ThisExpressionNode(expression.Source())},
                              false);
                         return this->BuildMemberAccess(std::move(objectPointer), classType->Info(), field,
                                                        expression.Source());
                    }
               break;
          }
     }

     if (this->_objectInitialisationStack.empty())
          if (auto local = lookupLocal(); local != nullptr) return local;

     std::string errorMessage = "Unresolved identifier " + name;

     auto mainError = std::make_unique<diagnostics::UnresolvedSymbolError>(name, errorMessage, expression.Source());

     auto similarNames = CollectSimilarIdentifierNames(name, this->GetContext().contextSequence);
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

     this->GetContext().diagnostics.get().diagnosticsList.push_back(std::move(mainError));

     return nullptr;
}

Expression ecpps::ir::IR::ParseExpression(const ast::NodePointer& expression)
{
     if (expression == nullptr) return nullptr;

     if (auto* const integerLiteral = dynamic_cast<ast::IntegerLiteralNode*>(expression.get());
         integerLiteral != nullptr)
          return std::make_unique<PRValue>(typeSystem::g_int.get(),
                                           std::unique_ptr<ir::IntegralNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     ir::IntegralNode(integerLiteral->Value(), expression->Source())},
                                           true);

     if (auto* const characterLiteral = dynamic_cast<ast::CharacterLiteralNode*>(expression.get());
         characterLiteral != nullptr)
          return std::make_unique<PRValue>(
               typeSystem::g_char.get(),
               std::unique_ptr<ir::IntegralNode, IRDeleter>{new (*this->GetContext().nodeAllocator) ir::IntegralNode(
                    static_cast<std::uint64_t>(characterLiteral->Value()), expression->Source())},
               true);
     if (auto* const binaryExpression = dynamic_cast<ast::BinaryOperatorNode*>(expression.get());
         binaryExpression != nullptr)
          return this->ParseBinaryExpression(*binaryExpression);
     if (auto* const unaryExpression = dynamic_cast<ast::UnaryOperatorNode*>(expression.get());
         unaryExpression != nullptr)
          return this->ParseUnaryExpression(*unaryExpression);
     if (auto* const functionCall = dynamic_cast<ast::CallOperatorNode*>(expression.get()); functionCall != nullptr)
          return this->ParseCallExpression(*functionCall);
     if (dynamic_cast<ast::ThisNode*>(expression.get()) != nullptr)
          return this->ParseThisExpression(expression->Source());
     if (auto* const identifier = dynamic_cast<ast::IdentifierNode*>(expression.get()); identifier != nullptr)
          return this->ParseIdExpression(*identifier);
     if (auto* const stringLiteral = dynamic_cast<ast::StringLiteralNode*>(expression.get()); stringLiteral != nullptr)
          return this->ParseStringLiteral(*stringLiteral);
     if (auto* const sizeofNode = dynamic_cast<ast::SizeOfNode*>(expression.get()); sizeofNode != nullptr)
          return this->ParseSizeofExpression(*sizeofNode);
     if (auto* const alignofNode = dynamic_cast<ast::AlignOfNode*>(expression.get()); alignofNode != nullptr)
          return this->ParseAlignofExpression(*alignofNode);

     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               expression->ToString(0) + " cannot appear in this context.", expression->Source()));

     return nullptr;
}

Expression ecpps::ir::IR::PickInitialiser(const ast::NodePointer& expression,
                                          typeSystem::NonowningTypePointer desiredType, ast::InitialisationType type)
{
     switch (type)
     {
     case ast::InitialisationType::ParenthesisedExpressionList:
          return this->ParseDirectInitialisation(expression, desiredType);
     case ast::InitialisationType::BracedInitialiser: return this->ParseListInitialisation(expression, desiredType);
     case ast::InitialisationType::EqualInitialiser:
     {
          return this->ParseCopyInitialisation(expression, desiredType);
     }
     case ast::InitialisationType::Default:
     {
          // TODO: Implement
     }
     break;
     default:
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build("Unknown initialisation type",
                                                                               expression->Source()));
          break;
     }
     throw TracedException(std::logic_error("Unknown initialisation type"));
}

Expression ecpps::ir::IR::ParseDirectInitialisation(const ecpps::ast::NodePointer& expression,
                                                    ecpps::typeSystem::NonowningTypePointer desiredType)
{
     auto operand = ParseExpression(expression);
     if (IsBoolean(desiredType))
     {
          if (IsBoolean(operand->Type())) return operand;
     }
     auto converted = ConvertTo(std::move(operand), desiredType);
     if (converted == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot convert initialiser to variable type of '" + desiredType->Name() + "'",
                    expression->Source()));
     }
     return converted;
}

Expression ecpps::ir::IR::ParseCopyInitialisation(const ecpps::ast::NodePointer& expression,
                                                  ecpps::typeSystem::NonowningTypePointer desiredType)
{
     runtime_assert(desiredType != nullptr, "Invalid variable type");
     if (desiredType == nullptr || expression == nullptr) return nullptr;
     if (dynamic_cast<const ast::InitialiserListNode*>(expression.get()) != nullptr)
          return this->ParseListInitialisation(expression, desiredType);
     auto operand = ParseExpression(expression);

     auto converted = ConvertTo(std::move(operand), desiredType);
     if (converted == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot convert initialiser to variable type of '" + desiredType->Name() + "'",
                    expression->Source()));
     }
     return converted;
}

Expression ecpps::ir::IR::ParseZeroInitialisation(typeSystem::NonowningTypePointer desiredType, const Location& source)
{
     if (desiredType == nullptr) return nullptr;
     if (desiredType->CastTo<typeSystem::PointerType>() != nullptr)
     {
          return std::make_unique<PRValue>(desiredType,
                                           std::unique_ptr<IntegralNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator) IntegralNode(0, source)},
                                           true);
     }
     if (IsScalar(desiredType))
     {
          if (IsIntegral(desiredType))
          {
               return std::make_unique<PRValue>(desiredType,
                                                std::unique_ptr<IntegralNode, IRDeleter>{
                                                     new (*this->GetContext().nodeAllocator) IntegralNode(0, source)},
                                                true);
          }
     }
     else if (const auto* const arrayType = desiredType->CastTo<typeSystem::ArrayType>(); arrayType != nullptr)
     {
          const auto* const elementType = arrayType->ElementType()->CastTo<typeSystem::IntegralType>();
          if (elementType != nullptr)
          {
               std::vector<std::uint32_t> values(arrayType->ElementCount(), 0u);
               auto arrayNode = std::unique_ptr<IntegerArrayNode, IRDeleter>{
                    new (*this->GetContext().nodeAllocator) IntegerArrayNode(std::move(values), elementType, source)};
               return std::make_unique<PRValue>(desiredType, std::move(arrayNode), true);
          }
     }
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "Zero initialisation is not supported for this type", source));
     return nullptr;
}

Expression ecpps::ir::IR::ParseValueInitialisation(typeSystem::NonowningTypePointer desiredType, const Location& source)
{
     // TODO: Classes
     if (desiredType == nullptr) return nullptr;
     return ParseZeroInitialisation(desiredType, source);
}

void ecpps::ir::IR::ParseIfStatement(const ast::IfStatementNode& node)
{
     auto condition = ParseCondition(node.Condition());
     if (condition == nullptr) return;

     const auto* conditionReg = LowerExpressionLoaded(std::move(condition), this->_built);
     if (conditionReg == nullptr) return;

     const auto thenLabel = MakeLabelName("if.then");
     const auto endLabel = MakeLabelName("if.end");
     const auto& elseLabel = node.ElseBody().empty() ? endLabel : MakeLabelName("if.else");
     const auto& falseTarget = elseLabel;

     EmitBranch(conditionReg, thenLabel, falseTarget, node.Source());

     EmitLabel(thenLabel, node.Source());
     for (const auto& statement : node.Body()) ParseNode(statement);
     EmitJump(endLabel, node.Source());
     if (elseLabel != endLabel)
     {
          EmitLabel(elseLabel, node.Source());
          for (const auto& statement : node.ElseBody()) ParseNode(statement);
     }

     EmitLabel(endLabel, node.Source());
}

bool ecpps::ir::IR::EmitConditionalBranch(const ast::NodePointer& conditionNode, const std::string& trueLabel,
                                          const std::string& falseLabel, const Location& source)
{
     if (conditionNode == nullptr)
     {
          EmitJump(trueLabel, source);
          return true;
     }

     auto condition = ParseCondition(conditionNode);
     if (condition == nullptr) return false;

     const auto* conditionReg = LowerExpressionLoaded(std::move(condition), this->_built);
     if (conditionReg == nullptr) return false;

     EmitBranch(conditionReg, trueLabel, falseLabel, source);
     return true;
}

void ecpps::ir::IR::ParseWhileStatement(const ast::WhileStatementNode& node)
{
     const auto bodyLabel = MakeLabelName("while.body");
     const auto continueLabel = MakeLabelName("while.cont");
     const auto endLabel = MakeLabelName("while.end");

     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(bodyLabel, node.Source());
     {
          const JumpScope jumpScope{*this, endLabel, continueLabel};
          for (const auto& statement : node.Body()) ParseNode(statement);
     }

     EmitLabel(continueLabel, node.Source());
     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(endLabel, node.Source());
}

void ecpps::ir::IR::ParseForStatement(const ast::ForStatementNode& node)
{
     const auto bodyLabel = MakeLabelName("for.body");
     const auto continueLabel = MakeLabelName("for.cont");
     const auto endLabel = MakeLabelName("for.end");

     ParseNode(node.InitStatement());

     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(bodyLabel, node.Source());
     {
          const JumpScope jumpScope{*this, endLabel, continueLabel};
          for (const auto& statement : node.Body()) ParseNode(statement);
          ParseNode(node.Expression());
     }

     EmitLabel(continueLabel, node.Source());
     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(endLabel, node.Source());
}

void ecpps::ir::IR::ParseDoWhileStatement(const ast::DoWhileStatementNode& node)
{
     const auto bodyLabel = MakeLabelName("do.body");
     const auto continueLabel = MakeLabelName("do.cont");
     const auto endLabel = MakeLabelName("do.end");

     EmitLabel(bodyLabel, node.Source());
     {
          const JumpScope jumpScope{*this, endLabel, continueLabel};
          for (const auto& statement : node.Body()) ParseNode(statement);
     }

     EmitLabel(continueLabel, node.Source());
     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(endLabel, node.Source());
}
void ecpps::ir::IR::ParseLabelNode(const ast::LabelNode& node)
{
     auto& allocator = *this->GetContext().nodeAllocator;

     this->_built.push_back(
          std::unique_ptr<SSALabelNode, IRDeleter>{new (allocator) SSALabelNode(node.Name(), node.Source())});
}
void ecpps::ir::IR::ParseGotoNode(const ast::GotoNode& node)
{
     auto& allocator = *this->GetContext().nodeAllocator;

     this->_built.push_back(
          std::unique_ptr<SSAGotoNode, IRDeleter>{new (allocator) SSAGotoNode(node.Name(), node.Source())});
}
void ecpps::ir::IR::ParseBreakNode(const ast::BreakNode& node)
{
     if (this->_jumpStack.empty())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                    "A 'break' statement may only appear inside a loop or a switch statement", node.Source()));
          return;
     }

     EmitJump(this->_jumpStack.back().breakLabel, node.Source());
}
void ecpps::ir::IR::ParseContinueNode(const ast::ContinueNode& node)
{
     for (const auto& targets : this->_jumpStack | std::views::reverse)
     {
          if (!targets.continueLabel.has_value()) continue;

          EmitJump(*targets.continueLabel, node.Source());
          return;
     }

     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
               "A 'continue' statement may only appear inside a loop", node.Source()));
}

Expression ecpps::ir::IR::ParseCondition(const ast::NodePointer& conditionNode)
{
     auto condition = ParseExpression(conditionNode);
     if (condition == nullptr) return nullptr;

     if (dynamic_cast<const high::CompareNode*>(condition->Value().get()) != nullptr) return condition;

     if (!IsScalar(condition->Type()))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot contextually convert " + condition->Type()->Name() + " to bool",
                    condition->Value()->Source()));
          return nullptr;
     }

     const auto source = condition->Value()->Source();
     return MakeNonZeroTest(std::move(condition), source);
}

std::string ecpps::ir::IR::MakeLabelName(const std::string_view hint)
{
     auto* const function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Labels can only be created inside a function");
     if (function == nullptr) throw TracedException("Invalid function");

     return std::format(".L{}.{}.{}", function->GetScope<FunctionScope>().Name().value_or("__unknown"), hint,
                        this->_nextLabelIndex++);
}

void ecpps::ir::IR::EmitLabel(std::string name, const Location& source)
{
     auto& allocator = *this->GetContext().nodeAllocator;
     this->_built.push_back(
          std::unique_ptr<SSALabelNode, IRDeleter>{new (allocator) SSALabelNode(std::move(name), source)});
}

void ecpps::ir::IR::EmitJump(std::string target, const Location& source)
{
     auto& allocator = *this->GetContext().nodeAllocator;
     this->_built.push_back(
          std::unique_ptr<SSAGotoNode, IRDeleter>{new (allocator) SSAGotoNode(std::move(target), source)});
}

void ecpps::ir::IR::EmitBranch(const SingleAssignRegisterNode* condition, std::string trueLabel, std::string falseLabel,
                               const Location& source)
{
     auto& allocator = *this->GetContext().nodeAllocator;
     this->_built.push_back(std::unique_ptr<SSABranchNode, IRDeleter>{
          new (allocator) SSABranchNode(condition, std::move(trueLabel), std::move(falseLabel), source)});
}

Expression ecpps::ir::IR::ParseListInitialisation(const ast::NodePointer& expression,
                                                  typeSystem::NonowningTypePointer desiredType)
{
     if (expression == nullptr || desiredType == nullptr) return nullptr;
     const auto* initialiserListNode = dynamic_cast<const ast::InitialiserListNode*>(expression.get());
     if (initialiserListNode == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Expected an initialiser list for list initialisation", expression->Source()));
          return nullptr;
     }

     // TODO: Check if aggregate
     if (IsClass(desiredType) /* && IsAggregate(desiredType) */)
     {
          return ParseAggregateInitialisation(expression, desiredType);
     }
     std::vector<Expression> initialisers{};
     for (const auto& init : initialiserListNode->Initialisers())
     {
          auto parsedInit = ParseExpression(init);
          if (parsedInit == nullptr) return nullptr;
          initialisers.push_back(std::move(parsedInit));
     }
     if (initialisers.empty())
     {
          return ParseValueInitialisation(desiredType, expression->Source());
     }

     if (const auto* const arrayType = desiredType->CastTo<typeSystem::ArrayType>(); arrayType != nullptr)
     {
          const auto* const elementType = arrayType->ElementType()->CastTo<typeSystem::IntegralType>();
          if (elementType == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "List initialisation is only supported for arrays of integral types", expression->Source()));
               return nullptr;
          }

          if (initialisers.size() > arrayType->ElementCount())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Cannot initialise an array with more elements than it can hold. Provided {} "
                                     "elements for an array of `{}` `{}`s",
                                     initialisers.size(), arrayType->ElementCount(),
                                     arrayType->ElementType()->RawName()),
                         expression->Source()));
               return nullptr;
          }

          std::vector<std::uint32_t> values{};
          values.reserve(arrayType->ElementCount());
          for (const auto& initialiser : initialisers)
          {
               if (IsNarrowingConversion(initialiser, arrayType->ElementType()))
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              std::format("Narrowing conversion from '{}' to '{}' is not allowed in array "
                                          "aggregate initialization",
                                          initialiser->Type()->Name(), arrayType->ElementType()->Name()),
                              initialiser->Value()->Source()));
                    return nullptr;
               }
               const auto constant = initialiser->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
               if (!constant.has_value() || !std::holds_alternative<std::uint64_t>(constant->variant))
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Array elements must currently be initialised with constant integers",
                              initialiser->Value()->Source()));
                    return nullptr;
               }
               values.push_back(static_cast<std::uint32_t>(std::get<std::uint64_t>(constant->variant)));
          }
          values.resize(arrayType->ElementCount(), 0u);

          auto arrayNode = std::unique_ptr<IntegerArrayNode, IRDeleter>{
               new (*this->GetContext().nodeAllocator)
                    IntegerArrayNode(std::move(values), elementType, expression->Source())};
          return std::make_unique<PRValue>(desiredType, std::move(arrayNode), true);
     }

     if (initialisers.size() == 1 && !IsReference(desiredType))
     {
          auto initialiserExpression = std::move(initialisers.front());
          if (IsNarrowingConversion(initialiserExpression, desiredType))
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Narrowing conversion from '" + initialiserExpression->Type()->Name() + "' to '" +
                              desiredType->Name() + "' is not allowed in list initialisation",
                         expression->Source()));
               return nullptr;
          }
          return ConvertTo(std::move(initialiserExpression), desiredType);
     }
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "List initialisation with multiple initialisers is not supported yet", expression->Source()));
     return nullptr;
}

[[nodiscard]] ecpps::ir::TypeRequest ecpps::ir::IR::TypeASTToRequest(const ast::NodePointer& type)
{
     auto& typeContext = GetTypeContext();

     const ast::Node* base = type.get();
     ecpps::ir::TypeRequest request{};

     if (const auto* basicType = dynamic_cast<const ast::BasicType*>(base); basicType != nullptr)
     {
          const auto& value = basicType->Value();

          const auto qualifiers = QualifiersFrom(basicType->IsConst(), basicType->IsVolatile());
          const auto lookup = LookupTypeName(value);
          switch (lookup.kind)
          {
          case TypeNameLookup::Kind::Class:
               request.kind = TypeKind::Compound;
               request.qualifiers = qualifiers;
               request.data = ClassRequest{.id = lookup.classId};
               return request;

          case TypeNameLookup::Kind::NonType:
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "'" + value + "' is not a type name", basicType->Source()));
               return ErrorRequest();

          case TypeNameLookup::Kind::Alias:
          {
               const auto* targetType = lookup.alias;
               if (const auto* classType = targetType->CastTo<typeSystem::ClassType>())
               {
                    request.kind = TypeKind::Compound;
                    request.qualifiers = QualifiersFrom(basicType->IsConst() || classType->IsConst(),
                                                        basicType->IsVolatile() || classType->IsVolatile());
                    request.data = ClassRequest{.id = classType->Info().id};
                    return request;
               }
               request.qualifiers = qualifiers;

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
                    throw TracedException("Type aliases for this kind of type not yet implemented");

               if (basicType->IsConst() && basicType->IsVolatile())
                    request.qualifiers = typeSystem::Qualifiers::ConstVolatile;
               else if (basicType->IsConst())
                    request.qualifiers = typeSystem::Qualifiers::Const;
               else if (basicType->IsVolatile())
                    request.qualifiers = typeSystem::Qualifiers::Volatile;

               return request;
          }
          case TypeNameLookup::Kind::NotFound: break;
          }

          request.kind = TypeKind::Fundamental;
          request.qualifiers = qualifiers;

          const auto fail = [&](std::string message)
          {
               std::vector<diagnostics::DiagnosticsMessage> diagnosticsList{};
               diagnosticsList.push_back(diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::move(message), basicType->Source()));
               return ErrorRequest(std::move(diagnosticsList));
          };

          if (value == "void")
          {
               request.data = VoidRequest{};
               return request;
          }
          if (value == "bool")
          {
               request.data = BooleanRequest{};
               return request;
          }

          std::vector<std::string_view> tokens;
          std::string_view sv{value};
          while (!sv.empty())
          {
               const auto pos = sv.find(' ');
               tokens.emplace_back(sv.substr(0, pos));
               if (pos == std::string_view::npos) break;
               sv.remove_prefix(pos + 1);
          }
          using std::operator""sv;

          constexpr std::array integerKeywords{"char"sv, "short"sv, "int"sv, "long"sv, "signed"sv, "unsigned"sv};
          constexpr std::array unsupportedKeywords{"float"sv,   "double"sv,   "wchar_t"sv,
                                                   "char8_t"sv, "char16_t"sv, "char32_t"sv};
          for (const auto token : tokens)
          {
               if (std::ranges::contains(unsupportedKeywords, token))
                    return fail(std::format("The type '{}' is not supported yet", token));
               if (!std::ranges::contains(integerKeywords, token))
                    return fail(tokens.size() == 1 ? std::format("Unknown type name '{}'", value)
                                                   : std::format("Invalid type specifier '{}'", value));
          }
          if (!IsValidIntegerSpecifierCombination(tokens))
               return fail(std::format("Invalid type specifier '{}'", value));

          StandardSignedIntegerRequest signedRequest{};
          const auto has = [&](const std::string_view t)
          {
               return std::ranges::contains(tokens, t);
          };
          const auto longCount = std::ranges::count(tokens, "long"sv);
          const bool isChar = has("char");
          const bool isShort = has("short");
          const bool isSigned = has("signed");
          const bool isUnsigned = has("unsigned");

          if (isChar && !isSigned && !isUnsigned) signedRequest.isCharWithoutSign = true;
          else
          {
               signedRequest.signedness =
                    isUnsigned ? typeSystem::Signedness::Unsigned : typeSystem::Signedness::Signed;
               if (isChar) signedRequest.size = typeSystem::TypeKind::Char;
               else if (isShort)
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

          request.data = PointerRequest{.elementType = baseType};
          return request;
     }
     if (const auto* qualifiedType = dynamic_cast<const ast::QualifiedType*>(base); qualifiedType != nullptr)
     {
          if (qualifiedType->Sections().Size() == 0)
          {
               const auto* unqualified = qualifiedType->UnqualifiedType().get();
               if (dynamic_cast<const ast::BasicType*>(unqualified))
                    return TypeASTToRequest(qualifiedType->UnqualifiedType());
          }

          throw TracedException("Qualified types with namespaces not yet implemented");
     }
     if (const auto* classType = dynamic_cast<const ast::ClassType*>(base); classType != nullptr)
     {
          const auto id = dynamic_cast<const ast::ClassDefNode*>(classType) != nullptr
                               ? DefineClass(static_cast<const ast::ClassDefNode&>(*classType)) // NOLINT
                               : ResolveElaboratedClass(*classType);
          if (id == typeSystem::InvalidClassId) return ErrorRequest();
          request.kind = TypeKind::Compound;
          request.qualifiers = QualifiersFrom(classType->IsConst(), classType->IsVolatile());
          request.data = ClassRequest{.id = id};
          return request;
     }

     throw TracedException("not implemented");
}

ecpps::typeSystem::NonowningTypePointer ecpps::ir::IR::ParseType(const ast::NodePointer& type)
{
     auto& typeContext = GetTypeContext();
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
                    ConstantEvaluationResultToExpression(value, expression->Type(), *this->GetContext().nodeAllocator);
          }
          catch (...)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::ConstantEvaluationWarning>(
                         "Failed to use the constant expression evaluation result", expression->Value()->Source()));
          }
     }

     const auto comparison = toType->CompareTo(expression->Type());

     if (!comparison.IsValid())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
          if (conversion == typeSystem::ConversionSequence::ConversionKind::BooleanConversion)
          {
               const auto source = expression->Value()->Source();
               return MakeNonZeroTest(std::move(expression), source);
          }
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
                                                                                "[conv.array]");

               const auto wasConstexpr = expression->IsConstantExpression();
               if (expression->IsPRValue())
               {
                    NodePointer decayNode{};

                    if (auto* const intArray = dynamic_cast<IntegerArrayNode*>(expression->Value().get()))
                    {
                         const auto source = intArray->Source();

                         decayNode = std::unique_ptr<TemporaryIntegerArrayDecayNode, IRDeleter>(
                              new (*this->GetContext().nodeAllocator)
                                   TemporaryIntegerArrayDecayNode(std::move(expression), source));
                    }
                    else
                         throw TracedException("array-to-pointer is not supported yet");

                    return std::make_unique<PRValue>(pointerType, std::move(decayNode), wasConstexpr);
               }

               if (expression->IsLValue())
               {
                    std::string variableName{};
                    if (const auto* const referenceNode = dynamic_cast<const ReferenceNode*>(expression->Value().get()))
                         variableName = referenceNode->Address();
                    else if (const auto* const loadNode = dynamic_cast<const LoadNode*>(expression->Value().get()))
                         variableName = loadNode->Address();

                    const auto source = expression->Value()->Source();
                    if (!variableName.empty())
                    {
                         const auto* const allocReg = ResolveAllocReg(variableName);
                         if (allocReg == nullptr)
                         {
                              this->GetContext().diagnostics.get().diagnosticsList.push_back(
                                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                        "Cannot resolve allocation register for variable '" + variableName + "'",
                                        source));
                              return nullptr;
                         }

                         auto& allocator = *this->GetContext().nodeAllocator;
                         auto registerReference =
                              std::make_unique<LValue>(expression->Type(),
                                                       std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                                            new (allocator) SSARegisterReferenceNode(allocReg, source)},
                                                       false);

                         NodePointer decayNode = std::unique_ptr<LoadArrayDecayNode, IRDeleter>(
                              new (allocator) LoadArrayDecayNode(std::move(registerReference), source));

                         return std::make_unique<PRValue>(pointerType, std::move(decayNode), wasConstexpr);
                    }

                    auto arrayAddress = this->ParseAddressOfExpression(std::move(expression), source);
                    if (arrayAddress == nullptr) return nullptr;
                    return std::make_unique<PRValue>(
                         pointerType,
                         std::unique_ptr<high::PointerConversionNode, IRDeleter>{
                              new (*this->GetContext().nodeAllocator)
                                   high::PointerConversionNode(std::move(arrayAddress), pointerType, source)},
                         wasConstexpr);
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

               auto castNode = std::unique_ptr<high::PointerConversionNode, IRDeleter>(
                    new (*this->GetContext().nodeAllocator)
                         high::PointerConversionNode(std::move(expression), toType, source));

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
          return std::make_unique<PRValue>(type, std::move(std::move(*expression).Value()),
                                           expression->IsConstantExpression());
     }
     if (IsArithmetic(expressionType))
          return std::make_unique<PRValue>(type,
                                           std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::ConvertNode(std::move(expression), type, source)},
                                           false);
     return nullptr;
}

ecpps::ir::MatchingScore ecpps::ir::IR::MatchFunction(const std::shared_ptr<FunctionScope>& function,
                                                      const std::vector<Expression>& arguments)
{
     if (arguments.size() != function->parameters.size()) return MatchingScore::NotMatching;

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

          const bool valid = seq.IsValid() && refKind != ImplicitConversion::RefBindingKind::IllFormed;
          if (!valid) return MatchingScore::NotMatching;

          const ImplicitConversion conversion{std::move(seq), refKind, true};

          totalScore += conversion.Rank();
     }

     return totalScore;
}

ecpps::ir::MatchingScore ecpps::ir::ImplicitConversion::Rank(void) const noexcept
{
     if (!this->isValid || !this->typeSequence.IsValid()) return MatchingScore::MaxScore;

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
     case RefBindingKind::IllFormed: return MatchingScore::MaxScore;
     }

     return cost;
}

ecpps::ir::ImplicitConversion ecpps::ir::MatchImplicitConversion(const Expression& expression,
                                                                 typeSystem::NonowningTypePointer type)
{
     ImplicitConversion::RefBindingKind referenceKind{};
     referenceKind = expression->IsPRValue() ? ImplicitConversion::RefBindingKind::BindToTemporary
                                             : ImplicitConversion::RefBindingKind::None;

     return ImplicitConversion{type->CompareTo(expression->Type()), referenceKind, true};
}
void ecpps::ir::CreateReferenceMap(abstract::VirtualRegisterMap& map, const std::vector<NodePointer>& irNodes)
{
     for (const auto& node : irNodes)
     {
          if (node == nullptr) continue;

          switch (node->Kind())
          {
          case ecpps::ir::NodeKind::Allocate: break;
          case ecpps::ir::NodeKind::Procedure:
          {
               const auto* procedureNode = dynamic_cast<const ProcedureNode*>(node.get());
               runtime_assert(procedureNode != nullptr, "Invalid procedure node");

               CreateReferenceMap(map, procedureNode->Body());
               break;
          }

          case ecpps::ir::NodeKind::Addition:
          {
               const auto* innerNode = dynamic_cast<const SSAAddNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid addition node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::Subtraction:
          {
               const auto* innerNode = dynamic_cast<const SSASubNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid subtraction node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::Multiplication:
          {
               const auto* innerNode = dynamic_cast<const SSAMulNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid multiplication node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::Division:
          {
               const auto* innerNode = dynamic_cast<const SSADivNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid division node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::Modulo:
          {
               const auto* innerNode = dynamic_cast<const SSAModNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid modulo node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::LeftBitShift:
          {
               const auto* innerNode = dynamic_cast<const SSALeftShiftNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid left-shift node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::RightBitShift:
          {
               const auto* innerNode = dynamic_cast<const SSARightShiftNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid right-shift node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::Or:
          {
               const auto* innerNode = dynamic_cast<const SSABinOrNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid OR node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::And:
          {
               const auto* innerNode = dynamic_cast<const SSABinAndNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid AND node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::Xor:
          {
               const auto* innerNode = dynamic_cast<const SSABinXorNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid XOR node");

               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }

          case ecpps::ir::NodeKind::BitwiseNot:
          {
               const auto* innerNode = dynamic_cast<const SSABitwiseNotNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid bitwise-not node");

               map.ReferenceRegister(innerNode->Operand().Index());
               break;
          }

          case ecpps::ir::NodeKind::ArithmeticNegation:
          {
               const auto* innerNode = dynamic_cast<const SSAArithmeticNegationNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid arithmetic-negation node");

               map.ReferenceRegister(innerNode->Operand().Index());
               break;
          }

          case ecpps::ir::NodeKind::Convert:
          {
               const auto* innerNode = dynamic_cast<const SSAConvertNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid conversion node");

               map.ReferenceRegister(innerNode->Src().Index());
               break;
          }

          case ecpps::ir::NodeKind::PointerConversion:
          {
               if (const auto* innerNode = dynamic_cast<const SSAPointerConvertNode*>(node.get()))
               {
                    map.ReferenceRegister(innerNode->Src().Index());
                    break;
               }

               if (const auto* innerNode = dynamic_cast<const SSAPointerConvertFromDecayNode*>(node.get()))
               {
                    if (const auto* loadDecayNode = dynamic_cast<const LoadArrayDecayNode*>(&innerNode->DecayNode());
                        loadDecayNode != nullptr && loadDecayNode->GetAllocReg() != nullptr)
                         map.ReferenceRegister(loadDecayNode->GetAllocReg()->Index());
                    break;
               }

               runtime_assert(false, "Invalid pointer-conversion node");
               break;
          }

          case ecpps::ir::NodeKind::Store:
          {
               if (const auto* innerNode = dynamic_cast<const SSAStoreIntegerNode*>(node.get()))
               {
                    map.ReferenceRegister(innerNode->Target().Index());
                    break;
               }

               if (const auto* innerNode = dynamic_cast<const SSAStoreNode*>(node.get()))
               {
                    map.ReferenceRegister(innerNode->Target().Index());
                    map.ReferenceRegister(innerNode->Src().Index());
                    break;
               }

               if (const auto* innerNode = dynamic_cast<const SSAArrayStoreNode*>(node.get()))
               {
                    map.ReferenceRegister(innerNode->Target().Index());
                    break;
               }
               if (const auto* innerNode = dynamic_cast<const SSAIndirectStoreNode*>(node.get()))
               {
                    map.ReferenceRegister(innerNode->Target().Index());
                    map.ReferenceRegister(innerNode->Src().Index());
                    break;
               }

               runtime_assert(false, "Invalid store node");
               break;
          }

          case ecpps::ir::NodeKind::Load:
          {
               if (const auto* innerNode = dynamic_cast<const SSARegisterReferenceNode*>(node.get()))
               {
                    map.ReferenceRegister(innerNode->Reg().Index());
                    break;
               }

               if (const auto* innerNode = dynamic_cast<const SSALoadNode*>(node.get()))
               {
                    map.ReferenceRegister(innerNode->Address().Index());
                    break;
               }

               if (dynamic_cast<const LoadNode*>(node.get()) != nullptr) break;

               runtime_assert(false, "Invalid load node");
               break;
          }

          case ecpps::ir::NodeKind::AddressOf:
          {
               const auto* innerNode = dynamic_cast<const SSAAddressOfNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid address-of node");

               map.ReferenceRegister(innerNode->Result().Index());
               map.ReferenceRegister(innerNode->Operand().Index());
               break;
          }

          case ecpps::ir::NodeKind::Dereference:
          {
               const auto* innerNode = dynamic_cast<const SSADerefNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid dereference node");

               map.ReferenceRegister(innerNode->Ptr().Index());
               break;
          }

          case ecpps::ir::NodeKind::Call:
          {
               const auto* innerNode = dynamic_cast<const SSACallNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid call node");

               for (const auto* argument : innerNode->Arguments())
               {
                    if (argument == nullptr) throw TracedException("Invalid call argument");
                    map.ReferenceRegister(argument->Index());
               }

               break;
          }

          case ecpps::ir::NodeKind::Return:
          {
               const auto* innerNode = dynamic_cast<const SSAReturnNode*>(node.get());

               if (innerNode != nullptr && innerNode->HasOperand())
                    map.ReferenceRegister(innerNode->Operand()->Index());

               break;
          }

          case ecpps::ir::NodeKind::Integer:
          {
               break;
          }

          case ecpps::ir::NodeKind::Reference:
          {
               break;
          }

          case ecpps::ir::NodeKind::IncomingParameter:
          {
               break;
          }

          case ecpps::ir::NodeKind::IntegerArrayDecay:
          {
               break;
          }

          case ecpps::ir::NodeKind::LoadArrayDecay:
          {
               const auto* innerNode = dynamic_cast<const LoadArrayDecayNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid load-array-decay node");

               if (const auto* reg = innerNode->GetAllocReg()) map.ReferenceRegister(reg->Index());

               break;
          }

          case ecpps::ir::NodeKind::CompareExchange:
          {
               break;
          }
          case ecpps::ir::NodeKind::Compare:
          {
               const auto* innerNode = dynamic_cast<const SSACompareNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid compare node");
               map.ReferenceRegister(innerNode->Left().Index());
               map.ReferenceRegister(innerNode->Right().Index());
               break;
          }
          case ecpps::ir::NodeKind::Branch:
          {
               const auto* innerNode = dynamic_cast<const SSABranchNode*>(node.get());
               runtime_assert(innerNode != nullptr, "Invalid branch node");
               map.ReferenceRegister(innerNode->Condition().Index());
               break;
          }
          default: break;
          }
     }
}

Expression ecpps::ir::IR::MakeNonZeroTest(Expression operand, const Location& source) const
{
     auto& allocator = *this->GetContext().nodeAllocator;
     const auto* operandType = operand->Type();
     const bool wasConstexpr = operand->IsConstantExpression();

     TypeRequest boolRequest{.kind = TypeKind::Fundamental, .data = BooleanRequest{}};
     const auto* boolType = GetTypeContext().Get(boolRequest);

     auto zero = std::make_unique<PRValue>(
          operandType, std::unique_ptr<IntegralNode, IRDeleter>{new (allocator) IntegralNode(0, source)}, true);

     auto compare = std::unique_ptr<high::CompareNode, IRDeleter>{new (allocator) high::CompareNode(
          std::move(operand), std::move(zero), ComparisonPredicate::NotEqual, source)};

     return std::make_unique<PRValue>(boolType, std::move(compare), wasConstexpr);
}

std::optional<ecpps::ir::IR::CommonOperands> ecpps::ir::IR::UsualArithmeticConversions(Expression left,
                                                                                       Expression right) const
{
     const auto* leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     const auto* rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform this binary operation on " + left->Type()->Name() + " and " + right->Type()->Name(),
                    left->Value()->Source()));
          return std::nullopt;
     }

     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto* commonType = leftIntegral->CommonWith(rightIntegral)->CastTo<typeSystem::IntegralType>();
     if (commonType == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot find a common integral type between " + left->Type()->Name() + " and " +
                         right->Type()->Name(),
                    left->Value()->Source()));
          return std::nullopt;
     }

     const auto convert = [&](Expression operand) -> Expression
     {
          if (operand->Type() == commonType) return operand;

          const auto innerSource = operand->Value()->Source();
          const auto wasConstexpr = operand->IsConstantExpression();
          return std::make_unique<PRValue>(commonType,
                                           std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::ConvertNode(std::move(operand), commonType, innerSource)},
                                           wasConstexpr);
     };

     return CommonOperands{.left = convert(std::move(left)), .right = convert(std::move(right)), .type = commonType};
}
static ecpps::ir::ComparisonPredicate SelectPredicate(const ecpps::ast::Operator operator_, const bool isSigned)
{
     using enum ecpps::ir::ComparisonPredicate;
     using ecpps::ast::Operator;
     switch (operator_)
     {
     case Operator::EqualsSign: return Equal;
     case Operator::NotEqual: return NotEqual;
     case Operator::Less: return isSigned ? SignedLess : UnsignedLess;
     case Operator::LessEqual: return isSigned ? SignedLessEqual : UnsignedLessEqual;
     case Operator::Greater: return isSigned ? SignedGreater : UnsignedGreater;
     case Operator::GreaterEqual: return isSigned ? SignedGreaterEqual : UnsignedGreaterEqual;
     default: throw TracedException(std::logic_error("Invalid relational operator"));
     }
}

Expression ecpps::ir::IR::ParseRelationalExpression(Expression left, const ast::Operator operator_, Expression right,
                                                    const Location& source) const
{
     const auto* leftPointer = left->Type()->CastTo<typeSystem::PointerType>();
     const auto* rightPointer = right->Type()->CastTo<typeSystem::PointerType>();

     bool isSigned = false;
     if (leftPointer != nullptr && rightPointer != nullptr)
     {
          if (leftPointer->BaseType() != rightPointer->BaseType())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot compare pointers to different types (" + left->Type()->Name() + " and " +
                              right->Type()->Name() + ")",
                         source));
               return nullptr;
          }
     }
     else
     {
          auto operands = UsualArithmeticConversions(std::move(left), std::move(right));
          if (!operands) return nullptr;

          isSigned = operands->type->Sign() == typeSystem::Signedness::Signed;
          left = std::move(operands->left);
          right = std::move(operands->right);
     }

     TypeRequest boolRequest{.kind = TypeKind::Fundamental, .data = BooleanRequest{}};
     const auto* boolType = GetTypeContext().Get(boolRequest);

     return std::make_unique<PRValue>(
          boolType,
          std::unique_ptr<high::CompareNode, IRDeleter>{new (*this->GetContext().nodeAllocator) high::CompareNode(
               std::move(left), std::move(right), SelectPredicate(operator_, isSigned), source)},
          false);
}

ecpps::ir::IR::TypeNameLookup ecpps::ir::IR::LookupTypeName(const std::string& name) const
{
     for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
     {
          const auto& scope = context->GetScope();
          if (DeclaresNonType(scope, name)) return {.kind = TypeNameLookup::Kind::NonType};
          if (const auto it = scope.typeAliases.find(name); it != scope.typeAliases.end())
               return {.kind = TypeNameLookup::Kind::Alias, .alias = it->second};
          if (const auto it = scope.classes.find(name); it != scope.classes.end())
               return {.kind = TypeNameLookup::Kind::Class, .classId = it->second};
     }
     return {};
}

std::optional<ecpps::typeSystem::ClassId> ecpps::ir::IR::LookupClassForElaboration(const std::string& name,
                                                                                   const Location& source)
{
     for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
     {
          const auto& scope = context->GetScope();
          if (const auto it = scope.classes.find(name); it != scope.classes.end()) return it->second;
          if (scope.typeAliases.contains(name))
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("'{}' names a typedef, not a class; it cannot be used in an elaborated "
                                     "type specifier",
                                     name),
                         source));
               return typeSystem::InvalidClassId;
          }
     }
     return std::nullopt;
}
ecpps::ir::Scope& ecpps::ir::IR::NearestNamespaceOrBlockScope(void) const
{
     for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
          if (dynamic_cast<const ClassContext*>(context.get()) == nullptr) return context->GetScope();
     throw TracedException("No enclosing namespace scope");
}

std::vector<ecpps::typeSystem::ScopeSegment> ecpps::ir::IR::EnclosingPathUpTo(const Scope& target) const
{
     std::vector<typeSystem::ScopeSegment> path{};
     for (const auto& context : this->GetContext().contextSequence)
     {
          const auto& scope = context->GetScope();
          if (const auto* ns = dynamic_cast<const NamespaceScope*>(&scope); ns && ns->Name().has_value())
               path.push_back({typeSystem::ScopeKind::Namespace, *ns->Name()}); // TODO: unnamed namespaces are skipped
          else if (const auto* cs = dynamic_cast<const ClassScope*>(&scope); cs && !cs->name.empty())
               path.push_back({typeSystem::ScopeKind::Class, cs->name});
          else if (const auto* fn = dynamic_cast<const FunctionScope*>(&scope))
               path.push_back({typeSystem::ScopeKind::Function, fn->Name().value_or("__unknown")}); // local class
          if (&scope == &target) break;
     }
     return path;
}

void ecpps::ir::IR::CheckClassKey(const typeSystem::ClassInfo& info, const ast::ClassType& node)
{
     const auto key = ToClassKind(node.Key());
     if (key == info.kind) return;
     const bool unionMismatch = (key == typeSystem::ClassKind::Union) != (info.kind == typeSystem::ClassKind::Union);
     if (unionMismatch)
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("'{}' was declared as a {} but is redeclared as a {}", info.DisplayName(),
                                info.Keyword(), node.ToString(0)),
                    node.Source()));
     else // TODO: Check if MS ABI, since that is ABI violating and shall reject the program
          this->GetContext().diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::Warning>(
               "warning",
               std::format("'{}' was declared as a {} but is redeclared with a different class-key", info.DisplayName(),
                           info.Keyword()),
               node.Source()));
}

ecpps::typeSystem::ClassId ecpps::ir::IR::DeclareClassInScope(Scope& scope, const ast::ClassType& node)
{
     typeSystem::ClassDeclaration declaration{};
     declaration.kind = ToClassKind(node.Key());
     declaration.name = node.Value();
     declaration.enclosingPath = EnclosingPathUpTo(scope);
     declaration.isLocal = dynamic_cast<const FunctionScope*>(&scope) != nullptr;
     declaration.source = node.Source();

     const auto id = GetTypeContext().DeclareClass(std::move(declaration));
     if (!node.Value().empty()) scope.classes.emplace(node.Value(), id);
     return id;
}

ecpps::typeSystem::ClassId ecpps::ir::IR::ResolveElaboratedClass(const ast::ClassType& node)
{
     if (node.Value().empty())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                    "An elaborated type specifier requires a class name", node.Source()));
          return typeSystem::InvalidClassId;
     }
     if (const auto found = LookupClassForElaboration(node.Value(), node.Source()); found.has_value())
     {
          if (*found != typeSystem::InvalidClassId) CheckClassKey(GetTypeContext().Class(*found), node);
          return *found;
     }
     return DeclareClassInScope(NearestNamespaceOrBlockScope(), node);
     // TODO: the `friend class X;` form has other rules ([namespace.memdef]/3). Just.. eh
}

ecpps::typeSystem::ClassId ecpps::ir::IR::DefineClass(const ast::ClassDefNode& node)
{
     auto& types = GetTypeContext();
     auto& scope = this->GetContext().contextSequence.back()->GetScope();

     typeSystem::ClassId id{};
     if (const auto it = scope.classes.find(node.Value()); !node.Value().empty() && it != scope.classes.end())
     {
          id = it->second;
          CheckClassKey(types.Class(id), node);
          if (types.Class(id).state != typeSystem::ClassState::Declared)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Redefinition of '{}'", types.Class(id).DisplayName()), node.Source()));
               return id;
          }
     }
     else
          id = DeclareClassInScope(scope, node);

     types.BeginDefinition(id);

     auto classScope = std::make_unique<ClassScope>();
     classScope->name = node.Value();
     classScope->id = id;
     classScope->parentScope = &scope;
     if (!node.Value().empty()) classScope->classes.emplace(node.Value(), id); // injected-class-name
     auto* rawScope = classScope.get();
     scope.classScopes.push_back(std::move(classScope));
     this->GetContext().classTable->classes[id].scope = rawScope;

     this->GetContext().contextSequence.push_back(std::make_shared<ClassContext>(rawScope, id));

     auto access = node.Key() == ast::ClassKey::Class ? typeSystem::Access::Private : typeSystem::Access::Public;
     std::vector<typeSystem::ClassField> fields{};
     std::vector<std::function<void()>> deferred{};
     for (const auto& member : node.MemberSpecifiers()) ParseClassMember(member, id, access, fields, deferred);

     types.Complete(id, std::move(fields));

     for (const auto& task : deferred) task();

     this->GetContext().contextSequence.pop_back();
     return id;
}

void ecpps::ir::IR::ParseClassMember(const ast::NodePointer& member, const typeSystem::ClassId id,
                                     typeSystem::Access& access, std::vector<typeSystem::ClassField>& fields,
                                     std::vector<std::function<void()>>& deferred)
{
     // auto& classScope = this->GetContext().contextSequence.back()->GetScope<ClassScope>();

     if (const auto* spec = dynamic_cast<const ast::AccessSpecifierNode*>(member.get()))
     {
          access = ToAccess(spec->Value());
          return;
     }
     if (const auto* declaration = dynamic_cast<const ast::MemberDeclarationNode*>(member.get()))
     {
          ParseMemberDeclaration(*declaration, id, access, fields, deferred);
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
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("A class declaration requires a name",
                                                                                 node.Source()));
          return;
     }
     if (const auto it = scope.classes.find(node.Value()); it != scope.classes.end())
     {
          CheckClassKey(GetTypeContext().Class(it->second), node);
          return;
     }
     DeclareClassInScope(scope, node);
}
bool ecpps::ir::IR::RequireComplete(const typeSystem::NonowningTypePointer type, const Location& source,
                                    const std::string_view what) const
{
     if (type == nullptr || type->IsComplete()) return true;
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               std::format("Invalid use of incomplete type '{}' in {}", type->Name(), what), source));
     return false;
}

std::optional<std::uint64_t> ecpps::ir::IR::EvaluateArrayBound(const ast::NodePointer& bound, const std::string& name)
{
     auto& list = this->GetContext().diagnostics.get().diagnosticsList;

     const auto expression = ParseExpression(bound);
     if (expression == nullptr) return std::nullopt;

     auto constant = expression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
     if (!constant.has_value())
     {
          list.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("The bound of array '{}' must be a constant expression", name), bound->Source()));
          auto& nested = constant.error();
          while (!nested.empty())
          {
               list.push_back(std::move(nested.top()));
               nested.pop();
          }
          return std::nullopt;
     }
     if (!std::holds_alternative<std::uint64_t>(constant->variant))
     {
          list.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("The bound of array '{}' must be an integer", name), bound->Source()));
          return std::nullopt;
     }
     const auto length = std::get<std::uint64_t>(constant->variant);
     if (length == 0)
     {
          list.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("The bound of array '{}' must be greater than zero", name), bound->Source()));
          return std::nullopt;
     }
     return length;
}

ecpps::ir::IR::DeclaratorTypeResult ecpps::ir::IR::ResolveDeclaratorType(const ast::Declarator& declarator,
                                                                         typeSystem::NonowningTypePointer base,
                                                                         const std::string& name,
                                                                         const bool allowUnboundedOutermost)
{
     const DeclaratorTypeResult failure{.type = nullptr, .unboundedArray = false, .failed = true};
     auto& list = this->GetContext().diagnostics.get().diagnosticsList;
     const auto* idNode = DeclaratorIdentifier(declarator);
     const Location idSource = idNode != nullptr ? idNode->Source() : Location{0, 0, 0};
     const auto* current = base;

     for (const auto& op : declarator.ptrOperators)
     {
          if (op.kind == ast::PtrOperator::Kind::Pointer)
          {
               TypeRequest request{};
               request.kind = TypeKind::Compound;
               request.qualifiers = QualifiersFrom(op.isConst, op.isVolatile);
               request.data = PointerRequest{.elementType = current};
               current = GetTypeContext().Get(request);
          }
          else if (op.kind == ast::PtrOperator::Kind::LValueReference ||
                   op.kind == ast::PtrOperator::Kind::RValueReference)
          {
               auto referenceKind = op.kind == ast::PtrOperator::Kind::LValueReference
                                         ? typeSystem::ReferenceType::Kind::LValue
                                         : typeSystem::ReferenceType::Kind::RValue;
               if (const auto* existingReference = current->CastTo<typeSystem::ReferenceType>())
               {
                    referenceKind = existingReference->GetKind() == typeSystem::ReferenceType::Kind::LValue ||
                                              referenceKind == typeSystem::ReferenceType::Kind::LValue
                                         ? typeSystem::ReferenceType::Kind::LValue
                                         : typeSystem::ReferenceType::Kind::RValue;
                    current = existingReference->BaseType();
               }

               TypeRequest request{};
               request.kind = TypeKind::Compound;
               request.data = ReferenceRequest{.elementType = current, .kind = referenceKind};
               current = GetTypeContext().Get(request);
          }
          else
               throw TracedException("TODO: pointers to members are not implemented yet");
     }

     bool unbounded = false;
     for (std::size_t index = declarator.suffixes.size(); index-- > 0;)
     {
          const auto* array = std::get_if<ast::ArrayBound>(&declarator.suffixes[index]);
          if (array == nullptr) throw TracedException("TODO: function declarators are not implemented yet");

          if (array->size == nullptr)
          {
               if (current->CastTo<typeSystem::ReferenceType>() != nullptr)
               {
                    list.push_back(std::make_unique<diagnostics::TypeError>(
                         std::format("Declaration of '{}' introduces an array of references, which is not allowed",
                                     name),
                         idSource));
                    return failure;
               }
               if (index != 0) // only the outermost array (applied last) may omit its bound
               {
                    list.push_back(std::make_unique<diagnostics::TypeError>(
                         std::format("Declaration of '{}' introduces an array of unbounded arrays which is not "
                                     "allowed, only the outermost array might be unbounded",
                                     name),
                         idSource));
                    return failure;
               }
               if (!RequireComplete(current, idSource, "an array element type")) return failure;
               unbounded = true;
               continue;
          }

          const auto length = EvaluateArrayBound(array->size, name);
          if (!length.has_value()) return failure;
          if (current->CastTo<typeSystem::ReferenceType>() != nullptr)
          {
               list.push_back(std::make_unique<diagnostics::TypeError>(
                    std::format("Declaration of '{}' introduces an array of references, which is not allowed", name),
                    array->size->Source()));
               return failure;
          }
          if (!RequireComplete(current, array->size->Source(), "an array element type")) return failure;

          TypeRequest request{};
          request.kind = TypeKind::Compound;
          request.data = BoundedArrayRequest{.elementType = current, .size = *length};
          current = GetTypeContext().Get(request);
     }

     const auto rejectUnbounded = [&]
     {
          list.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("The declaration of '{}' requires an array bound", name), idSource));
          return failure;
     };

     if (declarator.nested != nullptr)
     {
          if (unbounded && LastDerivation(*declarator.nested) != Derivation::None)
               throw TracedException("TODO: pointers to arrays of unknown bound are not implemented yet");
          auto inner = ResolveDeclaratorType(*declarator.nested, current, name, allowUnboundedOutermost);
          if (unbounded && !inner.failed)
          {
               if (!allowUnboundedOutermost) return rejectUnbounded();
               inner.unboundedArray = true;
          }
          return inner;
     }

     if (unbounded && !allowUnboundedOutermost) return rejectUnbounded();
     return {.type = current, .unboundedArray = unbounded, .failed = false};
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
     classScope.memberAccess[nested.Value()] = access;
}
void ecpps::ir::IR::ParseMemberDeclaration(const ast::MemberDeclarationNode& node, const typeSystem::ClassId id,
                                           const typeSystem::Access access, std::vector<typeSystem::ClassField>& fields,
                                           std::vector<std::function<void()>>& deferred)
{
     const auto& specifiers = node.Specifiers();
     auto& classScope = this->GetContext().contextSequence.back()->GetScope<ClassScope>();
     const auto report = [&](std::string message, const Location& source)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(std::move(message), source));
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
          if (identifier == nullptr || identifier->Value() != className)
          {
               report("Destructors and conversion functions are not supported", node.Source());
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
          constructor.declaration = &declarator;
          constructor.access = access;
          constructor.isExplicit = specifiers.functionSpecifier.explicitSpecifier.has_value();
          for (std::size_t index = 0; index < functionSuffix->parameters.size(); index++)
          {
               const auto& parameter = functionSuffix->parameters[index];
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
               constructor.parameters.push_back(
                    {parameter.name != nullptr ? parameter.name->ToString(0) : std::format("__arg{}", index),
                     parameterType, false});
          }

          auto& classInfo = GetTypeContext().Class(id);
          classInfo.hasUserDeclaredConstructors = true;
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
          classScope.memberAccess[definition->Value()] = access;

     for (const auto& declarator : node.Declarators())
     {
          const auto* identifier = DeclaratorIdentifier(declarator);
          if (identifier == nullptr)
          {
               report("Expected an unqualified identifier in member declarator", node.Source());
               continue;
          }
          const std::string& name = identifier->Value();

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
                    report("Virtual member functions are not supported yet", identifier->Source());
                    continue;
               }
               if (specifiers.functionSpecifier.explicitSpecifier.has_value())
               {
                    report("'explicit' is only supported on constructors", identifier->Source());
                    continue;
               }
               if (specifiers.constexprSpecifier != ast::ConstantExpressionSpecifier::None)
               {
                    report("'constexpr', 'consteval' and 'constinit' member functions are not supported",
                           identifier->Source());
                    continue;
               }
               if (name == "main")
               {
                    report("'main' cannot be declared as a member function", identifier->Source());
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
                    report("Expected a parameter list for member function", identifier->Source());
                    continue;
               }
               const auto& functionType = std::get<ast::ParametersAndQualifiers>(*suffix);
               if (declarator.nested != nullptr || !declarator.ptrOperators.empty())
               {
                    report("Member function declarators with derived return types are not supported yet",
                           identifier->Source());
                    continue;
               }
               const bool isStatic = specifiers.storageSpecifier & ast::StorageClassSpecifier::Static;
               if (isStatic && (functionType.isConst || functionType.isVolatile ||
                                functionType.refQualifier != ast::RefQualifier::None))
               {
                    report("A static member function cannot have cv- or ref-qualifiers", identifier->Source());
                    continue;
               }
               if (functionType.isVariadic)
               {
                    report("Variadic member functions are not supported", identifier->Source());
                    continue;
               }

               std::vector<FunctionScope::Parameter> parameters{};
               parameters.reserve(functionType.parameters.size());
               bool invalidParameters = false;
               for (std::size_t index = 0; index < functionType.parameters.size(); index++)
               {
                    const auto& parameter = functionType.parameters[index];
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
                        !RequireComplete(parameterType, parameter.type->Source(), "a member function parameter") ||
                        RejectClassByValue(parameterType, parameter.type->Source(), "a by-value parameter"))
                    {
                         invalidParameters = true;
                         break;
                    }
                    parameters.emplace_back(parameter.name != nullptr ? parameter.name->ToString(0)
                                                                      : std::format("__arg{}", index),
                                            parameterType, false);
               }
               if (invalidParameters) continue;
               if (parameters.size() == 1 && parameters.front().type == typeSystem::g_void.get()) parameters.clear();
               if (!typeSystem::g_void->CommonWith(declaredType) &&
                   (!RequireComplete(declaredType, node.Source(), "a member function return type") ||
                    RejectClassByValue(declaredType, node.Source(), "a return type")))
                    continue;

               if (declarator.functionBody == ast::FunctionBodyKind::Defaulted)
               {
                    report("Only special member functions may be defaulted", identifier->Source());
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
                                identifier->Source());
                         continue;
                    }
                    if (declarator.functionBody == ast::FunctionBodyKind::Block && function->isMemberFunctionDefined)
                    {
                         report("Member function is defined more than once", identifier->Source());
                         continue;
                    }
                    if (function->returnType != declaredType)
                    {
                         report("Member function redeclaration has a different return type", identifier->Source());
                         continue;
                    }
                    function->isInline = function->isInline || specifiers.isInline;
               }
               else
               {
                    auto functionScope =
                         MakeFunctionScope()
                              .Name(name)
                              .ReturnType(declaredType)
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
                              .Source(identifier->Source())
                              .NamespacePath(NamespacePathFromContext())
                              .Build();
                    functionScope->parameters = parameters;
                    functionScope->parentScope = &classScope;
                    functionScope->memberClassInfo = &GetTypeContext().Class(id);
                    functionScope->memberFunctionInfo = memberInfo;
                    functionScope->memberAccess = access;
                    memberFunctions.push_back(std::move(functionScope));
                    function = memberFunctions.back();
               }

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
                    const auto* declaratorPointer = &declarator;
                    deferred.emplace_back(
                         [this, declaratorPointer, function, id]
                         {
                              EmitMemberFunctionBody(*declaratorPointer, function, id);
                         });
               }
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

          const auto* declaratorPointer = &declarator;
          const auto* memberType = resolved.type;
          deferred.emplace_back(
               [this, id, fieldIndex, declaratorPointer, memberType, source = identifier->Source()]
               {
                    StoreDefaultMemberInitialiser(id, fieldIndex, *declaratorPointer, memberType, source);
               });
     }
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
                                                                   std::ranges::to<std::vector>());
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

     if (!function->isStatic) functionIr.EmitThisPrologue(thisType, declarator.location);
     std::uint64_t parameterIndex = static_cast<std::uint64_t>(!function->isStatic);
     for (const auto& parameter : function->parameters)
     {
          Variable variable{parameter.name, parameter.type, StorageDuration::Automatic};
          locals->emplace_back(std::move(variable));

          auto* activeFunction = dynamic_cast<FunctionContext*>(functionIr.GetContext().contextSequence.back().get());
          runtime_assert(activeFunction != nullptr, "Expected member function context during parameter emission");
          RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
          auto allocation = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator)
                    SingleAssignRegisterNode(activeFunction->GetNextRegisterIndex(), info, parameter.type->Width(),
                                             parameter.type->Alignment(), declarator.location)};
          auto* allocationPointer = allocation.get();
          activeFunction->RegisterParamAllocReg(parameter.name, allocationPointer);
          functionIr._built.push_back(std::unique_ptr<AllocationNode, IRDeleter>{new (
               *functionIr.GetContext().nodeAllocator) AllocationNode(std::move(allocation), declarator.location)});
          functionIr._built.push_back(std::unique_ptr<ParameterNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator)
                    ParameterNode(function.get(), parameterIndex++, allocationPointer, declarator.location)});
     }

     for (const auto& statement : declarator.body) functionIr.ParseNode(statement);
     if (typeSystem::g_void->CommonWith(function->returnType))
          functionIr._built.push_back(std::unique_ptr<SSAReturnNode, IRDeleter>{
               new (*functionIr.GetContext().nodeAllocator) SSAReturnNode(nullptr, declarator.location)});

     this->_built.push_back(std::unique_ptr<ProcedureNode, IRDeleter>{
          new (*this->GetContext().nodeAllocator)
               ProcedureNode(function.get(), std::move(locals), std::move(functionIr._built), declarator.location)});
}
bool ecpps::ir::IR::RejectClassByValue(const typeSystem::NonowningTypePointer type, const Location& source,
                                       const std::string_view what) const
{
     if (!typeSystem::IsClass(type)) return false;
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               std::format("Class type '{}' is not supported as {} yet", type->Name(), what), source));
     return true;
}
ecpps::ir::IR::DefaultMemberInitialiserValidator::DefaultMemberInitialiserValidator(IR& ir,
                                                                                    const typeSystem::ClassId ownerId,
                                                                                    Location source,
                                                                                    const std::size_t diagnosticCount)
    : _ir(ir), _ownerId(ownerId), _source(std::move(source)), _diagnosticCount(diagnosticCount)
{
}

void ecpps::ir::IR::DefaultMemberInitialiserValidator::Report(std::string message, const Location& at)
{
     _ir.GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(std::move(message), at));
}

bool ecpps::ir::IR::DefaultMemberInitialiserValidator::ValidateConstructor(
     const typeSystem::ClassType* classType, const std::vector<const ast::NodePointer*>& argumentNodes,
     const bool copyList, const bool isListInitialisation, const Location& at)
{
     const auto classRecord = _ir.GetContext().classTable->classes.find(classType->Info().id);
     if (classRecord == _ir.GetContext().classTable->classes.end() || classRecord->second.constructors.empty())
     {
          Report(std::format("No constructor of '{}' matches this initializer", classType->Info().DisplayName()), at);
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
     std::size_t bestScore = std::numeric_limits<std::size_t>::max();
     bool ambiguous = false;
     for (const auto& candidate : classRecord->second.constructors)
     {
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
          }
          else if (score == bestScore)
               ambiguous = true;
     }
     if (selected == nullptr || ambiguous)
     {
          Report(ambiguous
                      ? "Constructor initializer is ambiguous"
                      : std::format("No constructor of '{}' matches this initializer", classType->Info().DisplayName()),
                 at);
          return false;
     }
     if (copyList && selected->isExplicit)
     {
          Report("Copy-list initialization cannot select an explicit constructor", at);
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
                                  "initialization",
                                  arguments[index]->Type()->Name(), parameterType->Name()),
                      (*argumentNodes[index])->Source());
               return false;
          }
     if (selected->access != typeSystem::Access::Public && classType->Info().id != _ownerId)
     {
          Report("Selected constructor is not accessible", at);
          return false;
     }
     if (selected->declaration == nullptr || selected->declaration->functionBody == ast::FunctionBodyKind::Deleted)
     {
          Report("The selected constructor is deleted or unavailable", at);
          return false;
     }
     if (selected->declaration->functionBody == ast::FunctionBodyKind::None)
     {
          Report("The selected constructor is declared but has no definition", at);
          return false;
     }
     return true;
}

bool ecpps::ir::IR::DefaultMemberInitialiserValidator::ValidateEmpty(typeSystem::NonowningTypePointer type)
{
     if (type->CastTo<typeSystem::ReferenceType>() != nullptr)
     {
          Report("An omitted reference member has no initializer", _source);
          return false;
     }
     if (const auto* array = type->CastTo<typeSystem::ArrayType>()) return ValidateEmpty(array->ElementType());
     if (const auto* classType = type->CastTo<typeSystem::ClassType>())
     {
          if (classType->Info().kind == typeSystem::ClassKind::Union)
          {
               Report("Union initialization is not supported yet", _source);
               return false;
          }
          if (!_ir.IsAggregateClass(type) && classType->Info().hasUserDeclaredConstructors)
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
          Report("Union initialization is not supported yet", list.Source());
          return false;
     }
     if (!_ir.IsAggregateClass(classType) && classType->Info().hasUserDeclaredConstructors)
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
                    Report(std::format("Member '{}' is initialized more than once", designated->Member()),
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
          return _ir.GetContext().diagnostics.get().diagnosticsList.size() == _diagnosticCount;
     }
     if (const auto* array = type->CastTo<typeSystem::ArrayType>())
     {
          const auto* list = dynamic_cast<const ast::InitialiserListNode*>(value.get());
          if (list == nullptr)
          {
               Report("An array default member initializer must use a braced initializer", value->Source());
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
               if (classType->Info().hasUserDeclaredConstructors)
                    return ValidateConstructor(classType, {&value},
                                               initialisationType == ast::InitialisationType::EqualInitialiser, false,
                                               value->Source());
               Report(std::format("Class '{}' needs constructor or copy initialization support",
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
               Report("A scalar initializer list must contain exactly one initializer", value->Source());
               return false;
          }
          scalarValue = &list->Initialisers().front();
     }

     auto expression = _ir.ParseExpression(*scalarValue);
     if (expression == nullptr) return false;
     if (aggregateElement && _ir.IsNarrowingConversion(expression, type))
     {
          Report(std::format("Narrowing conversion from '{}' to '{}' is not allowed in aggregate "
                             "initialization",
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
          classRequest.data = std::move(classRequestData);
          const auto* ownerType = GetTypeContext().Get(classRequest);
          ActiveObjectInitialisation activeObject;
          activeObject.type = ownerType;
          this->_objectInitialisationStack.push_back(activeObject);
          const auto builtSize = this->_built.size();
          const auto diagnosticCount = this->GetContext().diagnostics.get().diagnosticsList.size();

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

void ecpps::ir::IR::EmitThisPrologue(const typeSystem::NonowningTypePointer thisType, const Location& source)
{
     auto* const funcCtx = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(funcCtx != nullptr, "Expected function context during `this` emission");
     if (funcCtx == nullptr || thisType == nullptr) return;
     auto& allocator = *this->GetContext().nodeAllocator;

     RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
     auto allocReg = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
          funcCtx->GetNextRegisterIndex(), info, thisType->Width(), thisType->Alignment(), source)};
     auto* allocRegPtr = allocReg.get();

     funcCtx->thisType = thisType;
     funcCtx->RegisterThisAllocReg(allocRegPtr);

     this->_built.push_back(
          std::unique_ptr<AllocationNode, IRDeleter>{new (allocator) AllocationNode(std::move(allocReg), source)});
     this->_built.push_back(std::unique_ptr<ParameterNode, IRDeleter>{
          new (allocator) ParameterNode(&funcCtx->GetScope<FunctionScope>(), 0, allocRegPtr, source)});
}
