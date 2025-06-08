#pragma once
#include <string>
#include <vector>
#include "../Machine/ABI.h"
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
          explicit ProcedureNode(const abi::Linkage linkage, const abi::CallingConventionName callingConvention,
                                 typeSystem::TypePointer returnType, std::string name,
                                 std::vector<Parameter> parameterList, std::vector<NodePointer> body)
              : NodeBase(NodeKind::Procedure), _linkage(linkage), _callingConvention(callingConvention),
                _returnType(std::move(returnType)), _name(std::move(name)), _parameterList(std::move(parameterList)),
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
               built +=
                   this->_returnType->RawName() + " " + ::ToString(this->_callingConvention) + " " + this->_name + "(";
               built += ")\n" + std::string(indent * ast::PrettyIndent, ' ') + "{\n";
               for (const auto& line : this->_body) built += line->ToString(indent + 1) + "\n";
               return built + std::string(indent * ast::PrettyIndent, ' ') + "}";
          }
          [[nodiscard]] abi::CallingConventionName CallingConvention(void) const noexcept
          {
               return this->_callingConvention;
          }
          [[nodiscard]] abi::Linkage Linkage(void) const noexcept { return this->_linkage; }
          [[nodiscard]] const typeSystem::TypePointer& ReturnType(void) const noexcept { return this->_returnType; }

     private:
          abi::Linkage _linkage;
          abi::CallingConventionName _callingConvention;
          typeSystem::TypePointer _returnType;
          std::string _name;
          std::vector<Parameter> _parameterList;
          std::vector<NodePointer> _body;
     };
} // namespace ecpps::ir