#pragma once
#include <string>
#include <vector>
#include "../Machine/ABI.h"
#include "../Parsing/AST.h"
#include "../TypeSystem/TypeBase.h"
#include "Context.h"
#include "Execution/Context.h"
#include "Expressions.h"
#include "NodeBase.h"

namespace ecpps::ir
{
     struct Parameter
     {
          ecpps::typeSystem::NonowningTypePointer type;
          std::string name;
     };

     class ProcedureNode final : public NodeBase
     {
     public:
          explicit ProcedureNode(FunctionScope* scope, std::shared_ptr<std::vector<FunctionScope::LocalEntity>> locals,
                                 std::vector<NodePointer> body, Location source)
              : NodeBase(NodeKind::Procedure, source), _locals(std::move(locals)), _body(std::move(body)), _scope(scope)
          {
          }

          [[nodiscard]] const std::string& Name(void) const noexcept
          {
               return this->_scope->Name().value();
          }
          [[nodiscard]] const std::vector<std::string>& NamespacePath(void) const noexcept
          {
               return this->_scope->namespacePath;
          }
          [[nodiscard]] const std::vector<FunctionScope::Parameter>& ParameterList(void) const noexcept
          {
               return this->_scope->parameters;
          }
          [[nodiscard]] const std::vector<FunctionScope::LocalEntity>& Locals(void) const noexcept
          {
               runtime_assert(this->_locals != nullptr, "ProcedureNode must have a non-nullptr _locals");
               return *this->_locals;
          }
          [[nodiscard]] const std::vector<NodePointer>& Body(void) const noexcept
          {
               return this->_body;
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built(indent * ast::PrettyIndent, ' ');
               std::string builtName{};
               for (const auto& path : this->_scope->namespacePath) builtName += std::format("{}::", path);
               builtName += Name();

               built += this->_scope->returnType->RawName() + " " + ::ToString(this->_scope->callingConvention) + " " +
                        builtName + "(";
               built += ")\n" + std::string(indent * ast::PrettyIndent, ' ') + "{\n";
               for (const auto& line : this->_body) built += line->ToString(indent + 1) + "\n";
               return built + std::string(indent * ast::PrettyIndent, ' ') + "}";
          }
          [[nodiscard]] abi::CallingConventionName CallingConvention(void) const noexcept
          {
               return this->_scope->callingConvention;
          }
          [[nodiscard]] abi::Linkage Linkage(void) const noexcept
          {
               return this->_scope->linkage;
          }
          [[nodiscard]] typeSystem::NonowningTypePointer ReturnType(void) const noexcept
          {
               return this->_scope->returnType;
          }
          [[nodiscard]] FunctionScope* Scope(void) const noexcept
          {
               return this->_scope;
          }

     private:
          std::shared_ptr<std::vector<FunctionScope::LocalEntity>> _locals;
          std::vector<NodePointer> _body;

          FunctionScope* _scope;
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
               return std::string(indent * ast::PrettyIndent, ' ') + this->_function->Name().value_or("__unknown") +
                      "(" + args + ")";
          }

          [[nodiscard]] const std::shared_ptr<FunctionScope>& Function(void) const noexcept
          {
               return this->_function;
          }
          [[nodiscard]] const std::vector<Expression>& Arguments(void) const noexcept
          {
               return this->_arguments;
          }
          [[nodiscard]] std::vector<Expression>& MoveArguments(void) noexcept
          {
               return this->_arguments;
          }

     private:
          std::shared_ptr<FunctionScope> _function;
          std::vector<Expression> _arguments{};
     };
} // namespace ecpps::ir
