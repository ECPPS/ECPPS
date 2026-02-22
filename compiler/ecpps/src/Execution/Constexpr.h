#include <string_view>
#include <variant>
#include "Assert.h"
#include "Expressions.h"
#include "NodeBase.h"
#include "Shared/Diagnostics.h"
#include "TypeSystem/CompoundTypes.h"

namespace ecpps::ir
{
     template <typename T = PRValue>
          requires std::is_base_of_v<ExpressionBase, T>
     std::unique_ptr<T> ConstantEvaluationResultToExpression(const ConstantEvaluatedResult& result,
                                                             typeSystem::NonowningTypePointer type,
                                                             BumpAllocator& allocator)
     {
          return std::visit(
              OverloadedVisitor{
                  [type](std::monostate) -> std::unique_ptr<T> { return std::make_unique<T>(type, nullptr, true); },
                  [type, &allocator, &result](const std::uint64_t value) -> std::unique_ptr<T>
                  {
                       return std::make_unique<PRValue>(
                           type,
                           std::unique_ptr<IntegralNode, IRDeleter>(new (allocator) IntegralNode(value, result.source)),
                           true);
                  },
                  [type, &allocator, &result](const ConstantAggregateArray& array) -> std::unique_ptr<T>
                  {
                       const auto* arrayType = type->CastTo<typeSystem::ArrayType>();
                       runtime_assert(arrayType != nullptr, "Invalid type provided for the constant evaluator");
                       const auto* elementType = arrayType->ElementType();
                       const auto* intElementType = elementType->CastTo<typeSystem::IntegralType>();
                       runtime_assert(arrayType->ElementCount() == array.members.size(),
                                      "Mismatch between the array type and element count");
                       runtime_assert(intElementType != nullptr,
                                      "Invalid underlying type provided for the constant evaluator");

                       const auto allHoldInts =
                           array.members |
                           std::views::transform(
                               [](const ConstantEvaluatedVariant& element)
                               {
                                    using std::string_view_literals::operator""sv;
                                    if (!std::holds_alternative<std::uint64_t>(element))
                                         throw ecpps::TracedException("Invalid int array sequence");
                                    return static_cast<std::uint32_t>(std::get<std::uint64_t>(element));
                               }) |
                           std::ranges::to<std::vector>();

                       return std::make_unique<PRValue>(
                           type,
                           std::unique_ptr<IntegerArrayNode, IRDeleter>(
                               new (allocator) IntegerArrayNode(allHoldInts, intElementType, result.source)),
                           true);
                  },
                  [](const auto&) -> std::unique_ptr<T> { throw[]{}; }},
              result.variant);
     }
} // namespace ecpps::ir
