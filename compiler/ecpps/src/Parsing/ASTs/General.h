#pragma once
#include "../AST.h"
namespace ecpps::ast
{
     class IdentifierNode final : public Node
     {
     public:
          explicit IdentifierNode(std::string value, Location where) : Node(std::move(where)), _value(std::move(value))
          {
          }
          [[nodiscard]] const std::string& Value(void) const noexcept { return this->_value; }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + this->_value;
          }

     private:
          std::string _value;
     };

     class OperatorFunctionId final : Node
     {
     public:
          explicit OperatorFunctionId(Operator op, Location where) : Node(std::move(where)), _operator(std::move(op)) {}

          [[nodiscard]] Operator GetOperator(void) const noexcept { return this->_operator; }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "operator" + ecpps::ast::ToString(this->_operator);
          }

     private:
          Operator _operator;
     };

     class QualifiedIdNode final : public Node
     {
     public:
     private:
          NodePointer _unqualified;
          SBOVector<NodePointer> _path;
     };
} // namespace ecpps::ast