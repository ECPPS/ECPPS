#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
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
#include "Execution/Diagnostics.h"
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

#include "IR.Internal.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

using ecpps::ir::detail::AnalyseCandidateFailure;
using ecpps::ir::detail::CollectExactMatches;
using ecpps::ir::detail::CollectSimilarIdentifierNames;
using ecpps::ir::detail::CompareByPriority;
using ecpps::ir::detail::ConvertConstant;
using ecpps::ir::detail::FormatFunctionSignature;
using ecpps::ir::detail::IsIndirectLValue;
using ecpps::ir::detail::LevenshteinDistance;
using OverloadCandidateDiagnostic = ecpps::ir::overloadDiagnostics::Candidate;
using OverloadCandidateDetail = ecpps::ir::overloadDiagnostics::CandidateDetail;
using OverloadSimilarName = ecpps::ir::overloadDiagnostics::SimilarName;
using SimilarNamesKind = ecpps::ir::overloadDiagnostics::SimilarNamesKind;

template <std::ranges::input_range Range, typename Ranker> auto SelectUniqueBest(const Range& candidates, Ranker ranker)
{
     using Candidate = std::ranges::range_value_t<Range>;
     using Projection = std::invoke_result_t<Ranker&, std::ranges::range_reference_t<const Range>>;
     using Rank = typename Projection::value_type;
     std::optional<std::pair<Candidate, Rank>> best{};
     bool ambiguous = false;
     for (const auto& candidate : candidates)
     {
          const auto rank = ranker(candidate);
          if (!rank.has_value()) continue;
          if (!best.has_value() || *rank > best->second)
          {
               best.emplace(candidate, *rank);
               ambiguous = false;
          }
          else if (*rank == best->second)
               ambiguous = true;
     }
     return std::pair{std::move(best), ambiguous};
}

using ExplicitConstDeclaration = std::pair<std::string, ecpps::Location>;

static std::optional<ExplicitConstDeclaration> ExplicitConstLocalDeclaration(const Expression& expression,
                                                                             const ecpps::ir::Context& context)
{
     if (expression == nullptr) return std::nullopt;
     const auto* reference = dynamic_cast<const ecpps::ir::ReferenceNode*>(expression->Value().get());
     if (reference == nullptr) return std::nullopt;

     for (const auto& nestedContext : context.contextSequence | std::views::reverse)
          if (const auto* function = dynamic_cast<const ecpps::ir::FunctionScope*>(&nestedContext->GetScope()))
               for (const auto& local : function->Locals() | std::views::reverse)
                    if (const auto* variable = std::get_if<ecpps::ir::Variable>(&local.local);
                        variable != nullptr && variable->Name() == reference->Address() && variable->isExplicitlyConst)
                         return ExplicitConstDeclaration{variable->Name().value_or("<anonymous>"),
                                                         variable->declarationSource};
     return std::nullopt;
}

static bool SameLocation(const ecpps::Location& left, const ecpps::Location& right)
{
     return left.line == right.line && left.position == right.position && left.endPosition == right.endPosition;
}

static bool IsExplicitConstLocalReference(const Expression& expression,
                                          const std::optional<ExplicitConstDeclaration>& editedDeclaration,
                                          const ecpps::ir::Context& context)
{
     const auto argumentDeclaration = ExplicitConstLocalDeclaration(expression, context);
     return editedDeclaration.has_value() && argumentDeclaration.has_value() &&
            editedDeclaration->first == argumentDeclaration->first &&
            SameLocation(editedDeclaration->second, argumentDeclaration->second);
}

static Expression MakeExpressionProbe(const ecpps::typeSystem::NonowningTypePointer type, const bool isLValue,
                                      const bool isXValue, const bool isConstantExpression,
                                      const ecpps::Location& source, ecpps::BumpAllocator& allocator)
{
     auto value = ecpps::ir::NodePointer{new (allocator) ecpps::ir::ReferenceNode("__overload_probe", source)};
     if (isLValue) return std::make_unique<ecpps::LValue>(type, std::move(value), isConstantExpression);
     if (isXValue) return std::make_unique<ecpps::XValue>(type, std::move(value), isConstantExpression);
     return std::make_unique<ecpps::PRValue>(type, std::move(value), isConstantExpression);
}

static Expression MakeExpressionProbe(const Expression& expression, const ecpps::typeSystem::NonowningTypePointer type,
                                      ecpps::BumpAllocator& allocator)
{
     if (expression == nullptr) return nullptr;
     return MakeExpressionProbe(type, expression->IsLValue(), expression->IsXValue(),
                                expression->IsConstantExpression(), expression->Value()->Source(), allocator);
}

static ecpps::typeSystem::NonowningTypePointer RemoveConstFromClassType(const ecpps::typeSystem::ClassType* classType,
                                                                        ecpps::ir::TypeContext& typeContext)
{
     if (classType == nullptr) return nullptr;
     const auto qualifiers =
          classType->IsVolatile() ? ecpps::typeSystem::Qualifiers::Volatile : ecpps::typeSystem::Qualifiers::None;
     return typeContext.Get(ecpps::ir::TypeRequest{.kind = ecpps::ir::TypeKind::Compound,
                                                   .qualifiers = qualifiers,
                                                   .data = ecpps::ir::ClassRequest{.id = classType->Info().id}});
}

static bool CanModelSuggestionArguments(const std::vector<ecpps::ir::FunctionScope::Parameter>& parameters,
                                        const std::vector<Expression>& arguments)
{
     return std::ranges::equal(
          parameters, arguments,
          [](const auto& parameter, const auto& argument)
          {
               if (parameter.type == nullptr || argument == nullptr) return false;
               const auto* reference = parameter.type->template CastTo<ecpps::typeSystem::ReferenceType>();
               const auto* target = reference != nullptr ? reference->BaseType() : parameter.type;
               const auto* targetClass = target->template CastTo<ecpps::typeSystem::ClassType>();
               const auto* argumentClass = argument->Type()->template CastTo<ecpps::typeSystem::ClassType>();
               return targetClass == nullptr && argumentClass == nullptr
                           ? true
                           : reference != nullptr && targetClass != nullptr && argumentClass != nullptr &&
                                  targetClass->Info().id == argumentClass->Info().id;
          });
}

static std::optional<std::vector<Expression>> MakeSuggestionArgumentProbes(
     const std::vector<ecpps::ir::FunctionScope::Parameter>& parameters, const std::vector<Expression>& arguments,
     const std::optional<ExplicitConstDeclaration>& editedDeclaration, const ecpps::ir::Context& context,
     ecpps::ir::TypeContext& typeContext)
{
     if (!CanModelSuggestionArguments(parameters, arguments)) return std::nullopt;
     std::vector<Expression> probes{};
     probes.reserve(arguments.size());
     for (const auto& argument : arguments)
     {
          const auto* argumentType = argument->Type();
          if (IsExplicitConstLocalReference(argument, editedDeclaration, context))
          {
               const auto* classType = argumentType->CastTo<ecpps::typeSystem::ClassType>();
               argumentType = RemoveConstFromClassType(classType, typeContext);
          }
          probes.push_back(MakeExpressionProbe(argument, argumentType, *context.nodeAllocator));
     }
     return probes;
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
                    DiagnosticSink::Report(
                         this->GetContext(),
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
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot perform pointer arithmetic on a pointer to a void or incomplete type", source));
               return nullptr;
          }
          const auto elementSize = pointee->Size();

          if (elementSize == 0)
          {
               DiagnosticSink::Report(
                    this->GetContext(),
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
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build("Cannot add two pointers", source));
               return nullptr;
          }

          if (leftPointer->BaseType() != rightPointer->BaseType())
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Cannot subtract pointers to different types (" + left->Type()->Name() +
                                                " and " + right->Type()->Name() + ")",
                                           source));
               return nullptr;
          }

          const auto elementSize = leftPointer->BaseType()->Size();
          if (elementSize == 0)
          {
               DiagnosticSink::Report(this->GetContext(),
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
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Cannot perform this binary operation on " +
                                                               left->Type()->Name() + " and " + right->Type()->Name(),
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
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Cannot perform this binary operation on " +
                                                               left->Type()->Name() + " and " + right->Type()->Name(),
                                                          left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto* commonType = leftIntegral->CommonWith(rightIntegral);
     if (commonType == nullptr)
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Cannot find a common integral type between " +
                                                               left->Type()->Name() + " and " + left->Type()->Name(),
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
          DiagnosticSink::Report(
               this->GetContext(),
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

     if (auto* dereference = dynamic_cast<high::DereferenceNode*>(operand->Value().get()); dereference != nullptr)
          return std::move(*dereference).Operand();

     if (!operand->IsGLValue())
     {
          DiagnosticSink::Report(this->GetContext(),
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
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          std::format("Cannot subscript types `{}` and `{}`",
                                                                      left->Type()->Name(), right->Type()->Name()),
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
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Cannot perform this binary operation on " +
                                                               left->Type()->Name() + " and " + right->Type()->Name(),
                                                          left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto* commonType = leftIntegral->CommonWith(rightIntegral);
     if (commonType == nullptr)
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Cannot find a common integral type between " +
                                                               left->Type()->Name() + " and " + left->Type()->Name(),
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
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Cannot perform this binary operation on " +
                                                               left->Type()->Name() + " and " + right->Type()->Name(),
                                                          left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto* commonType = leftIntegral->CommonWith(rightIntegral);
     if (commonType == nullptr)
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Cannot find a common integral type between " +
                                                               left->Type()->Name() + " and " + left->Type()->Name(),
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
     bool isIndirect = IsIndirectLValue(operand);
     const auto* pointerType = isIndirect ? operand->Type()->CastTo<typeSystem::PointerType>() : nullptr;

     const auto* operandType = pointerType != nullptr ? pointerType->BaseType() : operand->Type();

     if (IsScalar(operandType))
     {
          const auto* qualifiedType = operandType->CastTo<typeSystem::QualifiedType>();
          if (!operand->IsLValue() || (qualifiedType != nullptr && qualifiedType->IsConst()))
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "A modifiable lvalue is required for the builtin pre-increment operator",
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
     bool isIndirect = IsIndirectLValue(operand);
     const auto* pointerType = isIndirect ? operand->Type()->CastTo<typeSystem::PointerType>() : nullptr;

     const auto* operandType = pointerType != nullptr ? pointerType->BaseType() : operand->Type();
     if (IsScalar(operandType))
     {
          const auto* qualifiedType = operandType->CastTo<typeSystem::QualifiedType>();
          if (!operand->IsLValue() || (qualifiedType != nullptr && qualifiedType->IsConst()))
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "A modifiable lvalue is required for the builtin post-increment operator",
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
     bool isIndirect = IsIndirectLValue(operand);
     const auto* pointerType = isIndirect ? operand->Type()->CastTo<typeSystem::PointerType>() : nullptr;

     const auto* operandType = pointerType != nullptr ? pointerType->BaseType() : operand->Type();
     if (IsScalar(operandType))
     {
          const auto* qualifiedType = operandType->CastTo<typeSystem::QualifiedType>();
          if (!operand->IsLValue() || (qualifiedType != nullptr && qualifiedType->IsConst()))
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "A modifiable lvalue is required for the builtin pre-decrement operator",
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
     bool isIndirect = IsIndirectLValue(operand);
     const auto* pointerType = isIndirect ? operand->Type()->CastTo<typeSystem::PointerType>() : nullptr;

     const auto* operandType = pointerType != nullptr ? pointerType->BaseType() : operand->Type();
     if (IsScalar(operandType))
     {
          const auto* qualifiedType = operandType->CastTo<typeSystem::QualifiedType>();
          if (!operand->IsLValue() || (qualifiedType != nullptr && qualifiedType->IsConst()))
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "A modifiable lvalue is required for the builtin post-decrement operator",
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

Expression ecpps::ir::IR::ParseAssignmentExpression(Expression left, Expression right, const Location& source)
{
     if (left == nullptr || right == nullptr) return nullptr;

     if (const auto* classType = left->Type()->CastTo<typeSystem::ClassType>())
     {
          const auto* rightClass = right->Type()->CastTo<typeSystem::ClassType>();
          if (!left->IsLValue() || classType->IsConst())
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Class assignment requires a modifiable lvalue", source));
               return nullptr;
          }
          if (rightClass == nullptr || rightClass->Info().id != classType->Info().id)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Class assignment requires an operand of the same class type", source));
               return nullptr;
          }
          const auto* type = left->Type();
          return std::make_unique<LValue>(type,
                                          std::unique_ptr<high::AssignNode, IRDeleter>{
                                               new (*this->GetContext().nodeAllocator)
                                                    high::AssignNode(std::move(left), std::move(right), source)},
                                          false);
     }

     const auto* leftQualifiedType = left->Type()->CastTo<typeSystem::QualifiedType>();

     if (leftQualifiedType == nullptr)
     {
          // TODO: Classes, floating point etc
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Cannot perform this binary operation on " +
                                                               left->Type()->Name() + " and " + right->Type()->Name(),
                                                          left->Value()->Source()));

          return nullptr;
     }
     if (!left->IsLValue())
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      "Cannot perform this binary operation on an rvalue", left->Value()->Source()));
          return nullptr;
     }
     if (leftQualifiedType->IsConst())
     {
          DiagnosticSink::Report(
               this->GetContext(),
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

Expression ecpps::ir::IR::ParseCompoundAssignmentExpression(Expression left, Expression right,
                                                            const ast::Operator operator_, const Location& source) const
{
     if (RejectClassByValue(left->Type(), source, "a compound assignment operand")) return nullptr;

     const auto* leftQualifiedType = left->Type()->CastTo<typeSystem::QualifiedType>();
     if (leftQualifiedType == nullptr)
     {
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform compound assignment on " + left->Type()->Name(), left->Value()->Source()));
          return nullptr;
     }

     if (!left->IsLValue())
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "A modifiable lvalue is required for compound assignment",
                                                          left->Value()->Source()));
          return nullptr;
     }
     if (leftQualifiedType->IsConst())
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      "Assignment expressions require modifiable lvalues", left->Value()->Source()));
          return nullptr;
     }

     if (right->Type() != left->Type())
     {
          right = ConvertTo(std::move(right), left->Type());
          if (right == nullptr) return nullptr;
     }

     const auto* resultType = left->Type();
     switch (operator_)
     {
     case ast::Operator::PlusAssign:
     case ast::Operator::MinusAssign:
     case ast::Operator::MultiplyAssign:
     case ast::Operator::DivideAssign:
     case ast::Operator::ModuloAssign:
     case ast::Operator::XorAssign:
     case ast::Operator::AndAssign:
     case ast::Operator::OrAssign:
     case ast::Operator::LeftShiftAssign:
     case ast::Operator::RightShiftAssign:
          return std::make_unique<LValue>(resultType,
                                          std::unique_ptr<high::CompoundAssignmentNode, IRDeleter>{
                                               new (*this->GetContext().nodeAllocator) high::CompoundAssignmentNode(
                                                    std::move(left), std::move(right), operator_, source)},
                                          false);
     default:
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Compound assignment operator '" + ast::ToString(operator_) + "' is not supported yet", source));
          return nullptr;
     }
}

Expression ecpps::ir::IR::ParseUnaryExpression(const ast::UnaryOperatorNode& node)
{
     const auto operator_ = node.Value();
     auto operand = ParseExpression(node.Operand());
     if (operand == nullptr) return nullptr;

     if (typeSystem::IsClass(operand->Type()))
     {
          std::vector<Expression> arguments{};
          if ((operator_ == ast::Operator::Increment || operator_ == ast::Operator::Decrement) &&
              node.UnaryType() == ast::UnaryOperatorType::Postfix)
               arguments.push_back(std::make_unique<PRValue>(
                    typeSystem::g_int.get(),
                    std::unique_ptr<IntegralNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                  IntegralNode(0, node.Source())},
                    true));
          return ParseMemberOperatorCall("operator" + ast::ToString(operator_), std::move(operand),
                                         std::move(arguments), node.Source());
     }

     switch (operator_)
     {
     case ast::Operator::Plus:
          if (const auto* integral = operand->Type()->CastTo<typeSystem::IntegralType>(); integral != nullptr)
          {
               const auto* promoted = typeSystem::PromoteInteger(integral);
               if (operand->Type() == promoted) return operand;
               return ConvertTo(std::move(operand), promoted);
          }
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "The built-in unary + operator requires an integral operand",
                                                          node.Source()));
          return nullptr;
     case ast::Operator::Minus: return this->ParseArithmeticNegationExpression(std::move(operand), node.Source());
     case ast::Operator::Asterisk: return this->ParseDereferenceExpression(std::move(operand), node.Source());
     case ast::Operator::Ampersand: return this->ParseAddressOfExpression(std::move(operand), node.Source());
     case ast::Operator::Tilde: return this->ParseBitwiseNotExpression(std::move(operand), node.Source());
     case ast::Operator::Exclamation:
          if (!IsScalar(operand->Type()))
          {
               DiagnosticSink::Report(
                    this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                             "The built-in unary ! operator requires a scalar operand", node.Source()));
               return nullptr;
          }
          {
               auto nonZero = MakeNonZeroTest(std::move(operand), node.Source());
               const auto* boolType = nonZero->Type();
               auto falseValue = std::make_unique<PRValue>(
                    boolType, std::unique_ptr<IntegralNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                           IntegralNode(0, node.Source())},
                    true);
               return std::make_unique<PRValue>(
                    boolType,
                    std::unique_ptr<high::CompareNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                       high::CompareNode(
                                                                            std::move(nonZero), std::move(falseValue),
                                                                            ComparisonPredicate::Equal, node.Source())},
                    false);
          }
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
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Expected a data member name after the member access operator", node.Source()));
               return nullptr;
          }

          std::vector<std::tuple<typeSystem::ClassId, bool, bool, bool, bool>> visitedArrowReceivers{};
          while (operator_ == ast::Operator::Arrow && typeSystem::IsClass(left->Type()))
          {
               const auto* receiverClass = left->Type()->CastTo<typeSystem::ClassType>();
               const auto receiverState = std::tuple{receiverClass->Info().id, receiverClass->IsConst(),
                                                     receiverClass->IsVolatile(), left->IsLValue(), left->IsXValue()};
               if (std::ranges::find(visitedArrowReceivers, receiverState) != visitedArrowReceivers.end())
               {
                    DiagnosticSink::Report(
                         this->GetContext(),
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Recursive operator-> did not produce a pointer to a class", node.Source()));
                    return nullptr;
               }
               visitedArrowReceivers.push_back(receiverState);
               left = ParseMemberOperatorCall("operator->", std::move(left), {}, node.Source());
               if (left == nullptr) return nullptr;
          }

          typeSystem::NonowningTypePointer objectType = left->Type();
          Expression objectPointer{};
          if (operator_ == ast::Operator::Arrow)
          {
               const auto* pointerType = objectType->CastTo<typeSystem::PointerType>();
               if (pointerType == nullptr)
               {
                    DiagnosticSink::Report(this->GetContext(),
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
                    DiagnosticSink::Report(this->GetContext(),
                                           diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                "The '.' operator requires a class lvalue", node.Source()));
                    return nullptr;
               }
               objectPointer = ParseAddressOfExpression(std::move(left), node.Source());
          }

          const auto* classType = objectType->CastTo<typeSystem::ClassType>();
          if (classType == nullptr)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Member access requires a class type", node.Source()));
               return nullptr;
          }
          for (const auto& field : classType->Info().fields)
               if (!field.name.empty() && field.name == memberName->Value())
                    return this->BuildMemberAccess(std::move(objectPointer), classType->Info(), field, node.Source());

          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      std::format("Class '{}' has no data member named '{}'",
                                                  classType->Info().DisplayName(), memberName->Value()),
                                      memberName->Source()));
          return nullptr;
     }

     auto right = ParseExpression(node.Right());
     if (left == nullptr || right == nullptr) return nullptr;

     if ((operator_ == ast::Operator::Plus || operator_ == ast::Operator::Minus) &&
         (typeSystem::IsClass(left->Type()) || typeSystem::IsClass(right->Type())))
     {
          std::optional<CompareByPriority::Candidate> bestMember{};
          bool memberAmbiguous = false;
          if (const auto* leftClass = left->Type()->CastTo<typeSystem::ClassType>())
          {
               const auto classRecord = this->GetContext().classTable->classes.find(leftClass->Info().id);
               if (classRecord != this->GetContext().classTable->classes.end() && classRecord->second.scope != nullptr)
               {
                    std::vector<Expression> arguments{};
                    arguments.push_back(MakeExpressionProbe(right, right->Type(), *this->GetContext().nodeAllocator));
                    for (const auto& candidate : classRecord->second.scope->functions)
                    {
                         if (candidate->Name() != "operator" + ast::ToString(operator_) ||
                             candidate->memberClassInfo == nullptr || candidate->isStatic)
                              continue;
                         const auto& member = candidate->memberFunctionInfo;
                         if ((leftClass->IsConst() && !member.isConst) ||
                             (leftClass->IsVolatile() && !member.isVolatile) ||
                             (member.refQualifier == abi::MemberRefQualifier::LValue && !left->IsLValue()) ||
                             (member.refQualifier == abi::MemberRefQualifier::RValue && left->IsLValue()))
                              continue;
                         const auto score = MatchFunction(candidate, arguments);
                         if (score == MatchingScore::NotMatching) continue;
                         std::size_t objectQualificationPenalty{};
                         if (!leftClass->IsConst() && member.isConst) ++objectQualificationPenalty;
                         if (!leftClass->IsVolatile() && member.isVolatile) ++objectQualificationPenalty;
                         if (!bestMember.has_value() || score > bestMember->argumentScore ||
                             (score == bestMember->argumentScore &&
                              objectQualificationPenalty < bestMember->objectQualificationPenalty))
                         {
                              bestMember = CompareByPriority::Candidate{.argumentScore = score,
                                                                        .objectQualificationPenalty =
                                                                             objectQualificationPenalty,
                                                                        .function = candidate};
                              memberAmbiguous = false;
                         }
                         else if (score == bestMember->argumentScore &&
                                  objectQualificationPenalty == bestMember->objectQualificationPenalty)
                              memberAmbiguous = true;
                    }
               }
          }

          const auto builtin = detail::SelectBuiltinArithmeticType(this->GetContext(), left, right);
          const auto memberCost = bestMember.has_value()
                                       ? std::optional<std::size_t>{std::numeric_limits<std::size_t>::max() -
                                                                    std::to_underlying(bestMember->argumentScore)}
                                       : std::nullopt;
          if (memberCost.has_value() && (!builtin.has_value() || *memberCost < builtin->cost))
          {
               if (memberAmbiguous)
               {
                    DiagnosticSink::Report(this->GetContext(),
                                           overloadDiagnostics::AmbiguousOverload(
                                                left->Type()->CastTo<typeSystem::ClassType>()->Info().DisplayName() +
                                                     "::operator" + ast::ToString(operator_),
                                                node.Source()));
                    return nullptr;
               }
               std::vector<Expression> arguments{};
               arguments.push_back(std::move(right));
               return ParseMemberOperatorCall("operator" + ast::ToString(operator_), std::move(left),
                                              std::move(arguments), node.Source());
          }
          if (builtin.has_value() && (!memberCost.has_value() || builtin->cost < *memberCost))
          {
               if (builtin->ambiguous)
               {
                    DiagnosticSink::Report(this->GetContext(),
                                           diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                "The built-in arithmetic operator has ambiguous user-defined "
                                                "conversions",
                                                node.Source()));
                    return nullptr;
               }
               auto operands = UsualArithmeticConversions(std::move(left), std::move(right));
               if (!operands.has_value()) return nullptr;
               if (operator_ == ast::Operator::Plus)
                    return std::make_unique<PRValue>(
                         operands->type,
                         std::unique_ptr<high::AdditionNode, IRDeleter>{
                              new (*this->GetContext().nodeAllocator) high::AdditionNode(
                                   std::move(operands->left), std::move(operands->right), node.Source())},
                         false);
               return std::make_unique<PRValue>(
                    operands->type,
                    std::unique_ptr<high::SubtractionNode, IRDeleter>{
                         new (*this->GetContext().nodeAllocator) high::SubtractionNode(
                              std::move(operands->left), std::move(operands->right), node.Source())},
                    false);
          }
          if (builtin.has_value() && memberCost.has_value() && builtin->cost == *memberCost)
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "The overloaded and built-in arithmetic operators are ambiguous", node.Source()));
               return nullptr;
          }
     }

     if (typeSystem::IsClass(left->Type()))
     {
          if (operator_ == ast::Operator::Assignment)
          {
               const auto* classType = left->Type()->CastTo<typeSystem::ClassType>();
               const auto classRecord = this->GetContext().classTable->classes.find(classType->Info().id);
               if (classRecord != this->GetContext().classTable->classes.end() && classRecord->second.scope != nullptr)
               {
                    std::vector<Expression> arguments{};
                    arguments.push_back(std::move(right));
                    const bool hasViableMemberOverload = std::ranges::any_of(
                         classRecord->second.scope->functions,
                         [&](const std::shared_ptr<FunctionScope>& candidate)
                         {
                              if (candidate->Name() != "operator=" || candidate->memberClassInfo == nullptr ||
                                  candidate->isStatic ||
                                  MatchFunction(candidate, arguments) == MatchingScore::NotMatching)
                                   return false;
                              if ((classType->IsConst() && !candidate->memberFunctionInfo.isConst) ||
                                  (classType->IsVolatile() && !candidate->memberFunctionInfo.isVolatile) ||
                                  (candidate->memberFunctionInfo.refQualifier == abi::MemberRefQualifier::LValue &&
                                   !left->IsLValue()) ||
                                  (candidate->memberFunctionInfo.refQualifier == abi::MemberRefQualifier::RValue &&
                                   left->IsLValue()))
                                   return false;
                              return true;
                         });
                    if (hasViableMemberOverload)
                         return ParseMemberOperatorCall("operator=", std::move(left), std::move(arguments),
                                                        node.Source());
                    right = std::move(arguments.front());
               }
          }
          else
          {
               std::vector<Expression> arguments{};
               arguments.push_back(std::move(right));
               return ParseMemberOperatorCall("operator" + ast::ToString(operator_), std::move(left),
                                              std::move(arguments), node.Source());
          }
     }

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
     case ast::Operator::PlusAssign:
     case ast::Operator::MinusAssign:
     case ast::Operator::MultiplyAssign:
     case ast::Operator::DivideAssign:
     case ast::Operator::ModuloAssign:
     case ast::Operator::XorAssign:
     case ast::Operator::AndAssign:
     case ast::Operator::OrAssign:
     case ast::Operator::LeftShiftAssign:
     case ast::Operator::RightShiftAssign:
          return this->ParseCompoundAssignmentExpression(std::move(left), std::move(right), operator_, node.Source());
     case ast::Operator::EqualsSign:
     case ast::Operator::NotEqual:
     case ast::Operator::Less:
     case ast::Operator::LessEqual:
     case ast::Operator::Greater:
     case ast::Operator::GreaterEqual:
          return this->ParseRelationalExpression(std::move(left), operator_, std::move(right), node.Source());
     case ast::Operator::Comma:
     {
          [[maybe_unused]] const auto* discardedValue = LowerExpression(std::move(left), this->_built);
          return right;
     }
     default: throw TracedException(std::logic_error("Invalid binary operator"));
     }
}

Expression ecpps::ir::IR::ParseMemberCallExpression(const ast::CallOperatorNode& node,
                                                    const ast::BinaryOperatorNode& memberAccess)
{
     const auto* operatorId = dynamic_cast<const ast::OperatorFunctionId*>(memberAccess.Right().get());
     const auto* conversionId = dynamic_cast<const ast::ConversionFunctionId*>(memberAccess.Right().get());
     if (operatorId != nullptr || conversionId != nullptr)
     {
          auto object = ParseExpression(memberAccess.Left());
          if (object == nullptr) return nullptr;
          if (memberAccess.Value() == ast::Operator::Arrow)
          {
               const auto* pointerType = object->Type()->CastTo<typeSystem::PointerType>();
               if (pointerType == nullptr || !typeSystem::IsClass(pointerType->BaseType()))
               {
                    DiagnosticSink::Report(
                         this->GetContext(),
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "The '->' member access requires a pointer to a class type", memberAccess.Source()));
                    return nullptr;
               }
               object = ParseDereferenceExpression(std::move(object), memberAccess.Source());
          }
          else if (memberAccess.Value() != ast::Operator::FullStop || !typeSystem::IsClass(object->Type()))
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Explicit operator calls require a class object", memberAccess.Source()));
               return nullptr;
          }
          if (object == nullptr) return nullptr;

          std::vector<Expression> arguments = node.Arguments() |
                                              std::views::transform(
                                                   [this](const ast::NodePointer& argument)
                                                   {
                                                        return this->ParseExpression(argument);
                                                   }) |
                                              ecpps::detail::ToVector;
          if (std::ranges::any_of(arguments,
                                  [](const Expression& argument)
                                  {
                                       return argument == nullptr;
                                  }))
               return nullptr;
          const std::string name = conversionId != nullptr ? conversionId->ToString(0)
                                                           : "operator" + ast::ToString(operatorId->GetOperator());
          return ParseMemberOperatorCall(name, std::move(object), std::move(arguments), node.Source());
     }

     const auto* memberName = dynamic_cast<const ast::IdentifierNode*>(memberAccess.Right().get());
     const auto report = [&](std::string message, const Location& source) -> Expression
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          std::move(message), source));
          return nullptr;
     };
     if (memberName == nullptr)
          return report("Expected a member function name after the member access operator", memberAccess.Source());

     auto receiver = ParseExpression(memberAccess.Left());
     if (receiver == nullptr) return nullptr;
     const auto constObjectDeclaration = ExplicitConstLocalDeclaration(receiver, this->GetContext());

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
                                         ecpps::detail::ToVector;
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
               failures.emplace_back(
                    "The implied object argument is const-qualified, but the implicit object parameter is not "
                    "const-qualified");
          if (classType->IsVolatile() && !function->memberFunctionInfo.isVolatile)
               failures.emplace_back(
                    "The implied object argument is volatile-qualified, but the implicit object parameter is not "
                    "volatile-qualified");
          if (function->memberFunctionInfo.refQualifier == abi::MemberRefQualifier::LValue && !receiverIsLvalue)
               failures.emplace_back(
                    "The implied object argument is an rvalue, but the implicit object parameter is an lvalue "
                    "reference");
          if (function->memberFunctionInfo.refQualifier == abi::MemberRefQualifier::RValue && receiverIsLvalue)
               failures.emplace_back(
                    "The implied object argument is an lvalue, but the implicit object parameter is an rvalue "
                    "reference");
          return failures;
     };

     const auto selectedAfterRemovingConst = [&](const std::shared_ptr<FunctionScope>& expected)
     {
          const auto [best, ambiguous] = SelectUniqueBest(
               sameNameCandidates,
               [&](const std::shared_ptr<FunctionScope>& candidate)
                    -> std::optional<std::tuple<MatchingScore, std::size_t>>
               {
                    const auto editedArguments =
                         MakeSuggestionArgumentProbes(candidate->parameters, arguments, constObjectDeclaration,
                                                      this->GetContext(), GetTypeContext());
                    if (!editedArguments.has_value()) return std::nullopt;
                    const auto& member = candidate->memberFunctionInfo;
                    if (!candidate->isStatic &&
                        ((classType->IsVolatile() && !member.isVolatile) ||
                         (member.refQualifier == abi::MemberRefQualifier::LValue && !receiverIsLvalue) ||
                         (member.refQualifier == abi::MemberRefQualifier::RValue && receiverIsLvalue)))
                         return std::nullopt;

                    const auto score = MatchFunction(candidate, *editedArguments);
                    if (score == MatchingScore::NotMatching) return std::nullopt;
                    const std::size_t qualificationPenalty =
                         candidate->isStatic
                              ? 0
                              : static_cast<std::size_t>(member.isConst) +
                                     static_cast<std::size_t>(!classType->IsVolatile() && member.isVolatile);
                    return std::tuple{score, std::numeric_limits<std::size_t>::max() - qualificationPenalty};
               });
          return best.has_value() && !ambiguous && best->first == expected && !expected->isDeleted &&
                 this->CanAccess(expected->memberClassInfo->id, expected->memberAccess) &&
                 expected->returnType != nullptr && expected->returnType->IsComplete() &&
                 std::ranges::all_of(expected->parameters,
                                     [](const FunctionScope::Parameter& parameter)
                                     {
                                          return parameter.type != nullptr && parameter.type->IsComplete();
                                     });
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

     const auto makeCandidateDiagnostic = [&](const std::shared_ptr<FunctionScope>& function,
                                              const bool includeArgumentFailures, const bool includeReceiverFailures,
                                              const ecpps::ir::detail::CandidateFailureInfo* failureInfo)
     {
          OverloadCandidateDiagnostic candidate{.signature = FormatFunctionSignature(function),
                                                .source = function->source};
          if (includeArgumentFailures)
          {
               const auto calculatedFailureInfo = failureInfo == nullptr ? AnalyseCandidateFailure(function, arguments)
                                                                         : ecpps::ir::detail::CandidateFailureInfo{};
               const auto& parameterFailures =
                    failureInfo == nullptr ? calculatedFailureInfo.parameterFailures : failureInfo->parameterFailures;
               for (const auto& [argumentIndex, failure] : parameterFailures)
               {
                    Location failureSource = memberName->Source();
                    if (std::cmp_not_equal(argumentIndex, -1) && argumentIndex > 0 &&
                        argumentIndex <= node.Arguments().Size())
                         failureSource = node.Arguments()[argumentIndex - 1]->Source();
                    std::optional<std::string> followup{};
                    if (std::cmp_not_equal(argumentIndex, -1) && argumentIndex > 0 &&
                        argumentIndex <= arguments.size() && argumentIndex <= function->parameters.size())
                    {
                         const auto& argument = arguments[argumentIndex - 1];
                         const auto& parameter = function->parameters[argumentIndex - 1];
                         if (argument != nullptr &&
                             (typeSystem::IsPointer(argument->Type()) || typeSystem::IsArray(argument->Type())) &&
                             !typeSystem::IsPointer(parameter.type) && !typeSystem::IsArray(parameter.type))
                              followup = "Did you mean to dereference the argument?";
                    }
                    candidate.details.push_back(OverloadCandidateDetail{
                         .message = failure, .source = failureSource, .followup = std::move(followup)});
               }
          }
          if (includeReceiverFailures)
               for (const auto& failure : receiverFailuresFor(function))
               {
                    candidate.details.push_back(
                         OverloadCandidateDetail{.message = failure, .source = memberAccess.Left()->Source()});
                    if (failure == "The implied object argument is const-qualified, but the implicit object parameter "
                                   "is not const-qualified" &&
                        constObjectDeclaration.has_value() && selectedAfterRemovingConst(function))
                         candidate.details.push_back(
                              OverloadCandidateDetail{.message = "The implied object argument is declared const here",
                                                      .source = constObjectDeclaration->second,
                                                      .followup = "Suggestion: remove const from this declaration"});
               }

          if (!this->CanAccess(function->memberClassInfo->id, function->memberAccess))
               candidate.access = accessName(function->memberAccess);
          candidate.isDeleted = function->isDeleted;
          return candidate;
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

     const auto collectSimilarMembers = [&]()
     {
          std::vector<OverloadSimilarName> suggestions{};
          const std::size_t maximumDistance = std::max<std::size_t>(3, memberName->Value().length() / 3);

          for (const auto& function : memberFunctions)
          {
               if (function->Name() == memberName->Value() || function->memberClassInfo == nullptr) continue;
               const auto functionName = function->Name().value_or("__unknown");
               const auto distance = LevenshteinDistance(memberName->Value().View(), functionName, maximumDistance);
               if (distance == 0 || distance > maximumDistance) continue;

               auto suggestion = std::ranges::find(suggestions, functionName, &OverloadSimilarName::name);
               if (suggestion == suggestions.end())
               {
                    suggestions.push_back(
                         OverloadSimilarName{.name = functionName,
                                             .source = function->source,
                                             .distance = distance,
                                             .argumentFailureCount = std::numeric_limits<std::size_t>::max()});
                    suggestion = std::prev(suggestions.end());
               }
               const auto match = MatchFunction(function, arguments);
               const auto failureInfo = AnalyseCandidateFailure(function, arguments);
               const auto failures = failureInfo.parameterFailures.size();
               suggestion->acceptsArguments = suggestion->acceptsArguments || static_cast<bool>(match);
               suggestion->argumentFailureCount = std::min(suggestion->argumentFailureCount, failures);
               suggestion->candidates.push_back(makeCandidateDiagnostic(function, true, true, &failureInfo));
          }

          for (const auto& field : classType->Info().fields)
          {
               if (field.name.empty() || field.name == memberName->Value()) continue;
               const auto distance = LevenshteinDistance(memberName->Value().View(), field.name, maximumDistance);
               if (distance == 0 || distance > maximumDistance) continue;

               auto suggestion = std::ranges::find(suggestions, field.name, &OverloadSimilarName::name);
               if (suggestion == suggestions.end())
                    suggestions.push_back(
                         OverloadSimilarName{.name = field.name,
                                             .source = field.source,
                                             .distance = distance,
                                             .argumentFailureCount = std::numeric_limits<std::size_t>::max(),
                                             .isDataMember = true});
               else
                    suggestion->isDataMember = true;
          }

          std::ranges::sort(suggestions,
                            [](const OverloadSimilarName& left, const OverloadSimilarName& right)
                            {
                                 if (left.acceptsArguments != right.acceptsArguments)
                                      return left.acceptsArguments > right.acceptsArguments;
                                 if (left.distance != right.distance) return left.distance < right.distance;
                                 if (left.argumentFailureCount != right.argumentFailureCount)
                                      return left.argumentFailureCount < right.argumentFailureCount;
                                 return left.name < right.name;
                            });
          return suggestions;
     };

     if (candidates.empty())
     {
          auto similarMembers = collectSimilarMembers();
          if (sameNameCandidates.empty())
          {
               const auto dataMember = std::ranges::find(classType->Info().fields, memberName->Value().View(),
                                                         &typeSystem::ClassField::name);
               std::optional<Location> dataMemberSource{};
               if (dataMember != classType->Info().fields.end()) dataMemberSource = dataMember->source;
               DiagnosticSink::Report(this->GetContext(),
                                      ecpps::ir::overloadDiagnostics::MissingMember(
                                           classType->Info().DisplayName(), memberName->Value().Copy(),
                                           memberName->Source(), dataMemberSource, similarMembers));
          }
          else
          {
               std::vector<OverloadCandidateDiagnostic> candidateDiagnostics{};
               candidateDiagnostics.reserve(sameNameCandidates.size());
               for (const auto& candidate : sameNameCandidates)
                    candidateDiagnostics.push_back(makeCandidateDiagnostic(candidate, true, true, nullptr));
               DiagnosticSink::Report(this->GetContext(),
                                      ecpps::ir::overloadDiagnostics::NoMatchingOverload(
                                           classType->Info().DisplayName() + "::" + memberName->Value().Copy(),
                                           memberName->Source(), candidateDiagnostics, similarMembers,
                                           SimilarNamesKind::Members));
          }
          return nullptr;
     }

     auto rankedCandidates = candidates;
     const auto selectedCandidate = rankedCandidates.top();
     rankedCandidates.pop();
     std::vector<std::shared_ptr<FunctionScope>> tiedBestCandidates{selectedCandidate.function};
     while (!rankedCandidates.empty() && rankedCandidates.top().argumentScore == selectedCandidate.argumentScore &&
            rankedCandidates.top().objectQualificationPenalty == selectedCandidate.objectQualificationPenalty)
     {
          tiedBestCandidates.push_back(rankedCandidates.top().function);
          rankedCandidates.pop();
     }
     if (tiedBestCandidates.size() > 1)
     {
          std::vector<OverloadCandidateDiagnostic> candidateDiagnostics{};
          candidateDiagnostics.reserve(tiedBestCandidates.size());
          for (const auto& tiedCandidate : tiedBestCandidates)
               candidateDiagnostics.push_back(makeCandidateDiagnostic(tiedCandidate, false, false, nullptr));
          DiagnosticSink::Report(this->GetContext(),
                                 ecpps::ir::overloadDiagnostics::AmbiguousOverload(
                                      classType->Info().DisplayName() + "::" + memberName->Value().Copy(),
                                      memberName->Source(), candidateDiagnostics));
          return nullptr;
     }

     const auto& candidate = candidates.top().function;
     const bool isAccessible = this->CanAccess(candidate->memberClassInfo->id, candidate->memberAccess);
     if (!isAccessible || candidate->isDeleted)
     {
          auto candidateDiagnostic = makeCandidateDiagnostic(candidate, false, false, nullptr);
          DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::SelectedOverloadFailure(
                                                          candidateDiagnostic.signature, memberName->Source(),
                                                          !isAccessible, candidate->isDeleted, candidateDiagnostic));
          return nullptr;
     }
     if (!RequireComplete(candidate->returnType, node.Source(), "a member function call")) return nullptr;
     for (const auto& parameter : candidate->parameters)
          if (!RequireComplete(parameter.type, node.Source(), "a member function call")) return nullptr;

     std::vector<Expression> evaluatedArguments{};
     evaluatedArguments.reserve(arguments.size() + static_cast<std::size_t>(!candidate->isStatic));
     if (candidate->isStatic)
     {
          [[maybe_unused]] const auto* evaluatedReceiver = LowerExpression(std::move(receiverPointer), this->_built);
     }
     else
          evaluatedArguments.push_back(std::move(receiverPointer));
     for (std::size_t index = 0; index < arguments.size(); index++)
          evaluatedArguments.push_back(PrepareByValueArgument(
               std::move(arguments[index]), candidate->parameters[index].type, node.Arguments()[index]->Source()));

     auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{new (*this->GetContext().nodeAllocator) FunctionCallNode(
          candidate, std::move(evaluatedArguments), node.Source())};
     return std::make_unique<PRValue>(candidate->returnType, std::move(call), false);
}

Expression ecpps::ir::IR::ParseMemberOperatorCall(const std::string_view name, Expression object,
                                                  std::vector<Expression> arguments, const Location& source)
{
     const auto report = [&](std::string message) -> Expression
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          std::move(message), source));
          return nullptr;
     };
     if (object == nullptr) return nullptr;
     const auto constObjectDeclaration = ExplicitConstLocalDeclaration(object, this->GetContext());
     if (!typeSystem::IsClass(object->Type()))
          return report(std::format("Member operator{} requires a class object", name));

     const auto* classType = object->Type()->CastTo<typeSystem::ClassType>();
     if (classType == nullptr) return report(std::format("Member operator{} requires a class object", name));

     const auto classRecord = this->GetContext().classTable->classes.find(classType->Info().id);
     if (classRecord == this->GetContext().classTable->classes.end() || classRecord->second.scope == nullptr)
          return report(std::format("Member operators of '{}' are unavailable", classType->Info().DisplayName()));

     const bool receiverIsLvalue = object->IsLValue();
     using FunctionCandidate = CompareByPriority::Candidate;
     std::priority_queue<FunctionCandidate, std::vector<FunctionCandidate>, CompareByPriority> candidates{};
     std::vector<std::shared_ptr<FunctionScope>> sameNameCandidates{};
     const auto receiverFailuresFor = [&](const std::shared_ptr<FunctionScope>& candidate)
     {
          std::vector<std::string> failures{};
          if (candidate->isStatic) return failures;
          if (classType->IsConst() && !candidate->memberFunctionInfo.isConst)
               failures.emplace_back(
                    "The implied object argument is const-qualified, but the implicit object parameter is not "
                    "const-qualified");
          if (classType->IsVolatile() && !candidate->memberFunctionInfo.isVolatile)
               failures.emplace_back(
                    "The implied object argument is volatile-qualified, but the implicit object parameter is not "
                    "volatile-qualified");
          if (candidate->memberFunctionInfo.refQualifier == abi::MemberRefQualifier::LValue && !receiverIsLvalue)
               failures.emplace_back(
                    "The implied object argument is an rvalue, but the implicit object parameter is an lvalue "
                    "reference");
          if (candidate->memberFunctionInfo.refQualifier == abi::MemberRefQualifier::RValue && receiverIsLvalue)
               failures.emplace_back(
                    "The implied object argument is an lvalue, but the implicit object parameter is an rvalue "
                    "reference");
          return failures;
     };
     const auto selectedAfterRemovingConst = [&](const std::shared_ptr<FunctionScope>& expected)
     {
          const auto [best, ambiguous] = SelectUniqueBest(
               sameNameCandidates,
               [&](const std::shared_ptr<FunctionScope>& candidate)
                    -> std::optional<std::tuple<MatchingScore, std::size_t>>
               {
                    const auto editedArguments =
                         MakeSuggestionArgumentProbes(candidate->parameters, arguments, constObjectDeclaration,
                                                      this->GetContext(), GetTypeContext());
                    if (!editedArguments.has_value()) return std::nullopt;
                    const auto& member = candidate->memberFunctionInfo;
                    if (!candidate->isStatic &&
                        ((classType->IsVolatile() && !member.isVolatile) ||
                         (member.refQualifier == abi::MemberRefQualifier::LValue && !receiverIsLvalue) ||
                         (member.refQualifier == abi::MemberRefQualifier::RValue && receiverIsLvalue)))
                         return std::nullopt;

                    const auto score = MatchFunction(candidate, *editedArguments);
                    if (score == MatchingScore::NotMatching) return std::nullopt;
                    const std::size_t qualificationPenalty =
                         candidate->isStatic
                              ? 0
                              : static_cast<std::size_t>(member.isConst) +
                                     static_cast<std::size_t>(!classType->IsVolatile() && member.isVolatile);
                    return std::tuple{score, std::numeric_limits<std::size_t>::max() - qualificationPenalty};
               });
          if (!best.has_value() || ambiguous || best->first != expected || expected->isDeleted ||
              !this->CanAccess(expected->memberClassInfo->id, expected->memberAccess) ||
              expected->returnType == nullptr || !expected->returnType->IsComplete() ||
              !std::ranges::all_of(expected->parameters,
                                   [](const FunctionScope::Parameter& parameter)
                                   {
                                        return parameter.type != nullptr && parameter.type->IsComplete();
                                   }))
               return false;

          const auto editedArguments = MakeSuggestionArgumentProbes(
               expected->parameters, arguments, constObjectDeclaration, this->GetContext(), GetTypeContext());
          if (!editedArguments.has_value()) return false;
          if ((name == "operator+" || name == "operator-") && editedArguments->size() == 1)
          {
               const auto* unqualifiedType = RemoveConstFromClassType(classType, GetTypeContext());
               auto objectProbe = MakeExpressionProbe(object, unqualifiedType, *this->GetContext().nodeAllocator);
               if (detail::SelectBuiltinArithmeticType(this->GetContext(), objectProbe, editedArguments->front())
                        .has_value())
                    return false;
          }
          return true;
     };
     const auto makeCandidateDiagnostic = [&](const std::shared_ptr<FunctionScope>& candidate)
     {
          OverloadCandidateDiagnostic diagnostic{.signature = FormatFunctionSignature(candidate),
                                                 .source = candidate->source,
                                                 .isDeleted = candidate->isDeleted};
          const auto matchedCount = std::min(arguments.size(), candidate->parameters.size());
          for (std::size_t index = 0; index < matchedCount; ++index)
          {
               const auto& argument = arguments[index];
               const auto& parameter = candidate->parameters[index];
               if (argument != nullptr && !ConstructorArgumentCost(parameter.type, argument).has_value())
               {
                    const auto* referenceType = parameter.type->CastTo<typeSystem::ReferenceType>();
                    diagnostic.details.push_back(OverloadCandidateDetail{
                         .message = std::format(
                              "Argument {} of type '{}' cannot {} parameter '{}' of type '{}'", index + 1,
                              argument->Type()->Name(), referenceType != nullptr ? "bind to" : "convert to",
                              parameter.name.empty() ? "<unnamed>" : parameter.name, parameter.type->Name()),
                         .source = argument->Value()->Source()});
               }
          }
          if (arguments.size() < candidate->parameters.size())
          {
               auto requiredCount = candidate->parameters.size();
               while (requiredCount > 0 && candidate->parameters[requiredCount - 1].hasDefaultArgument) --requiredCount;
               if (arguments.size() < requiredCount)
                    diagnostic.details.push_back(OverloadCandidateDetail{
                         .message = std::format("Expected at least {} argument{}, got {}", requiredCount,
                                                requiredCount == 1 ? "" : "s", arguments.size()),
                         .source = source});
          }
          if (arguments.size() > candidate->parameters.size() && !candidate->isVariadic)
               for (std::size_t index = candidate->parameters.size(); index < arguments.size(); ++index)
                    diagnostic.details.push_back(OverloadCandidateDetail{
                         .message = std::format("Argument {} has no corresponding parameter", index + 1),
                         .source = arguments[index] != nullptr ? arguments[index]->Value()->Source() : source});
          for (const auto& failure : receiverFailuresFor(candidate))
          {
               diagnostic.details.push_back(OverloadCandidateDetail{.message = failure, .source = source});
               if (failure == "The implied object argument is const-qualified, but the implicit object parameter "
                              "is not const-qualified" &&
                   constObjectDeclaration.has_value() && selectedAfterRemovingConst(candidate))
                    diagnostic.details.push_back(
                         OverloadCandidateDetail{.message = "The implied object argument is declared const here",
                                                 .source = constObjectDeclaration->second,
                                                 .followup = "Suggestion: remove const from this declaration"});
          }
          if (!this->CanAccess(candidate->memberClassInfo->id, candidate->memberAccess))
          {
               switch (candidate->memberAccess)
               {
               case typeSystem::Access::Public: diagnostic.access = "public"; break;
               case typeSystem::Access::Protected: diagnostic.access = "protected"; break;
               case typeSystem::Access::Private: diagnostic.access = "private"; break;
               }
          }
          return diagnostic;
     };
     for (const auto& candidate : classRecord->second.scope->functions)
     {
          if (candidate->Name() != name || candidate->memberClassInfo == nullptr) continue;
          sameNameCandidates.push_back(candidate);
          std::size_t objectQualificationPenalty{};
          if (!candidate->isStatic)
          {
               if (!receiverFailuresFor(candidate).empty()) continue;
               if (!classType->IsConst() && candidate->memberFunctionInfo.isConst) objectQualificationPenalty++;
               if (!classType->IsVolatile() && candidate->memberFunctionInfo.isVolatile) objectQualificationPenalty++;
          }
          const auto score = MatchFunction(candidate, arguments);
          if (score != MatchingScore::NotMatching)
               candidates.push({.argumentScore = score,
                                .objectQualificationPenalty = objectQualificationPenalty,
                                .function = candidate});
     }

     std::vector<OverloadCandidateDiagnostic> candidateDiagnostics{};
     candidateDiagnostics.reserve(sameNameCandidates.size());
     for (const auto& candidate : sameNameCandidates)
          candidateDiagnostics.push_back(makeCandidateDiagnostic(candidate));
     if (candidates.empty())
     {
          if (sameNameCandidates.empty())
               DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::MissingMember(
                                                                classType->Info().DisplayName(), std::string{name},
                                                                source, std::nullopt, {}));
          else
               DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::NoMatchingOverload(
                                                                classType->Info().DisplayName() + "::" +
                                                                     std::string{name},
                                                                source, candidateDiagnostics, {},
                                                                SimilarNamesKind::Members));
          return nullptr;
     }

     auto rankedCandidates = candidates;
     const auto selectedCandidate = rankedCandidates.top();
     rankedCandidates.pop();
     std::vector<std::shared_ptr<FunctionScope>> tiedBestCandidates{selectedCandidate.function};
     while (!rankedCandidates.empty() && rankedCandidates.top().argumentScore == selectedCandidate.argumentScore &&
            rankedCandidates.top().objectQualificationPenalty == selectedCandidate.objectQualificationPenalty)
     {
          tiedBestCandidates.push_back(rankedCandidates.top().function);
          rankedCandidates.pop();
     }
     if (tiedBestCandidates.size() > 1)
     {
          candidateDiagnostics.clear();
          for (const auto& candidate : tiedBestCandidates)
               candidateDiagnostics.push_back(OverloadCandidateDiagnostic{
                    .signature = FormatFunctionSignature(candidate), .source = candidate->source});
          DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::AmbiguousOverload(
                                                           classType->Info().DisplayName() + "::" +
                                                                std::string{name},
                                                           source, candidateDiagnostics));
          return nullptr;
     }

     const auto& candidate = selectedCandidate.function;
     const bool isAccessible = this->CanAccess(candidate->memberClassInfo->id, candidate->memberAccess);
     if (!isAccessible || candidate->isDeleted)
     {
          const auto diagnosticCandidate = OverloadCandidateDiagnostic{.signature = FormatFunctionSignature(candidate),
                                                                       .source = candidate->source,
                                                                       .isDeleted = candidate->isDeleted};
          DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::SelectedOverloadFailure(
                                                           diagnosticCandidate.signature, source, !isAccessible,
                                                           candidate->isDeleted, diagnosticCandidate));
          return nullptr;
     }
     if (arguments.size() < candidate->parameters.size())
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      std::format("Default arguments for {} are not supported yet", name), source));
          return nullptr;
     }
     if (arguments.size() > candidate->parameters.size())
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      std::format("Variadic arguments for {} are not supported yet", name), source));
          return nullptr;
     }
     if (!RequireComplete(candidate->returnType, source, "a member operator call")) return nullptr;
     for (const auto& parameter : candidate->parameters)
          if (!RequireComplete(parameter.type, source, "a member operator call")) return nullptr;

     std::vector<Expression> evaluatedArguments{};
     evaluatedArguments.reserve(arguments.size() + static_cast<std::size_t>(!candidate->isStatic));
     if (candidate->isStatic)
     {
          [[maybe_unused]] const auto* evaluatedObject = LowerExpression(std::move(object), this->_built);
     }
     else
     {
          Expression objectPointer{};
          if (object->IsGLValue()) objectPointer = ParseAddressOfExpression(std::move(object), source);
          else
          {
               const auto* objectType = object->Type();
               const auto* objectStorage = LowerExpression(std::move(object), this->_built);
               if (objectStorage == nullptr) return nullptr;
               auto materializedObject = std::make_unique<LValue>(
                    objectType,
                    std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                         new (*this->GetContext().nodeAllocator) SSARegisterReferenceNode(objectStorage, source)},
                    false);
               objectPointer = ParseAddressOfExpression(std::move(materializedObject), source);
          }
          if (objectPointer == nullptr) return nullptr;
          evaluatedArguments.push_back(std::move(objectPointer));
     }
     for (std::size_t index = 0; index < arguments.size(); ++index)
          evaluatedArguments.push_back(
               PrepareByValueArgument(std::move(arguments[index]), candidate->parameters[index].type, source));
     if (std::ranges::any_of(evaluatedArguments,
                             [](const Expression& argument)
                             {
                                  return argument == nullptr;
                             }))
          return nullptr;

     auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{new (*this->GetContext().nodeAllocator) FunctionCallNode(
          candidate, std::move(evaluatedArguments), source)};
     if (const auto* referenceType = candidate->returnType->CastTo<typeSystem::ReferenceType>();
         referenceType != nullptr)
     {
          auto pointerResult = std::make_unique<PRValue>(PointerTo(referenceType->BaseType()), std::move(call), false);
          auto dereference = std::unique_ptr<high::DereferenceNode, IRDeleter>{
               new (*this->GetContext().nodeAllocator) high::DereferenceNode(std::move(pointerResult), source)};
          if (referenceType->GetKind() == typeSystem::ReferenceType::Kind::LValue)
               return std::make_unique<LValue>(referenceType->BaseType(), std::move(dereference), false);
          return std::make_unique<XValue>(referenceType->BaseType(), std::move(dereference), false);
     }
     return std::make_unique<PRValue>(candidate->returnType, std::move(call), false);
}

Expression ecpps::ir::IR::ParseSubscriptOperatorExpression(const ast::SubscriptOperatorNode& node)
{
     auto object = ParseExpression(node.Object());
     if (object == nullptr) return nullptr;

     std::vector<Expression> arguments{};
     arguments.reserve(node.Arguments().Size());
     for (const auto& argument : node.Arguments()) arguments.push_back(ParseExpression(argument));
     if (std::ranges::any_of(arguments,
                             [](const Expression& argument)
                             {
                                  return argument == nullptr;
                             }))
          return nullptr;

     if (typeSystem::IsClass(object->Type()))
          return ParseMemberOperatorCall("operator[]", std::move(object), std::move(arguments), node.Source());

     if (arguments.size() != 1)
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "The built-in subscript operator requires exactly one "
                                                          "argument",
                                                          node.Source()));
          return nullptr;
     }
     return ParseSubscriptExpression(std::move(object), std::move(arguments.front()), node.Source());
}

Expression ecpps::ir::IR::ParseCallExpression(const ast::CallOperatorNode& node)
{
     if (const auto* memberAccess = dynamic_cast<const ast::BinaryOperatorNode*>(node.Function().get());
         memberAccess != nullptr &&
         (memberAccess->Value() == ast::Operator::FullStop || memberAccess->Value() == ast::Operator::Arrow))
          return this->ParseMemberCallExpression(node, *memberAccess);

     if (const auto* objectIdentifier = dynamic_cast<const ast::IdentifierNode*>(node.Function().get());
         objectIdentifier != nullptr)
     {
          const auto objectName = objectIdentifier->Value().View();
          bool namesObject = false;
          for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
          {
               const auto* functionScope = dynamic_cast<const FunctionScope*>(&context->GetScope());
               if (functionScope != nullptr &&
                   std::ranges::any_of(functionScope->Locals(),
                                       [objectName](const FunctionScope::LocalEntity& local)
                                       {
                                            const auto* variable = std::get_if<Variable>(&local.local);
                                            return variable != nullptr && variable->Name().value_or("") == objectName;
                                       }))
               {
                    namesObject = true;
                    break;
               }

               const auto* function = dynamic_cast<const FunctionContext*>(context.get());
               const auto* pointerType = function != nullptr && function->thisType != nullptr
                                              ? function->thisType->CastTo<typeSystem::PointerType>()
                                              : nullptr;
               const auto* classType = pointerType != nullptr
                                            ? pointerType->BaseType()->CastTo<typeSystem::ClassType>()
                                            : nullptr;
               if (classType != nullptr &&
                   std::ranges::any_of(classType->Info().fields,
                                       [objectName](const typeSystem::ClassField& field)
                                       {
                                            return field.name == objectName;
                                       }))
               {
                    namesObject = true;
                    break;
               }
          }
          if (!this->_objectInitialisationStack.empty())
          {
               const auto* classType = this->_objectInitialisationStack.back().type->CastTo<typeSystem::ClassType>();
               namesObject = namesObject ||
                             (classType != nullptr &&
                              std::ranges::any_of(classType->Info().fields,
                                                  [objectName](const typeSystem::ClassField& field)
                                                  {
                                                       return field.name == objectName;
                                                  }));
          }

          if (namesObject)
          {
               auto object = ParseExpression(node.Function());
               if (object != nullptr && typeSystem::IsClass(object->Type()))
               {
                    std::vector<Expression> arguments = node.Arguments() |
                                                        std::views::transform(
                                                             [this](const ast::NodePointer& argument)
                                                             {
                                                                  return this->ParseExpression(argument);
                                                             }) |
                                                        ecpps::detail::ToVector;
                    if (std::ranges::any_of(arguments,
                                            [](const Expression& argument)
                                            {
                                                 return argument == nullptr;
                                            }))
                         return nullptr;
                    return ParseMemberOperatorCall("operator()", std::move(object), std::move(arguments),
                                                   node.Source());
               }
          }
     }

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
               qualifiers.emplace_back(part->Value().View());
          }
          if (isFromGlobalNamespace && parts.size() == 1)
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Expected a function name in a call expression, found global scope specifier",
                         node.Function()->Source()));
               return nullptr;
          }
          identifierFunction = dynamic_cast<ast::IdentifierNode*>(parts.back().get());
     }

     if (!identifierFunction)
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      std::format("Expected a function name in a call expression, found: {}",
                                                  node.Function()->ToString(0)),
                                      node.Function()->Source()));
          return nullptr;
     }

     const std::string name{identifierFunction->Value().View()};
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
                                              ecpps::detail::ToVector;

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
               const auto& selectedFunction = selectedCandidate.function;
               const std::string displayName = selectedFunction->memberClassInfo != nullptr
                                                    ? selectedFunction->memberClassInfo->DisplayName() + "::" + name
                                                    : name;
               DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::AmbiguousOverload(
                                                               displayName, identifierFunction->Source()));
               return nullptr;
          }
          const auto& candidate = candidates.top().function;

          if (candidate->memberClassInfo != nullptr &&
              !this->CanAccess(candidate->memberClassInfo->id, candidate->memberAccess))
          {
               const std::string signature = FormatFunctionSignature(candidate);
               DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::SelectedOverloadFailure(
                                                               signature, identifierFunction->Source(), true, false));
               return nullptr;
          }

          if (candidate->isDeleted)
          {
               DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::SelectedOverloadFailure(
                                                               FormatFunctionSignature(candidate),
                                                               identifierFunction->Source(), false, true));
               return nullptr;
          }

          for (const auto& param : candidate->parameters)
               if (!RequireComplete(param.type, node.Source(), "a function call")) return nullptr;
          if (candidate->returnType != nullptr && !typeSystem::g_void->CommonWith(candidate->returnType) &&
              !RequireComplete(candidate->returnType, node.Source(), "a function call"))
               return nullptr;

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
                    DiagnosticSink::Report(
                         this->GetContext(),
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "A non-static member function requires an object", identifierFunction->Source()));
                    return nullptr;
               }
               evaluatedArguments.push_back(std::move(receiver));
          }
          for (std::size_t index = 0; index < candidate->parameters.size(); index++)
          {
               const auto& parameter = candidate->parameters[index];
               evaluatedArguments.push_back(PrepareByValueArgument(std::move(arguments[index]), parameter.type,
                                                                   node.Arguments()[index]->Source()));
          }

          auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{
               new (*this->GetContext().nodeAllocator)
                    FunctionCallNode(candidate, std::move(evaluatedArguments), node.Source())};

          return std::make_unique<PRValue>(candidate->returnType, std::move(call), false);
     }

     std::vector<Expression> argumentsForAnalysis = node.Arguments() |
                                                    std::views::transform(
                                                         [this](const ast::NodePointer& argument)
                                                         {
                                                              return this->ParseExpression(argument);
                                                         }) |
                                                    ecpps::detail::ToVector;

     auto exactMatches = CollectExactMatches(name, this->GetContext().contextSequence);
     const auto makeCandidateDiagnostic = [&](const std::shared_ptr<FunctionScope>& function)
     {
          OverloadCandidateDiagnostic candidate{.signature = FormatFunctionSignature(function),
                                                .source = function->source};
          const auto failureInfo = AnalyseCandidateFailure(function, argumentsForAnalysis);
          for (const auto& [parameterIndex, failure] : failureInfo.parameterFailures)
          {
               const bool hasArgument = std::cmp_not_equal(parameterIndex, -1) && parameterIndex > 0 &&
                                        parameterIndex <= node.Arguments().Size();
               const Location argumentSource =
                    hasArgument ? node.Arguments()[parameterIndex - 1]->Source() : identifierFunction->Source();
               std::optional<std::string> followup{};
               if (std::cmp_not_equal(parameterIndex, -1) && parameterIndex > 0 &&
                   parameterIndex <= argumentsForAnalysis.size() && parameterIndex <= function->parameters.size())
               {
                    const auto& argument = argumentsForAnalysis[parameterIndex - 1];
                    const auto& parameter = function->parameters[parameterIndex - 1];
                    if (argument != nullptr &&
                        (typeSystem::IsPointer(argument->Type()) || typeSystem::IsArray(argument->Type())) &&
                        !typeSystem::IsPointer(parameter.type) && !typeSystem::IsArray(parameter.type))
                         followup = "Did you mean to dereference the argument?";
               }
               candidate.details.push_back(OverloadCandidateDetail{
                    .message = failure, .source = argumentSource, .followup = std::move(followup)});
          }
          return candidate;
     };
     const auto collectSimilarFunctions = [&]()
     {
          std::vector<OverloadSimilarName> similarNames{};
          const std::size_t maximumDistance = std::max<std::size_t>(3, name.length() / 3);
          for (const auto& context : this->GetContext().contextSequence)
               for (const auto& function : context->GetScope().functions)
               {
                    if (function == nullptr || !function->Name().has_value()) continue;
                    const auto& functionName = *function->Name();
                    if (functionName == name) continue;
                    const auto distance = LevenshteinDistance(name, functionName, maximumDistance);
                    if (distance == 0 || distance > maximumDistance) continue;

                    auto suggestion = std::ranges::find(similarNames, functionName, &OverloadSimilarName::name);
                    if (suggestion == similarNames.end())
                    {
                         similarNames.push_back(
                              OverloadSimilarName{.name = functionName,
                                                  .source = identifierFunction->Source(),
                                                  .distance = distance,
                                                  .argumentFailureCount = std::numeric_limits<std::size_t>::max()});
                         suggestion = std::prev(similarNames.end());
                    }
                    const bool acceptsArguments = static_cast<bool>(MatchFunction(function, argumentsForAnalysis));
                    const std::size_t failureCount =
                         AnalyseCandidateFailure(function, argumentsForAnalysis).parameterFailures.size();
                    suggestion->acceptsArguments = suggestion->acceptsArguments || acceptsArguments;
                    suggestion->argumentFailureCount = std::min(suggestion->argumentFailureCount, failureCount);
               }

          std::ranges::sort(similarNames,
                            [](const OverloadSimilarName& left, const OverloadSimilarName& right)
                            {
                                 if (left.acceptsArguments != right.acceptsArguments)
                                      return left.acceptsArguments > right.acceptsArguments;
                                 if (left.distance != right.distance) return left.distance < right.distance;
                                 if (left.argumentFailureCount != right.argumentFailureCount)
                                      return left.argumentFailureCount < right.argumentFailureCount;
                                 return left.name < right.name;
                            });
          return similarNames;
     };

     auto similarNames = collectSimilarFunctions();
     if (!exactMatches.empty())
     {
          std::vector<OverloadCandidateDiagnostic> candidateDiagnostics{};
          candidateDiagnostics.reserve(exactMatches.size());
          for (const auto& func : exactMatches) candidateDiagnostics.push_back(makeCandidateDiagnostic(func));
          std::string displayName = name;
          if (!qualifiers.empty())
          {
               displayName.clear();
               for (const auto& qualifier : qualifiers) displayName += (displayName.empty() ? "" : "::") + qualifier;
               displayName += (displayName.empty() ? "" : "::") + name;
          }
          else if (exactMatches.front()->memberClassInfo != nullptr)
               displayName = exactMatches.front()->memberClassInfo->DisplayName() + "::" + name;
          DiagnosticSink::Report(this->GetContext(),
                                 ecpps::ir::overloadDiagnostics::NoMatchingOverload(
                                      displayName, identifierFunction->Source(), candidateDiagnostics, similarNames,
                                      SimilarNamesKind::Functions));
     }
     else
     {
          DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::UnresolvedFunction(
                                                          name, identifierFunction->Source(), similarNames));
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
                    DiagnosticSink::Report(
                         this->GetContext(),
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
          ast::IdentifierNode identifierNode{basicTypeNode->Value().Pool().Intern(identifierName),
                                             basicTypeNode->Source()};
          operand = ParseIdExpression(identifierNode);
     }
     else
          operand = ParseExpression(expression.Value());

     if (operand == nullptr) return nullptr;

     const auto& operandType = operand->Type();
     if (operandType == nullptr)
     {
          DiagnosticSink::Report(
               this->GetContext(),
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
                    DiagnosticSink::Report(
                         this->GetContext(),
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
          ast::IdentifierNode identifierNode{basicTypeNode->Value().Pool().Intern(identifierName),
                                             basicTypeNode->Source()};
          operand = ParseIdExpression(identifierNode);
     }
     else
          operand = ParseExpression(expression.Value());

     if (operand == nullptr) return nullptr;

     const auto& operandType = operand->Type();
     if (operandType == nullptr)
     {
          DiagnosticSink::Report(
               this->GetContext(),
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

     DiagnosticSink::Report(
          this->GetContext(),
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "'this' is only available in a non-static member function or member initialiser", source));
     return nullptr;
}

Expression ecpps::ir::IR::ParseIdExpression(const ast::IdentifierNode& expression)
{
     const std::string name{expression.Value().View()};

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

     DiagnosticSink::Report(this->GetContext(), std::move(mainError));

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
     if (auto* const subscriptExpression = dynamic_cast<ast::SubscriptOperatorNode*>(expression.get());
         subscriptExpression != nullptr)
          return this->ParseSubscriptOperatorExpression(*subscriptExpression);
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

     DiagnosticSink::Report(this->GetContext(),
                            diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                 expression->ToString(0) + " cannot appear in this context.", expression->Source()));

     return nullptr;
}

ecpps::ir::MatchingScore ecpps::ir::IR::MatchFunction(const std::shared_ptr<FunctionScope>& function,
                                                      const std::vector<Expression>& arguments)
{
     const auto fixedArgumentCount = std::min(arguments.size(), function->parameters.size());
     if (arguments.size() < function->parameters.size())
          for (std::size_t index = arguments.size(); index < function->parameters.size(); ++index)
               if (!function->parameters[index].hasDefaultArgument) return MatchingScore::NotMatching;
     if (arguments.size() > function->parameters.size() && !function->isVariadic) return MatchingScore::NotMatching;

     std::size_t totalCost{};

     for (std::size_t index = 0; index < fixedArgumentCount; ++index)
     {
          const auto& argument = arguments[index];
          const auto& parameter = function->parameters[index];
          if (argument == nullptr) continue;
          const auto cost = ConstructorArgumentCost(parameter.type, argument);
          if (!cost.has_value() || *cost > std::numeric_limits<std::size_t>::max() - totalCost)
               return MatchingScore::NotMatching;
          totalCost += *cost;
     }
     if (arguments.size() > fixedArgumentCount)
     {
          const auto ellipsisCost = std::numeric_limits<std::size_t>::max() / 2;
          if (ellipsisCost > std::numeric_limits<std::size_t>::max() - totalCost) return MatchingScore::NotMatching;
          totalCost += ellipsisCost;
     }

     if (totalCost >= std::to_underlying(MatchingScore::MaxScore)) return MatchingScore::NotMatching;
     return static_cast<MatchingScore>(std::to_underlying(MatchingScore::MaxScore) - totalCost);
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
