#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "../TypeSystem/ArithmeticTypes.h"
#include "CodeGeneration/AbstractNodes.h"
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

#include "IR.Internal.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

using ecpps::ir::detail::AccessFor;

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
