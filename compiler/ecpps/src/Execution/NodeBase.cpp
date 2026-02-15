#include "NodeBase.h"
#include "Context.h"

std::expected<ecpps::ir::ConstantEvaluatedResult, std::stack<ecpps::diagnostics::DiagnosticsMessage>> ecpps::ir::
    NodeBase::TryConstantEvaluate(const ecpps::ir::EvaluationContext& evaluationContext) const
{
     auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
     if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
          result.error().push(std::make_unique<diagnostics::ConstantEvaluationWarning>(
              "Reached the maximum depth of the constant evaluation", this->Source()));
     else
          result.error().push(std::make_unique<diagnostics::ConstantEvaluationWarning>(
              std::format("Cannot evaluate `{}` as a constant expression", this->ToString(0)), this->Source()));
     return result;
}

std::expected<ecpps::ir::ConstantEvaluatedResult, std::stack<ecpps::diagnostics::DiagnosticsMessage>> ecpps::ir::
    IntegerArrayNode::TryConstantEvaluate(const ecpps::ir::EvaluationContext& evaluationContext) const
{
     if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
          return NodeBase::TryConstantEvaluate(evaluationContext);

     return ConstantEvaluatedResult{
         ConstantAggregateArray{
             this->_values |
             std::views::transform([](const std::uint32_t value)
                                   { return ConstantEvaluatedVariant{static_cast<std::uint64_t>(value)}; }) |
             std::ranges::to<std::vector>()},
         this->Source()};
}
