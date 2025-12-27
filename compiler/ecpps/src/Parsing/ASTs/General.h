#pragma once
#include "../AST.h"
namespace ecpps::ast
{
     class IdentifierNode final : public Node
     {
     public:
          explicit IdentifierNode(std::string value, Location where) : Node(where), _value(std::move(value)) {}
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
          explicit OperatorFunctionId(Operator op, Location where) : Node(where), _operator(op) {}

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
          explicit QualifiedIdNode(std::vector<NodePointer> path, Location where) : Node(where), _path(std::move(path))
          {
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built{};
               for (const auto& element : this->_path) built += element->ToString(0) + "::";
               if (!built.empty())
               {
                    built.pop_back();
                    built.pop_back();
               }
               return std::string(indent * PrettyIndent, ' ') + built;
          }

     private:
          std::vector<NodePointer> _path;
     };
} // namespace ecpps::ast
