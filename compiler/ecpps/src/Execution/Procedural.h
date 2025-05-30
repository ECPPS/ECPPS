#pragma once
#include <vector>
#include "../Parsing/AST.h"
#include "../TypeSystem/TypeBase.h"
#include "NodeBase.h"

namespace ecpps::ir
{
     struct Parameter
     {
          ecpps::typeSystem::TypePointer type;
          std::string name;
     };

     class ProcedureNode final : public NodeBase
     {
     public:
          explicit ProcedureNode(std::string name, std::vector<Parameter> parameterList, std::vector<NodePointer> body)
              : NodeBase(NodeKind::Procedure), _name(std::move(name)), _parameterList(std::move(parameterList)),
                _body(std::move(body))
          {
          }

          [[nodiscard]] const std::string& Name(void) const noexcept { return this->_name; }
          [[nodiscard]] const std::vector<Parameter>& ParameterList(void) const noexcept
          {
               return this->_parameterList;
          }
          [[nodiscard]] const std::vector<NodePointer>& Body(void) const noexcept { return this->_body; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built(indent * ast::PrettyIndent, ' ');
               built += this->_name + "(";
               built += ")\n" + std::string(indent * ast::PrettyIndent, ' ') + "\n";
               for (const auto& line : this->_body) built += line->ToString(indent + 1) + "\n";
               return built + std::string(indent * ast::PrettyIndent, ' ') + "}";
          }

     private:
          std::string _name;
          std::vector<Parameter> _parameterList;
          std::vector<NodePointer> _body;
     };
} // namespace ecpps::ir