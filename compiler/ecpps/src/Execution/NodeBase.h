#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "../Parsing/AST.h"

namespace ecpps::ir
{
     enum struct NodeKind
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
     };

     class NodeBase
     {
     public:
          explicit NodeBase(const NodeKind kind) : _kind(kind) {}
          virtual ~NodeBase(void) = default;

          [[nodiscard]] virtual std::string ToString(std::size_t indent) const = 0;

          [[nodiscard]] NodeKind Kind(void) const noexcept { return this->_kind; }

     private:
          NodeKind _kind;
     };

     using NodePointer = std::unique_ptr<NodeBase>;

     class IntegralNode final : public NodeBase
     {
     public:
          explicit IntegralNode(const std::uint64_t value) : NodeBase(NodeKind::Integer), _value(value) {}

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
} // namespace ecpps::ir