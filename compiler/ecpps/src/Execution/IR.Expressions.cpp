#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "../Machine/Mangling.h"
#include "../Parsing/ASTs/General.h"
#include "../Parsing/ASTs/Type.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "../TypeSystem/ClassType.h"
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

#include "IR.Internal.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

using ecpps::ir::detail::AnalyseCandidateFailure;
using ecpps::ir::detail::CollectExactMatches;
using ecpps::ir::detail::CollectSimilarIdentifierNames;
using ecpps::ir::detail::CollectSimilarNames;
using ecpps::ir::detail::CompareByPriority;
using ecpps::ir::detail::ConvertConstant;
using ecpps::ir::detail::FormatFunctionSignature;
using ecpps::ir::detail::IndirectStoreUnsupported;
using ecpps::ir::detail::IsIndirectLValue;
using ecpps::ir::detail::LevenshteinDistance;

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
