#include "NodeBase.h"
#include "Context.h"

std::expected<ecpps::ir::ConstantEvaluatedResult, std::stack<ecpps::diagnostics::DiagnosticsMessage>> ecpps::ir::
    NodeBase::TryConstantEvaluate(const ecpps::ir::EvaluationContext& evaluationContext) const
{
     if (ecpps::ir::GetContext().maxConstantEvaluationDepth < evaluationContext.currentDepth)
     {

          auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
          result.error().push(std::make_unique<diagnostics::ConstantEvaluationWarning>(
              "Reached the maximum depth of the constant evaluation", this->Source()));
          return result;
     }
     return std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
}

std::expected<ecpps::ir::ConstantEvaluatedResult, std::stack<ecpps::diagnostics::DiagnosticsMessage>> ecpps::ir::
    IntegerArrayNode::TryConstantEvaluate(const ecpps::ir::EvaluationContext& evaluationContext) const
{
     if (ecpps::ir::GetContext().maxConstantEvaluationDepth < evaluationContext.currentDepth)
          return NodeBase::TryConstantEvaluate(evaluationContext);

     return ConstantEvaluatedResult{
         ConstantAggregateArray{
             this->_values |
             std::views::transform([](const std::uint32_t value)
                                   { return ConstantEvaluatedVariant{static_cast<std::uint64_t>(value)}; }) |
             std::ranges::to<std::vector>()},
         this->Source()};
}
