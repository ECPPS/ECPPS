#pragma once

#include <cstdint>
#include <variant>
#include "../Parsing/AST.h"
#include "Assert.h"
#include "Context.h"
#include "Expressions.h"
#include "NodeBase.h"

namespace ecpps::ir
{
     enum struct BinaryOperationLevel : std::uint_fast8_t
     {
          None,         // +            => add tmp
          Atomic,       // (atomic) +   => lock add tmp
          Assign,       // +=           => add
          AtomicAssign, // (atomic) +=  => lock add
     };

     enum struct MemoryOrdering : std::uint_fast8_t
     {
          Acquire,
          Release,
          Relaxed,
          Sequenced,
     };

     /// <summary>
     /// add 2 (hopefully) integral operands. No checking as I can't predict them really, the optimiser might clash 2
     /// classes with that btw, if they are layout compatible, have an overloaded operator+= that adds all members 1:1
     /// to each other and each of them is possible to add using that node Example: struct C
     /// {
     ///      C& operator+(const C& other)
     ///      {
     ///           this->a += other.a;
     ///           this->b += other.b;
     ///           return *this;
     ///      }
     ///
     ///      int a;
     ///      int b;
     /// };
     ///
     /// This function supports vectorisation
     /// </summary>
     class AdditionNode final : public NodeBase
     {
     public:
          explicit AdditionNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Addition, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " + " +
                      this->_right->Value()->ToString(0);
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
                    return NodeBase::TryConstantEvaluate(evaluationContext);

               auto leftConstexpr = this->_left->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!leftConstexpr.has_value())
               {
                    leftConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the addition did not evaluate to a constant expression",
                        this->Source()));
                    return leftConstexpr;
               }
               auto rightConstexpr = this->_right->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!rightConstexpr.has_value())
               {
                    rightConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the addition did not evaluate to a constant expression",
                        this->Source()));
                    return rightConstexpr;
               }
               if (!std::holds_alternative<std::uint64_t>(leftConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the addition did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               if (!std::holds_alternative<std::uint64_t>(rightConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The right hand side of the addition did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               const auto leftValue = std::get<std::uint64_t>(leftConstexpr->variant);
               const auto rightValue = std::get<std::uint64_t>(rightConstexpr->variant);

               return ConstantEvaluatedResult{leftValue + rightValue, this->Source()};
          };

     private:
          Expression _left;
          Expression _right;
     };

     class AdditionAssignNode final : public NodeBase
     {
     public:
          explicit AdditionAssignNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Addition, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) +
                      " += " + this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };
     class SubtractionAssignNode final : public NodeBase
     {
     public:
          explicit SubtractionAssignNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Subtraction, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) +
                      " -= " + this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };
     class PostIncrementNode final : public NodeBase
     {
     public:
          explicit PostIncrementNode(Expression operand, std::size_t increment, Location source)
              : NodeBase(NodeKind::Addition, source), _operand(std::move(operand)), _increment(increment)
          {
          }
          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] std::size_t IncrementValue(void) const noexcept { return this->_increment; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_operand->Value()->ToString(0) + " ++ " +
                      std::to_string(this->_increment);
          }

     private:
          Expression _operand;
          std::size_t _increment;
     };
     class PostDecrementNode final : public NodeBase
     {
     public:
          explicit PostDecrementNode(Expression operand, std::size_t decrement, Location source)
              : NodeBase(NodeKind::Subtraction, source), _operand(std::move(operand)), _increment(decrement)
          {
          }
          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] std::size_t IncrementValue(void) const noexcept { return this->_increment; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_operand->Value()->ToString(0) + " -- " +
                      std::to_string(this->_increment);
          }

     private:
          Expression _operand;
          std::size_t _increment;
     };

     class SubtractionNode final : public NodeBase
     {
     public:
          explicit SubtractionNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Subtraction, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " - " +
                      this->_right->Value()->ToString(0);
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
                    return NodeBase::TryConstantEvaluate(evaluationContext);

               auto leftConstexpr = this->_left->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!leftConstexpr.has_value())
               {
                    leftConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the subtraction did not evaluate to a constant expression",
                        this->Source()));
                    return leftConstexpr;
               }
               auto rightConstexpr = this->_right->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!rightConstexpr.has_value())
               {
                    rightConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the subtraction did not evaluate to a constant expression",
                        this->Source()));
                    return rightConstexpr;
               }
               if (!std::holds_alternative<std::uint64_t>(leftConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the subtraction did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               if (!std::holds_alternative<std::uint64_t>(rightConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The right hand side of the subtraction did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               const auto leftValue = std::get<std::uint64_t>(leftConstexpr->variant);
               const auto rightValue = std::get<std::uint64_t>(rightConstexpr->variant);

               return ConstantEvaluatedResult{leftValue - rightValue, this->Source()};
          };

     private:
          Expression _left;
          Expression _right;
     };

     class MultiplicationNode final : public NodeBase
     {
     public:
          explicit MultiplicationNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Subtraction, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " * " +
                      this->_right->Value()->ToString(0);
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
                    return NodeBase::TryConstantEvaluate(evaluationContext);

               auto leftConstexpr = this->_left->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!leftConstexpr.has_value())
               {
                    leftConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the multiplication did not evaluate to a constant expression",
                        this->Source()));
                    return leftConstexpr;
               }
               auto rightConstexpr = this->_right->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!rightConstexpr.has_value())
               {
                    rightConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the multiplication did not evaluate to a constant expression",
                        this->Source()));
                    return rightConstexpr;
               }
               if (!std::holds_alternative<std::uint64_t>(leftConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the multiplication did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               if (!std::holds_alternative<std::uint64_t>(rightConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The right hand side of the multiplication did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               const auto leftValue = std::get<std::uint64_t>(leftConstexpr->variant);
               const auto rightValue = std::get<std::uint64_t>(rightConstexpr->variant);

               return ConstantEvaluatedResult{leftValue * rightValue, this->Source()};
          };

     private:
          Expression _left;
          Expression _right;
     };

     class DivideNode final : public NodeBase
     {

     public:
          explicit DivideNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Subtraction, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " / " +
                      this->_right->Value()->ToString(0);
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
                    return NodeBase::TryConstantEvaluate(evaluationContext);

               auto leftConstexpr = this->_left->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!leftConstexpr.has_value())
               {
                    leftConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the division did not evaluate to a constant expression",
                        this->Source()));
                    return leftConstexpr;
               }
               auto rightConstexpr = this->_right->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!rightConstexpr.has_value())
               {
                    rightConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the division did not evaluate to a constant expression",
                        this->Source()));
                    return rightConstexpr;
               }
               if (!std::holds_alternative<std::uint64_t>(leftConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the division did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               if (!std::holds_alternative<std::uint64_t>(rightConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The right hand side of the division did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               const auto leftValue = std::get<std::uint64_t>(leftConstexpr->variant);
               const auto rightValue = std::get<std::uint64_t>(rightConstexpr->variant);
               if (rightValue == 0)
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(
                        std::make_unique<diagnostics::ConstantEvaluationError>("Cannot divide by 0", this->Source()));
                    return result;
               }

               return ConstantEvaluatedResult{leftValue / rightValue, this->Source()};
          };

     private:
          Expression _left;
          Expression _right;
     };

     class ModuloNode final : public NodeBase
     {

     public:
          explicit ModuloNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Subtraction, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " % " +
                      this->_right->Value()->ToString(0);
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
                    return NodeBase::TryConstantEvaluate(evaluationContext);

               auto leftConstexpr = this->_left->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!leftConstexpr.has_value())
               {
                    leftConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the modulo did not evaluate to a constant expression", this->Source()));
                    return leftConstexpr;
               }
               auto rightConstexpr = this->_right->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!rightConstexpr.has_value())
               {
                    rightConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the modulo did not evaluate to a constant expression", this->Source()));
                    return rightConstexpr;
               }
               if (!std::holds_alternative<std::uint64_t>(leftConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The left hand side of the modulo did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               if (!std::holds_alternative<std::uint64_t>(rightConstexpr->variant))
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "The right hand side of the modulo did not evaluate to an integral or floating-point "
                        "result",
                        this->Source()));
                    return result;
               }
               const auto leftValue = std::get<std::uint64_t>(leftConstexpr->variant);
               const auto rightValue = std::get<std::uint64_t>(rightConstexpr->variant);
               if (rightValue == 0)
               {
                    auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
                    result.error().push(
                        std::make_unique<diagnostics::ConstantEvaluationError>("Cannot divide by 0", this->Source()));
                    return result;
               }

               return ConstantEvaluatedResult{leftValue / rightValue, this->Source()};
          };

     private:
          Expression _left;
          Expression _right;
     };

     class CompareExchangeNode final : public NodeBase
     {
     public:
          explicit CompareExchangeNode(Expression address, Expression expected, Expression replacement, bool isWeak,
                                       MemoryOrdering ordering, Location source)
              : NodeBase(NodeKind::CompareExchange, source), _address(std::move(address)),
                _expected(std::move(expected)), _replacement(std::move(replacement)), _isWeak(isWeak),
                _ordering(ordering)
          {
          }

          [[nodiscard]] const Expression& Address(void) const noexcept { return _address; }
          [[nodiscard]] const Expression& Expected(void) const noexcept { return _expected; }
          [[nodiscard]] const Expression& Replacement(void) const noexcept { return _replacement; }
          [[nodiscard]] bool IsWeak(void) const noexcept { return _isWeak; }
          [[nodiscard]] MemoryOrdering OrderingMode(void) const noexcept { return _ordering; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__cmpxchg(...)";
          }

     private:
          Expression _address;
          Expression _expected;
          Expression _replacement;
          bool _isWeak;
          MemoryOrdering _ordering;
     };

     class LoadNode final : public NodeBase
     {
     public:
          explicit LoadNode(std::string address, Location source)
              : NodeBase(NodeKind::Load, source), _address(std::move(address))
          {
          }

          [[nodiscard]] const std::string& Address(void) const noexcept { return this->_address; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_address;
          }

     private:
          std::string _address;
     };

     class ParameterNode final : public NodeBase
     {
     public:
          explicit ParameterNode(std::uint64_t index, Location source)
              : NodeBase(NodeKind::IncomingParameter, source), _index(index)
          {
          }

          [[nodiscard]] std::uint64_t Index(void) const noexcept { return this->_index; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + std::format("__param#{}", this->_index);
          }

     private:
          std::uint64_t _index;
     };

     class StoreNode final : public NodeBase
     {
     public:
          explicit StoreNode(std::string address, Expression value, Location source)
              : NodeBase(NodeKind::Store, source), _address(std::move(address)), _value(std::move(value))
          {
          }

          [[nodiscard]] const std::string& Address(void) const noexcept { return this->_address; }
          [[nodiscard]] const Expression& Value(void) const noexcept { return this->_value; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_address + " = " +
                      this->_value->Value()->ToString(0);
          }

     private:
          std::string _address;
          Expression _value;
     };

     class AddressOfNode final : public NodeBase
     {
     public:
          explicit AddressOfNode(Expression operand, Location source)
              : NodeBase(NodeKind::AddressOf, source), _operand(std::move(operand))
          {
          }

          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__address_of(" +
                      this->_operand->Value()->ToString(0) + ")";
          }

     private:
          Expression _operand;
     };

     class DereferenceNode final : public NodeBase
     {
     public:
          explicit DereferenceNode(Expression operand, Location source)
              : NodeBase(NodeKind::Dereference, source), _operand(std::move(operand))
          {
          }

          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__dereference(" +
                      this->_operand->Value()->ToString(0) + ")";
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
                    return NodeBase::TryConstantEvaluate(evaluationContext);

               auto operandConstexpr = this->_operand->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!operandConstexpr.has_value())
               {
                    operandConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "Indirected operand did not evaluate to a constant expression", this->Source()));
                    return operandConstexpr;
               }
               if (std::holds_alternative<ConstantAggregateArray>(operandConstexpr->variant))
               {
                    const auto& value = std::get<ConstantAggregateArray>(operandConstexpr->variant);
                    return ConstantEvaluatedResult{value.members[0], this->Source()};
               }

               auto result = std::unexpected<std::stack<diagnostics::DiagnosticsMessage>>(std::in_place_t{});
               result.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                   "The operand did not evaluate to an an operand eligible for indirection", this->Source()));
               return result;
          }

     private:
          Expression _operand;
     };

     class TemporaryIntegerArrayDecayNode final : public NodeBase
     {
     public:
          explicit TemporaryIntegerArrayDecayNode(Expression operand, Location source)
              : NodeBase(NodeKind::IntegerArrayDecay, source), _operand(std::move(operand)),
                _referencedArray(dynamic_cast<IntegerArrayNode*>(this->_operand->Value().get()))
          {
               runtime_assert(this->_referencedArray != nullptr,
                              "array decay conversion was not supplied with an array");
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') +
                      std::format("__decay({})", this->_referencedArray->ToString(0));
          }

          [[nodiscard]] const std::vector<std::uint32_t>& Values(void) const noexcept
          {
               return this->_referencedArray->Values();
          }
          [[nodiscard]] const typeSystem::IntegralType* Type(void) const noexcept
          {
               return this->_referencedArray->Type();
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
                    return NodeBase::TryConstantEvaluate(evaluationContext);

               return ConstantEvaluatedResult{
                   ConstantAggregateArray{
                       this->_referencedArray->Values() |
                       std::views::transform([](const std::uint32_t value)
                                             { return ConstantEvaluatedVariant{static_cast<std::uint64_t>(value)}; }) |
                       std::ranges::to<std::vector>()},
                   this->Source()};
          };

     private:
          Expression _operand;
          IntegerArrayNode* _referencedArray;
     };

     class LoadArrayDecayNode final : public NodeBase
     {
     public:
          explicit LoadArrayDecayNode(Expression operand, Location source)
              : NodeBase(NodeKind::LoadArrayDecay, source), _operand(std::move(operand)),
                _loadNode(dynamic_cast<LoadNode*>(this->_operand->Value().get()))
          {
               runtime_assert(this->_loadNode != nullptr, "load array decay supplied with a non-load operand");
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') +
                      std::format("__decay({})", this->_loadNode->ToString(0));
          }

          [[nodiscard]] const LoadNode* GetLoadNode(void) const noexcept { return this->_loadNode; }
          [[nodiscard]] const Expression& GetOperand(void) const noexcept { return this->_operand; }

     private:
          Expression _operand;
          LoadNode* _loadNode;
     };

     class ConvertNode final : public NodeBase
     {
     public:
          ConvertNode(Expression operand, ecpps::typeSystem::NonowningTypePointer targetType, Location source)
              : NodeBase(NodeKind::Convert, source), _operand(std::move(operand)), _targetType(targetType)
          {
          }

          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] ecpps::typeSystem::NonowningTypePointer TargetType(void) const noexcept
          {
               return this->_targetType;
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__convert<" + _targetType->RawName() + ">(" +
                      _operand->Value()->ToString(0) + ")";
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               if (ecpps::ir::GetContext().optimisations.maxConstantEvaluationDepth < evaluationContext.currentDepth)
                    return NodeBase::TryConstantEvaluate(evaluationContext);
               auto operandConstexpr = this->_operand->Value()->TryConstantEvaluate(
                   EvaluationContext{.currentDepth = evaluationContext.currentDepth + 1});
               if (!operandConstexpr.has_value())
               {
                    operandConstexpr.error().push(std::make_unique<diagnostics::ConstantEvaluationError>(
                        "Converted operand did not evaluate to a constant expression", this->Source()));
                    return operandConstexpr;
               }
               const auto& operand = *operandConstexpr;

               return operand;
          };

     private:
          Expression _operand;
          ecpps::typeSystem::NonowningTypePointer _targetType;
     };

} // namespace ecpps::ir
