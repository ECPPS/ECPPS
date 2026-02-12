#pragma once
#include <TypeSystem/ArithmeticTypes.h>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include "../Parsing/AST.h"
#include "Context.h"

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
          LoadArrayDecay
     };

     class NodeBase
     {
     public:
          explicit NodeBase(const NodeKind kind, const Location& source) : _kind(kind), _source(source) {}
          virtual ~NodeBase(void) = default;

          [[nodiscard]] virtual std::string ToString(std::size_t indent) const = 0;

          [[nodiscard]] NodeKind Kind(void) const noexcept { return this->_kind; }
          [[nodiscard]] const Location& Source(void) const noexcept { return this->_source; }

     private:
          NodeKind _kind;
          Location _source;
     };

     using IRDeleter = BumpAllocator::Deleter;

     using NodePointer = std::unique_ptr<NodeBase, IRDeleter>;

     class IntegralNode final : public NodeBase
     {
     public:
          explicit IntegralNode(const std::uint64_t value, Location source)
              : NodeBase(NodeKind::Integer, source), _value(value)
          {
          }

          [[nodiscard]] std::uint64_t Value(void) const noexcept { return this->_value; }
          [[nodiscard]] std::uint64_t Value(const std::uint64_t newValue) noexcept
          {
               return std::exchange(this->_value, newValue);
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + std::to_string(this->_value);
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

          [[nodiscard]] const std::vector<std::uint32_t>& Values(void) const noexcept { return this->_values; }
          [[nodiscard]] const typeSystem::IntegralType* Type(void) const noexcept { return this->_type; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + std::format("{}", this->_values);
          }

     private:
          std::vector<std::uint32_t> _values;
          const typeSystem::IntegralType* _type;
     };
} // namespace ecpps::ir
