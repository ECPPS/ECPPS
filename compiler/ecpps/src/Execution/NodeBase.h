#pragma once
#include <TypeSystem/ArithmeticTypes.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <print>
#include <stack>
#include <string>
#include <utility>
#include "../Parsing/AST.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Error.h"

namespace ecpps::ir
{
     enum struct NodeKind : std::uint_fast8_t
     {
          Integer,
          Procedure,
          Return,
          Addition,
          Subtraction,
          Multiplication,
          Division,
          Modulo,
          RightBitShift,
          LeftBitShift,
          And,
          Or,
          Xor,
          CompareExchange,
          Call,
          AddressOf,
          Dereference,
          Store,
          Convert,
          Load,
          IntegerArray,
          IntegerArrayDecay,
          LoadArrayDecay,
          PointerConversion,
          IncomingParameter,
          SSA,
          Allocate,
          Annotation
     };

     struct ConstantAggregateMap;
     struct ConstantAggregateArray;

     using ConstantEvaluatedVariant =
          std::variant<std::monostate, std::uint64_t, std::double_t, ConstantAggregateMap, ConstantAggregateArray>;

     struct ConstantAggregateMap
     {
          std::unordered_map<std::string, ConstantEvaluatedVariant> members{};
     };

     struct ConstantAggregateArray
     {
          std::vector<ConstantEvaluatedVariant> members{};
     };
     struct EvaluationContext
     {
          std::uint32_t currentDepth{};
          std::optional<std::vector<ConstantEvaluatedVariant>> functionArguments = std::nullopt;
     };
     struct ConstantEvaluatedResult
     {
          ConstantEvaluatedVariant variant;
          Location source;

          explicit ConstantEvaluatedResult(ConstantEvaluatedVariant variant, const Location& source)
              : variant(std::move(variant)), source(source)
          {
          }
     };

     class NodeBase
     {
     public:
          explicit NodeBase(const NodeKind kind, const Location& source) : _kind(kind), _source(source)
          {
          }
          virtual ~NodeBase(void) = default;

          [[nodiscard]] virtual std::string ToString(std::size_t indent) const = 0;

          [[nodiscard]] NodeKind Kind(void) const noexcept
          {
               return this->_kind;
          }
          [[nodiscard]] const Location& Source(void) const noexcept
          {
               return this->_source;
          }
          [[nodiscard]] virtual std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const;

     private:
          NodeKind _kind;
          Location _source;
     };

     using IRDeleter = BumpAllocator::Deleter<NodeBase>;

     using NodePointer = std::unique_ptr<NodeBase, IRDeleter>;

     class IntegralNode final : public NodeBase
     {
     public:
          explicit IntegralNode(const std::uint64_t value, Location source)
              : NodeBase(NodeKind::Integer, source), _value(value)
          {
          }

          [[nodiscard]] std::uint64_t Value(void) const noexcept
          {
               return this->_value;
          }
          [[nodiscard]] std::uint64_t Value(const std::uint64_t newValue) noexcept
          {
               return std::exchange(this->_value, newValue);
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + std::to_string(this->_value);
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate([[maybe_unused]] const EvaluationContext& evaluationContext) const override
          {
               return ConstantEvaluatedResult{this->_value, this->Source()};
          }

     private:
          std::uint64_t _value; // TODO: Support huge integers for vectorisation
     };

     class IntegerArrayNode final : public NodeBase
     {
     public:
          explicit IntegerArrayNode(std::vector<std::uint32_t> values, const typeSystem::IntegralType* type,
                                    Location source)
              : NodeBase(NodeKind::IntegerArray, source), _values(std::move(values)), _type(type)
          {
          }

          [[nodiscard]] const std::vector<std::uint32_t>& Values(void) const noexcept
          {
               return this->_values;
          }
          [[nodiscard]] const typeSystem::IntegralType* Type(void) const noexcept
          {
               return this->_type;
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + std::format("{}", this->_values);
          }
          [[nodiscard]] std::expected<ConstantEvaluatedResult, std::stack<diagnostics::DiagnosticsMessage>>
          TryConstantEvaluate(const EvaluationContext& evaluationContext) const override;

     private:
          std::vector<std::uint32_t> _values;
          const typeSystem::IntegralType* _type;
     };

     // The register class is used to determinate the priority of the said register
     // to a bounded register. Certain registers will however have the same priority, but they should still be assigned
     // to a unique class code. Each priority class is a multiple of 16 (to allow up to 16 said classes per priority
     // level). 0 (>> 4) is the lowest priority (most likely to be spilled), and higher values are higher priority (less
     // likely to be spilled). Additionally, 0xfff is registered to the fixed class

#define COMPOSE_CLASS(priority, classCode) ((static_cast<std::uint_least16_t>(priority) << 4) | (classCode))

     enum struct RegisterClass : std::uint_least16_t
     {
          // variables
          LocalVariable = COMPOSE_CLASS(0, 0),     // variables
          ParameterVariable = COMPOSE_CLASS(0, 1), // parameters (they are also variables, special class anyway)

          // computation temporaries
          ArithmeticTemporary = COMPOSE_CLASS(3, 0), // built-in arithmetic operations on integers and floats
          ConditionTemporary = COMPOSE_CLASS(3, 1),  // condition results (id est the result of a == b, a < b, etc.)
          AddressTemporary = COMPOSE_CLASS(3, 2),    // the result of an address-of operation (id est &a)
          Temporary = COMPOSE_CLASS(3, 3),           // any other

          ConversionResult = COMPOSE_CLASS(6, 0), // result of any arithmetic conversion

          Fixed1 = 0xfff0,
          Fixed2 = 0xfff1,
          Fixed3 = 0xfff2,
          Fixed4 = 0xfff3,
          Fixed5 = 0xfff4,
          Fixed6 = 0xfff5,
          Fixed7 = 0xfff6,
          Fixed8 = 0xfff7,
          Fixed9 = 0xfff8,
          Fixed10 = 0xfff9,
          Fixed11 = 0xfffa,
          Fixed12 = 0xfffb,
          Fixed13 = 0xfffc,
          Fixed14 = 0xfffd,
          Fixed15 = 0xfffe,
          Fixed16 = 0xffff
     };
     struct RegisterPriorityInfo
     {
          RegisterClass regClass{};
          std::uint32_t useCount{};
          std::uint32_t depth{};
     };

     class SingleAssignRegisterNode final : public NodeBase
     {
     public:
          explicit SingleAssignRegisterNode(std::size_t index, RegisterPriorityInfo info, std::size_t width,
                                            Location source)
              : NodeBase(NodeKind::SSA, source), _index(index), _priorityInfo(info), _width(width)
          {
          }
          explicit SingleAssignRegisterNode(std::size_t index, RegisterPriorityInfo info, std::string name,
                                            std::size_t width, Location source)
              : NodeBase(NodeKind::SSA, source), _index(index), _priorityInfo(info), _optionalName(std::move(name)),
                _width(width)
          {
          }
          [[nodiscard]] std::size_t Index(void) const noexcept
          {
               return this->_index;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               if (this->_optionalName.empty())
                    return std::string(indent * ast::PrettyIndent, ' ') + std::format("__register({})", this->_index);
               return std::string(indent * ast::PrettyIndent, ' ') +
                      std::format("__register({}, \"{}\")", this->_index, this->_optionalName);
          }
          [[nodiscard]] const RegisterPriorityInfo& PriorityInfo(void) const noexcept
          {
               return this->_priorityInfo;
          }

          [[nodiscard]] std::size_t UseCount(void) const noexcept
          {
               return this->_useCount;
          }
          [[nodiscard]] std::size_t Width(void) const noexcept
          {
               return this->_width;
          }
          // In lower mode: returns true if the node is no longer used; otherwise always true
          bool Use(void) const noexcept
          {
               if (_usageIsDecrement) return --this->_useCount == 0;

               this->_useCount++;
               return true;
          }

          static void SwitchToLower(void) noexcept
          {
               _usageIsDecrement = true;
          }

     private:
          std::size_t _index;
          RegisterPriorityInfo _priorityInfo;
          std::string _optionalName;
          mutable std::size_t _useCount{};
          std::size_t _width; // TODO: Complex layout, not width+alignment

          static bool _usageIsDecrement;
     };
     using SSAPointer = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>;
} // namespace ecpps::ir
