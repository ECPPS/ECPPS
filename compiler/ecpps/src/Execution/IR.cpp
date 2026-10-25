#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <algorithm>
#include <deque>
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
     std::string FormatFunctionSignature(const std::shared_ptr<ecpps::ir::FunctionScope>& func)
     {
          std::string signature = func->Name().value_or("__unknown") + "(";
          bool first = true;
          for (const auto& param : func->parameters)
          {
               if (!first) signature += ", ";
               first = false;
               signature += param.type->Name();
               if (!param.name.empty()) signature += " " + param.name;
          }
          signature += ")";
          if (func->returnType)
          {
               signature += " -> " + func->returnType->Name();
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

     ecpps::ir::TypeRequest ErrorRequest(void)
     {
          ecpps::ir::TypeRequest request{};
          request.kind = ecpps::ir::TypeKind::Error;
          request.data = ecpps::ir::InvalidRequest(nullptr, {});
          return request;
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
          std::unique_ptr<SingleAssignRegisterNode, IRDeleter>(loadedResult), resultReg, source)});
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
                    new (allocator) SSALoadNode(std::move(loaded), slot.reg, source)});
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
               new (allocator) SSALoadNode(std::move(result), allocReg, source)});
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

     IR ir{this->_context};
     const auto* returnType = this->ParseType(node.Signature().type);
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

          if (decl.initialiser == nullptr)
          {
               // TODO: default-initialiser
               // for scalars it is an indeterminate value, but TODO: class types
               // TODO: Error for references
               continue;
          }

          if (isArray)
          {
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

          const auto elementSize = leftPointer->BaseType()->Size();
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

     if (!operand->IsLValue())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "An lvalue is required for a the address-of operator", operand->Value()->Source()));

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
     bool operator()(const std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>& a,
                     const std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>& b) const
     {
          return a.first < b.first;
     }
};

Expression ecpps::ir::IR::ParseCallExpression(const ast::CallOperatorNode& node)
{
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
               identifierFunction =
                    new ast::IdentifierNode("__unknown", node.Function()->Source()); // a leak yeah, but uh
          }
          else
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
     std::priority_queue<std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>,
                         std::vector<std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>>,
                         CompareByPriority>
          candidates{};

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

               didMatchName = true;
               const auto match = ecpps::ir::IR::MatchFunction(candidate, arguments);
               if (!match) continue;
               candidates.emplace(match, candidate);
          }

          if (candidates.empty()) continue;
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
               const auto size = type->Size();

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
     const auto size = operandType->Size();
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

Expression ecpps::ir::IR::ParseIdExpression(const ast::IdentifierNode& expression)
{
     const std::string& name = expression.Value();

     for (const auto& context : this->GetContext().contextSequence)
     {
          const auto& scope = context->GetScope();
          if (const auto* const functionScope = dynamic_cast<const FunctionScope*>(&scope))
          {
               for (const auto& local : functionScope->Locals())
               {
                    if (std::holds_alternative<Variable>(local.local))
                    {
                         const auto& variable = std::get<Variable>(local.local);
                         if (variable.Name().value_or("__unknown_local") == name)
                         {
                              return std::make_unique<LValue>(
                                   variable.type,
                                   std::unique_ptr<ReferenceNode, IRDeleter>{
                                        new (*this->GetContext().nodeAllocator) ReferenceNode(
                                             variable.Name().value_or("__unknown_local"), expression.Source())},
                                   false);
                         }
                    }
               }
          }
     }

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
     const auto* initialiserListNode = dynamic_cast<const ast::InitialiserListNode*>(expression.get());
     if (initialiserListNode == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Expected an initialiser list for list initialisation", expression->Source()));
          return nullptr;
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
          }
          return ConvertTo(std::move(initialiserExpression), desiredType);
     }
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "List initialisation with multiple initialisers is not supported yet", expression->Source()));
     return nullptr;
}

bool ecpps::ir::IR::IsNarrowingConversion([[maybe_unused]] const Expression& expression,
                                          [[maybe_unused]] typeSystem::NonowningTypePointer toType) const
{
     return false;
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

          const auto has = [&](std::string_view t)
          {
               return std::ranges::contains(tokens, t);
          };

          const auto longCount = std::ranges::count(tokens, "long");

          const bool isChar = has("char");
          const bool isShort = has("short");
          const bool isInt = has("int");
          const bool isLong = longCount > 0;
          const bool isSigned = has("signed");
          const bool isUnsigned = has("unsigned");

          if (!(isChar || isShort || isInt || isLong || isSigned || isUnsigned))
          {
               signedRequest.signedness = typeSystem::Signedness::Signed;
               signedRequest.size = typeSystem::TypeKind::Int;
               request.data = signedRequest;

               std::vector<diagnostics::DiagnosticsMessage> diagnosticsList{};
               diagnosticsList.push_back(diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Invalid type specifier: " + value, basicType->Source()));
               TypeRequest fallbackRequest{};
               fallbackRequest.kind = TypeKind::Error;
               fallbackRequest.data =
                    InvalidRequest(std::make_unique<TypeRequest>(request), std::move(diagnosticsList));
               return fallbackRequest;
          }

          if (isChar && !isSigned && !isUnsigned)
          {
               signedRequest.isCharWithoutSign = true;
          }
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
                    else
                         throw TracedException("array-to-pointer is not supported yet");

                    const auto source = expression->Value()->Source();
                    const auto* const allocReg = ResolveAllocReg(variableName);
                    if (allocReg == nullptr)
                    {
                         this->GetContext().diagnostics.get().diagnosticsList.push_back(
                              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                   "Cannot resolve allocation register for variable '" + variableName + "'", source));
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
ecpps::ir::Scope& ecpps::ir::IR::NearestNamespaceOrBlockScope() const
{
     for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
          if (dynamic_cast<const ClassContext*>(context.get()) == nullptr) return context->GetScope();
     throw TracedException("No enclosing namespace scope");
}

std::vector<std::string> ecpps::ir::IR::EnclosingPathUpTo(const Scope& target) const
{
     std::vector<std::string> path{};
     for (const auto& context : this->GetContext().contextSequence)
     {
          const auto& scope = context->GetScope();
          if (const auto* ns = dynamic_cast<const NamespaceScope*>(&scope); ns && ns->Name().has_value())
               path.push_back(*ns->Name());
          else if (const auto* cs = dynamic_cast<const ClassScope*>(&scope); cs && !cs->name.empty())
               path.push_back(cs->name);
          else if (const auto* fn = dynamic_cast<const FunctionScope*>(&scope))
               path.push_back(fn->Name().value_or("__unknown") + "()"); // local
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
                                info.KeyWord(), node.ToString(0)),
                    node.Source()));
     else
          this->GetContext().diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::Warning>(
               "warning",
               std::format("'{}' was declared as a {} but is redeclared with a different class-key", info.DisplayName(),
                           info.KeyWord()),
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

void ecpps::ir::IR::ParseClassMember(const ast::NodePointer& member, [[maybe_unused]] const typeSystem::ClassId id,
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
          ParseMemberDeclaration(*declaration, access, fields, deferred);
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
          if (op.kind != ast::PtrOperator::Kind::Pointer)
               throw TracedException("TODO: references and pointers to members are not implemented yet");

          TypeRequest request{};
          request.kind = TypeKind::Compound;
          request.qualifiers = QualifiersFrom(op.isConst, op.isVolatile);
          request.data = PointerRequest{.elementType = current};
          current = GetTypeContext().Get(request);
     }

     bool unbounded = false;
     for (std::size_t index = declarator.suffixes.size(); index-- > 0;)
     {
          const auto* array = std::get_if<ast::ArrayBound>(&declarator.suffixes[index]);
          if (array == nullptr) throw TracedException("TODO: function declarators are not implemented yet");

          if (array->size == nullptr)
          {
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
void ecpps::ir::IR::ParseMemberDeclaration(const ast::MemberDeclarationNode& node, const typeSystem::Access access,
                                           std::vector<typeSystem::ClassField>& fields,
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
          throw TracedException("TODO: constructors, destructors and conversion functions are not implemented yet");

     const auto* classSpecifier = dynamic_cast<const ast::ClassType*>(specifiers.definingTypeSpecifier.get());

     if (node.Declarators().empty())
     {
          if (classSpecifier != nullptr) ParseNestedClass(*classSpecifier, access, fields);
          else
               report("A declaration must declare something", node.Source());
          return;
     }

     if (specifiers.storageSpecifier & ast::StorageClassSpecifier::Static)
          throw TracedException("TODO: static data members are not implemented yet");
     if ((specifiers.storageSpecifier & ast::StorageClassSpecifier::ThreadLocal) ||
         (specifiers.storageSpecifier & ast::StorageClassSpecifier::Extern))
     {
          report("'thread_local' and 'extern' cannot be applied to a non-static data member", node.Source());
          return;
     }
     if (specifiers.isInline || specifiers.functionSpecifier.isVirtual ||
         specifiers.functionSpecifier.explicitSpecifier.has_value() ||
         specifiers.constexprSpecifier != ast::ConstantExpressionSpecifier::None)
     {
          report("'inline', 'virtual', 'explicit', 'constexpr', 'consteval' and 'constinit' cannot be applied to a "
                 "non-static data member or a typedef",
                 node.Source());
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
               throw TracedException("TODO: member functions are not implemented yet (bodies go into `deferred`)");
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

          if (IsReference(resolved.type)) throw TracedException("TODO: reference data members are not implemented");
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
          // field.isMutable = isMutable;
          field.source = identifier->Source();
          fields.push_back(std::move(field));
          classScope.memberAccess[name] = access;

          if (declarator.initialiser == nullptr) continue;
          if (declarator.initialisationType == ast::InitialisationType::ParenthesisedExpressionList)
          {
               report("A default member initialiser must use '=' or braces", declarator.initialiser->Source());
               continue;
          }

          // TODO: store the expression on the field.
          const auto* declaratorPointer = &declarator;
          const auto* memberType = resolved.type;
          deferred.emplace_back(
               [this, declaratorPointer, memberType]
               {
                    (void)PickInitialiser(declaratorPointer->initialiser, memberType,
                                          declaratorPointer->initialisationType);
               });
     }
}
