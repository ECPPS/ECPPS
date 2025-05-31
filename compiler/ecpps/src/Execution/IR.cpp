#include "IR.h"
#include <vector>
#include <utility>
#include "ControlFlow.h"
#include "Procedural.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

std::vector<IRNodePointer> ecpps::ir::IR::Parse(const std::vector<ASTNodePointer>& ast)
{
     IR ir{};
     for (const auto& node : ast) ir.ParseNode(node);
     auto built = std::move(ir._built);
     return built;
}

void ecpps::ir::IR::ParseNode(const ast::NodePointer& node)
{
     if (const auto functionDefinitionNode = dynamic_cast<ast::FunctionDefinitionNode*>(node.get());
         functionDefinitionNode != nullptr)
          return ParseFunctionDefinition(*functionDefinitionNode);

     if (const auto returnNode = dynamic_cast<ast::ReturnNode*>(node.get()); returnNode != nullptr)
          return ParseReturn(*returnNode);

     auto expression = ParseExpression(node);
     this->_built.push_back(std::move(*expression.release()).Value()); // TODO: warn on nodiscard
}

void ecpps::ir::IR::ParseFunctionDefinition(const ast::FunctionDefinitionNode& node)
{
     std::vector<Parameter> parameters{};

     IR ir{};
     for (const auto& line : node.Body()) ir.ParseNode(line);

     this->_built.push_back(std::make_unique<ecpps::ir::ProcedureNode>(node.Signature().name->ToString(0),
                                                                std::move(parameters), std::move(ir._built)));
}

void ecpps::ir::IR::ParseReturn(const ast::ReturnNode& node)
{
     this->_built.push_back(std::make_unique<ir::ReturnNode>(ParseExpression(node.Value())));
}

Expression ecpps::ir::IR::ParseExpression(const ast::NodePointer& expression)
{
     if (expression == nullptr) return nullptr;

     if (const auto integerLiteral = dynamic_cast<ast::IntegerLiteralNode*>(expression.get());
         integerLiteral != nullptr)
          return std::make_unique<PRValue>(nullptr, std::make_unique<ir::IntegralNode>(integerLiteral->Value()),
                                           true); // TODO: Type system...

     // TODO: Error
     return nullptr;
}
