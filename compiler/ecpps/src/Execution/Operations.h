#pragma once

#include <RuntimeAssert.h>
#include <cstdint>
#include <optional>
#include "../Parsing/AST.h"
#include "Context.h"
#include "Expressions.h"
#include "NodeBase.h"

namespace ecpps::ir
{
     enum struct BinaryOperationLevel : std::uint_fast8_t
     {
          None,
          Atomic,
          Assign,
          AtomicAssign,
     };

     enum struct MemoryOrdering : std::uint_fast8_t
     {
          Acquire,
          Release,
          Relaxed,
          Sequenced,
     };

     class SSARegisterReferenceNode final : public NodeBase
     {
     public:
          explicit SSARegisterReferenceNode(const SingleAssignRegisterNode* reg, Location source)
              : NodeBase(NodeKind::Load, source), _reg(reg)
          {
               runtime_assert(this->_reg != nullptr, "Register reference cannot be null");

               this->_reg->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Reg() const noexcept { return *this->_reg; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_reg->ToString(0);
          }

          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               return NodeBase::TryConstantEvaluate(evaluationContext);
          }

     private:
          const SingleAssignRegisterNode* _reg;
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

     class SSALoadNode final : public NodeBase
     {
     public:
          explicit SSALoadNode(SSAPointer result, const SingleAssignRegisterNode* address, Location source)
              : NodeBase(NodeKind::Load, source), _result(std::move(result)), _address(address)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_address != nullptr, "Invalid SSA address");

               this->_address->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Address() const noexcept { return *this->_address; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = __load({})", ' ', indent * ast::PrettyIndent, this->_result->ToString(0),
                                  this->_address->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _address;
     };

     class SSAAddNode final : public NodeBase
     {
     public:
          explicit SSAAddNode(SSAPointer result, const SingleAssignRegisterNode* left,
                              const SingleAssignRegisterNode* right, Location source)
              : NodeBase(NodeKind::Addition, source), _result(std::move(result)), _left(left), _right(right)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_left != nullptr, "Invalid SSA left operand");
               runtime_assert(this->_right != nullptr, "Invalid SSA right operand");

               this->_left->Use();
               this->_right->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Left() const noexcept { return *this->_left; }
          [[nodiscard]] const SingleAssignRegisterNode& Right() const noexcept { return *this->_right; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = {} + {}", ' ', indent * ast::PrettyIndent, this->_result->ToString(0),
                                  this->_left->ToString(0), this->_right->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _left;
          const SingleAssignRegisterNode* _right;
     };

     class SSASubNode final : public NodeBase
     {
     public:
          explicit SSASubNode(SSAPointer result, const SingleAssignRegisterNode* left,
                              const SingleAssignRegisterNode* right, Location source)
              : NodeBase(NodeKind::Subtraction, source), _result(std::move(result)), _left(left), _right(right)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_left != nullptr, "Invalid SSA left operand");
               runtime_assert(this->_right != nullptr, "Invalid SSA right operand");

               this->_left->Use();
               this->_right->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Left() const noexcept { return *this->_left; }
          [[nodiscard]] const SingleAssignRegisterNode& Right() const noexcept { return *this->_right; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = {} - {}", ' ', indent * ast::PrettyIndent, this->_result->ToString(0),
                                  this->_left->ToString(0), this->_right->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _left;
          const SingleAssignRegisterNode* _right;
     };

     class SSAMulNode final : public NodeBase
     {
     public:
          explicit SSAMulNode(SSAPointer result, const SingleAssignRegisterNode* left,
                              const SingleAssignRegisterNode* right, Location source)
              : NodeBase(NodeKind::Multiplication, source), _result(std::move(result)), _left(left), _right(right)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_left != nullptr, "Invalid SSA left operand");
               runtime_assert(this->_right != nullptr, "Invalid SSA right operand");

               this->_left->Use();
               this->_right->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Left() const noexcept { return *this->_left; }
          [[nodiscard]] const SingleAssignRegisterNode& Right() const noexcept { return *this->_right; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = {} * {}", ' ', indent * ast::PrettyIndent, this->_result->ToString(0),
                                  this->_left->ToString(0), this->_right->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _left;
          const SingleAssignRegisterNode* _right;
     };

     class SSADivNode final : public NodeBase
     {
     public:
          explicit SSADivNode(SSAPointer result, const SingleAssignRegisterNode* left,
                              const SingleAssignRegisterNode* right, Location source)
              : NodeBase(NodeKind::Division, source), _result(std::move(result)), _left(left), _right(right)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_left != nullptr, "Invalid SSA left operand");
               runtime_assert(this->_right != nullptr, "Invalid SSA right operand");

               this->_left->Use();
               this->_right->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Left() const noexcept { return *this->_left; }
          [[nodiscard]] const SingleAssignRegisterNode& Right() const noexcept { return *this->_right; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = {} / {}", ' ', indent * ast::PrettyIndent, this->_result->ToString(0),
                                  this->_left->ToString(0), this->_right->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _left;
          const SingleAssignRegisterNode* _right;
     };

     class SSAModNode final : public NodeBase
     {
     public:
          explicit SSAModNode(SSAPointer result, const SingleAssignRegisterNode* left,
                              const SingleAssignRegisterNode* right, Location source)
              : NodeBase(NodeKind::Modulo, source), _result(std::move(result)), _left(left), _right(right)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_left != nullptr, "Invalid SSA left operand");
               runtime_assert(this->_right != nullptr, "Invalid SSA right operand");

               this->_left->Use();
               this->_right->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Left() const noexcept { return *this->_left; }
          [[nodiscard]] const SingleAssignRegisterNode& Right() const noexcept { return *this->_right; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = {} % {}", ' ', indent * ast::PrettyIndent, this->_result->ToString(0),
                                  this->_left->ToString(0), this->_right->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _left;
          const SingleAssignRegisterNode* _right;
     };

     class SSAImmNode final : public NodeBase
     {
     public:
          explicit SSAImmNode(SSAPointer result, std::uint64_t value, Location source)
              : NodeBase(NodeKind::Integer, source), _result(std::move(result)), _value(value)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] std::uint64_t Value() const noexcept { return this->_value; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = {}", ' ', indent * ast::PrettyIndent, this->_result->ToString(0),
                                  this->_value);
          }

     private:
          SSAPointer _result;
          std::uint64_t _value;
     };

     class SSAConvertNode final : public NodeBase
     {
     public:
          explicit SSAConvertNode(SSAPointer result, const SingleAssignRegisterNode* src,
                                  ecpps::typeSystem::NonowningTypePointer targetType, Location source)
              : NodeBase(NodeKind::Convert, source), _result(std::move(result)), _src(src), _targetType(targetType)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_src != nullptr, "Invalid SSA source operand");

               this->_src->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Src() const noexcept { return *this->_src; }
          [[nodiscard]] ecpps::typeSystem::NonowningTypePointer TargetType() const noexcept
          {
               return this->_targetType;
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = __convert<{}>({}) ", ' ', indent * ast::PrettyIndent,
                                  this->_result->ToString(0), this->_targetType->RawName(), this->_src->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _src;
          ecpps::typeSystem::NonowningTypePointer _targetType;
     };

     class SSAPointerConvertNode final : public NodeBase
     {
     public:
          explicit SSAPointerConvertNode(SSAPointer result, const SingleAssignRegisterNode* src,
                                         ecpps::typeSystem::NonowningTypePointer targetType, Location source)
              : NodeBase(NodeKind::PointerConversion, source), _result(std::move(result)), _src(src),
                _targetType(targetType)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_src != nullptr, "Invalid SSA source operand");

               this->_src->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Src() const noexcept { return *this->_src; }
          [[nodiscard]] ecpps::typeSystem::NonowningTypePointer TargetType() const noexcept
          {
               return this->_targetType;
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = __pointer_cast<{}>({}) ", ' ', indent * ast::PrettyIndent,
                                  this->_result->ToString(0), this->_targetType->RawName(), this->_src->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _src;
          ecpps::typeSystem::NonowningTypePointer _targetType;
     };

     class SSAPointerConvertFromDecayNode final : public NodeBase
     {
     public:
          explicit SSAPointerConvertFromDecayNode(SSAPointer result, NodePointer decayNode,
                                                  ecpps::typeSystem::NonowningTypePointer targetType, Location source)
              : NodeBase(NodeKind::PointerConversion, source), _result(std::move(result)),
                _decayNode(std::move(decayNode)), _targetType(targetType)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_decayNode != nullptr, "Invalid decay node");
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const NodeBase& DecayNode() const noexcept { return *this->_decayNode; }
          [[nodiscard]] ecpps::typeSystem::NonowningTypePointer TargetType() const noexcept
          {
               return this->_targetType;
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = __pointer_cast<{}>({}) ", ' ', indent * ast::PrettyIndent,
                                  this->_result->ToString(0), this->_targetType->RawName(),
                                  this->_decayNode->ToString(0));
          }

     private:
          SSAPointer _result;
          NodePointer _decayNode;
          ecpps::typeSystem::NonowningTypePointer _targetType;
     };

     class SSAStoreNode final : public NodeBase
     {
     public:
          explicit SSAStoreNode(const SingleAssignRegisterNode* target, const SingleAssignRegisterNode* src,
                                Location source)
              : NodeBase(NodeKind::Store, source), _target(target), _src(src)
          {
               runtime_assert(this->_target != nullptr, "Invalid SSA target");
               runtime_assert(this->_src != nullptr, "Invalid SSA source");

               this->_target->Use();
               this->_src->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Target() const noexcept { return *this->_target; }
          [[nodiscard]] const SingleAssignRegisterNode& Src() const noexcept { return *this->_src; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}store {} = {}", ' ', indent * ast::PrettyIndent, this->_target->ToString(0),
                                  this->_src->ToString(0));
          }

     private:
          const SingleAssignRegisterNode* _target;
          const SingleAssignRegisterNode* _src;
     };
     class SSAStoreIntegerNode final : public NodeBase
     {
     public:
          explicit SSAStoreIntegerNode(const SingleAssignRegisterNode* target, std::size_t src, Location source)
              : NodeBase(NodeKind::Store, source), _target(target), _src(src)
          {
               runtime_assert(this->_target != nullptr, "Invalid SSA target");

               this->_target->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Target(void) const noexcept { return *this->_target; }
          [[nodiscard]] std::size_t Src(void) const noexcept { return this->_src; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}store {} = {}", ' ', indent * ast::PrettyIndent, this->_target->ToString(0),
                                  this->_src);
          }

     private:
          const SingleAssignRegisterNode* _target;
          std::size_t _src;
     };

     class SSAArrayStoreNode final : public NodeBase
     {
     public:
          explicit SSAArrayStoreNode(const SingleAssignRegisterNode* target, NodePointer arrayNode, Location source)
              : NodeBase(NodeKind::Store, source), _target(target), _arrayNode(std::move(arrayNode))
          {
               runtime_assert(this->_target != nullptr, "Invalid SSA target");
               runtime_assert(this->_arrayNode != nullptr, "Invalid array node");

               this->_target->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Target() const noexcept { return *this->_target; }
          [[nodiscard]] const NodeBase& ArrayNode() const noexcept { return *this->_arrayNode; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}store {} = {}", ' ', indent * ast::PrettyIndent, this->_target->ToString(0),
                                  this->_arrayNode->ToString(0));
          }

     private:
          const SingleAssignRegisterNode* _target;
          NodePointer _arrayNode;
     };

     class SSAAddressOfNode final : public NodeBase
     {
     public:
          explicit SSAAddressOfNode(SSAPointer result, const SingleAssignRegisterNode* operand, Location source)
              : NodeBase(NodeKind::AddressOf, source), _result(std::move(result)), _operand(operand)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_operand != nullptr, "Invalid SSA operand");

               this->_operand->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Operand() const noexcept { return *this->_operand; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = __address_of({})", ' ', indent * ast::PrettyIndent,
                                  this->_result->ToString(0), this->_operand->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _operand;
     };

     class SSADerefNode final : public NodeBase
     {
     public:
          explicit SSADerefNode(SSAPointer result, const SingleAssignRegisterNode* ptr, Location source)
              : NodeBase(NodeKind::Dereference, source), _result(std::move(result)), _ptr(ptr)
          {
               runtime_assert(this->_result != nullptr, "Invalid SSA result");
               runtime_assert(this->_ptr != nullptr, "Invalid SSA pointer operand");

               this->_ptr->Use();
          }

          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result; }
          [[nodiscard]] const SingleAssignRegisterNode& Ptr() const noexcept { return *this->_ptr; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{: <{}}{} = __deref({})", ' ', indent * ast::PrettyIndent,
                                  this->_result->ToString(0), this->_ptr->ToString(0));
          }

     private:
          SSAPointer _result;
          const SingleAssignRegisterNode* _ptr;
     };

     class SSACallNode final : public NodeBase
     {
     public:
          explicit SSACallNode(std::optional<SSAPointer> result, const FunctionScope* function,
                               std::vector<const SingleAssignRegisterNode*> arguments, Location source)
              : NodeBase(NodeKind::Call, source), _result(std::move(result)), _function(function),
                _arguments(std::move(arguments))
          {
               runtime_assert(this->_function != nullptr, "Invalid function scope");

               for (const auto& argument : arguments) argument->Use();
          }

          [[nodiscard]] bool HasResult() const noexcept { return this->_result.has_value(); }
          [[nodiscard]] const SingleAssignRegisterNode& Result() const noexcept { return *this->_result.value(); }
          [[nodiscard]] const FunctionScope& Function() const noexcept { return *this->_function; }
          [[nodiscard]] const std::vector<const SingleAssignRegisterNode*>& Arguments() const noexcept
          {
               return this->_arguments;
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               std::string args;
               bool first = true;
               for (const auto* arg : this->_arguments)
               {
                    if (!first) args += ", ";
                    first = false;
                    args += arg->ToString(0);
               }
               const std::string call = std::format("{}({})", this->_function->Name().value_or("__unknown"), args);
               if (this->_result.has_value())
                    return std::format("{: <{}}{} = {}", ' ', indent * ast::PrettyIndent,
                                       this->_result.value()->ToString(0), call);
               return std::format("{: <{}}{}", ' ', indent * ast::PrettyIndent, call);
          }

     private:
          std::optional<SSAPointer> _result;
          const FunctionScope* _function;
          std::vector<const SingleAssignRegisterNode*> _arguments;
     };

     class SSAReturnNode final : public NodeBase
     {
     public:
          explicit SSAReturnNode(const SingleAssignRegisterNode* operand, Location source)
              : NodeBase(NodeKind::Return, source), _operand(operand)
          {
               if (this->_operand != nullptr) this->_operand->Use();
          }

          [[nodiscard]] bool HasOperand() const noexcept { return this->_operand != nullptr; }
          [[nodiscard]] const SingleAssignRegisterNode* Operand() const noexcept { return this->_operand; }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               if (this->_operand == nullptr) return std::format("{: <{}}return", ' ', indent * ast::PrettyIndent);
               return std::format("{: <{}}return {}", ' ', indent * ast::PrettyIndent, this->_operand->ToString(0));
          }

     private:
          const SingleAssignRegisterNode* _operand;
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

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') +
                      std::format("__decay({})", this->_referencedArray->ToString(0));
          }

          [[nodiscard]] const std::vector<std::uint32_t>& Values() const noexcept
          {
               return this->_referencedArray->Values();
          }
          [[nodiscard]] const typeSystem::IntegralType* Type() const noexcept { return this->_referencedArray->Type(); }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override
          {
               if (ecpps::ir::GetTypeContext().optimisations.maxConstantEvaluationDepth <
                   evaluationContext.currentDepth)
                    return NodeBase::TryConstantEvaluate(evaluationContext);

               return ConstantEvaluatedResult{
                   ConstantAggregateArray{
                       this->_referencedArray->Values() |
                       std::views::transform([](const std::uint32_t value)
                                             { return ConstantEvaluatedVariant{static_cast<std::uint64_t>(value)}; }) |
                       std::ranges::to<std::vector>()},
                   this->Source()};
          }

     private:
          Expression _operand;
          IntegerArrayNode* _referencedArray;
     };

     class LoadArrayDecayNode final : public NodeBase
     {
     public:
          explicit LoadArrayDecayNode(Expression operand, Location source)
              : NodeBase(NodeKind::LoadArrayDecay, source), _operand(std::move(operand)), _allocReg(nullptr)
          {
               if (const auto* ref = dynamic_cast<const SSARegisterReferenceNode*>(this->_operand->Value().get()))
                    _allocReg = &ref->Reg();
               runtime_assert(this->_allocReg != nullptr, "load array decay supplied with a non-register operand");
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') +
                      std::format("__decay({})", this->_allocReg->ToString(0));
          }

          [[nodiscard]] const SingleAssignRegisterNode* GetAllocReg() const noexcept { return this->_allocReg; }
          [[nodiscard]] const Expression& GetOperand() const noexcept { return this->_operand; }

     private:
          Expression _operand;
          const SingleAssignRegisterNode* _allocReg;
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

          [[nodiscard]] const Expression& Address() const noexcept { return _address; }
          [[nodiscard]] const Expression& Expected() const noexcept { return _expected; }
          [[nodiscard]] const Expression& Replacement() const noexcept { return _replacement; }
          [[nodiscard]] bool IsWeak() const noexcept { return _isWeak; }
          [[nodiscard]] MemoryOrdering OrderingMode() const noexcept { return _ordering; }

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

     class AllocationNode final : public NodeBase
     {
     public:
          explicit AllocationNode(std::size_t size, std::size_t alignment,
                                  std::unique_ptr<SingleAssignRegisterNode, IRDeleter> ssa, Location source)
              : NodeBase(NodeKind::Allocate, source), _size(size), _alignment(alignment), _ssa(std::move(ssa))
          {
               runtime_assert(this->_ssa != nullptr, "Invalid SSA node");
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::format("{} = alloc[size={}, align={}]", this->_ssa->ToString(indent), this->_size,
                                  this->_alignment);
          }
          [[nodiscard]] std::size_t Size() const noexcept { return this->_size; }
          [[nodiscard]] std::size_t Alignment() const noexcept { return this->_alignment; }
          [[nodiscard]] const SingleAssignRegisterNode& Node() const noexcept { return *this->_ssa; }

     private:
          std::size_t _size;
          std::size_t _alignment;
          std::unique_ptr<SingleAssignRegisterNode, IRDeleter> _ssa;
     };

     class ParameterNode final : public NodeBase
     {
     public:
          explicit ParameterNode(std::uint64_t index, Location source)
              : NodeBase(NodeKind::IncomingParameter, source), _index(index)
          {
          }

          [[nodiscard]] std::uint64_t Index() const noexcept { return this->_index; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + std::format("__param#{}", this->_index);
          }

     private:
          std::uint64_t _index;
     };
} // namespace ecpps::ir
