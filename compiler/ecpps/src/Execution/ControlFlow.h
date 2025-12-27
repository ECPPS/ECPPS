#pragma once
#include <string>
#include "../Parsing/AST.h"
#include "Expressions.h"
#include "NodeBase.h"

namespace ecpps::ir
{
     class ReturnNode final : public ir::NodeBase
     {
     public:
          explicit ReturnNode(Expression value, Location source)
              : NodeBase(NodeKind::Return, source), _value(std::move(value))
          {
          }

          [[nodiscard]] bool HasValue(void) const noexcept { return this->_value != nullptr; }
          [[nodiscard]] const Expression& Value(void) const noexcept { return this->_value; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               if (!HasValue()) return std::string(indent * ast::PrettyIndent, ' ') + "return;";
               return std::string(indent * ast::PrettyIndent, ' ') + "return " + this->_value->Value()->ToString(0) +
                      ";";
          }

     private:
          Expression _value;
     };
} // namespace ecpps::ir