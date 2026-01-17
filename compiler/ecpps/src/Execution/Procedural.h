#pragma once
#include <string>
#include <vector>
#include "../Machine/ABI.h"
#include "../Parsing/AST.h"
#include "../TypeSystem/TypeBase.h"
#include "Context.h"
#include "Expressions.h"
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
                                 std::vector<FunctionScope::Parameter> parameterList,
                                 std::vector<FunctionScope::Variable> locals, std::vector<NodePointer> body,
                                 Location source)
              : NodeBase(NodeKind::Procedure, source), _linkage(linkage), _callingConvention(callingConvention),
                _returnType(std::move(returnType)), _name(std::move(name)), _parameterList(std::move(parameterList)),
                _locals(std::move(locals)), _body(std::move(body))
          {
          }

          [[nodiscard]] const std::string& Name(void) const noexcept { return this->_name; }
          [[nodiscard]] const std::vector<FunctionScope::Parameter>& ParameterList(void) const noexcept
          {
               return this->_parameterList;
          }
          [[nodiscard]] const std::vector<FunctionScope::Variable>& Locals(void) const noexcept
          {
               return this->_locals;
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
          std::vector<FunctionScope::Parameter> _parameterList;
          std::vector<FunctionScope::Variable> _locals;
          std::vector<NodePointer> _body;
     };

     class FunctionCallNode final : public NodeBase
     {
     public:
          explicit FunctionCallNode(std::shared_ptr<FunctionScope> function, std::vector<Expression> arguments,
                                    Location source)
              : NodeBase(NodeKind::Call, source), _function(std::move(function)), _arguments(std::move(arguments))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string args{};
               for (const auto& arg : this->_arguments)
                    args += (arg == nullptr ? "__unknown" : arg->Value()->ToString(0)) + ", ";
               if (!args.empty())
               {
                    args.pop_back();
                    args.pop_back();
               }
               return std::string(indent * ast::PrettyIndent, ' ') + this->_function->name + "(" + args + ")";
          }

          [[nodiscard]] const std::shared_ptr<FunctionScope>& Function(void) const noexcept { return this->_function; }
          [[nodiscard]] const std::vector<Expression>& Arguments(void) const noexcept { return this->_arguments; }

     private:
          std::shared_ptr<FunctionScope> _function;
          std::vector<Expression> _arguments{};
     };
} // namespace ecpps::ir
