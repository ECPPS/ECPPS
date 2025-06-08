#include "IR.h"
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "../Machine/ABI.h"
#include "../Parsing/ASTs/Type.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "ControlFlow.h"
#include "Procedural.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

std::vector<IRNodePointer> ecpps::ir::IR::Parse(const std::vector<ASTNodePointer>& ast)
{
     IR ir{};
     ir._context.types.insert(typeSystem::g_char);
     ir._context.types.insert(typeSystem::g_signedChar);
     ir._context.types.insert(typeSystem::g_unsignedChar);
     ir._context.types.insert(typeSystem::g_short);
     ir._context.types.insert(typeSystem::g_int);
     ir._context.types.insert(typeSystem::g_long);
     ir._context.types.insert(typeSystem::g_longLong);
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
     const auto returnType = this->ParseType(node.Signature().type);
     abi::Linkage linkage = abi::Linkage::External;
     if (node.Signature().externOptional.has_value())
     {
          const std::string& languageLinkage = node.Signature().externOptional.value();
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               // TODO: Error
          }
     }
     else if (node.Signature().isInline ||
              node.Signature().constexprSpecifier != ast::ConstantExpressionSpecifier::None)
          linkage = abi::Linkage::Internal;
     // TODO: Error on conflicting linkage specification

     ir._context = this->_context;
     ir._context.contextSequence.Push(std::make_unique<FunctionContext>(returnType));

     for (const auto& line : node.Body()) ir.ParseNode(line);

     this->_built.push_back(std::make_unique<ecpps::ir::ProcedureNode>(linkage, node.Signature().callingConvention,
                                                                       returnType, node.Signature().name->ToString(0),
                                                                       std::move(parameters), std::move(ir._built)));
}

void ecpps::ir::IR::ParseReturn(const ast::ReturnNode& node)
{
     auto returnExpression = ParseExpression(node.Value());

     const auto function = dynamic_cast<FunctionContext*>(this->_context.contextSequence.Back().get());
     // TODO: Assert function != nullptr
     this->_built.push_back(
         std::make_unique<ir::ReturnNode>(ConvertTo(std::move(returnExpression), function->returnType)));
}

Expression ecpps::ir::IR::ParseExpression(const ast::NodePointer& expression)
{
     if (expression == nullptr) return nullptr;

     if (const auto integerLiteral = dynamic_cast<ast::IntegerLiteralNode*>(expression.get());
         integerLiteral != nullptr)
          return std::make_unique<PRValue>(typeSystem::g_int,
                                           std::make_unique<ir::IntegralNode>(integerLiteral->Value()), true);

     // TODO: Error
     return nullptr;
}

ecpps::typeSystem::TypePointer ecpps::ir::IR::ParseType(const ast::NodePointer& type)
{
     if (const auto basicType = dynamic_cast<ast::BasicType*>(type.get()); basicType != nullptr)
     {
          if (this->_context.types.contains(basicType->Value())) return *this->_context.types.find(basicType->Value());
     }

     return nullptr;
}

Expression ecpps::ir::IR::ConvertTo(Expression&& expression, const typeSystem::TypePointer& toType)
{
     const auto comparison = toType->CompareTo(expression->Type());

     if (!comparison.IsValid())
     {
          // TODO: Error
          return nullptr;
     }

     if (comparison.Sequence().Size() == 0) return std::move(expression);

     if (comparison.Sequence().Size() == 1)
     {
          const auto& conversion = *comparison.Sequence().begin();
          if (conversion == typeSystem::ConversionSequence::ConversionKind::IntegralConversion)
          {
               const auto intType = std::dynamic_pointer_cast<typeSystem::IntegralType>(toType);
               // TODO: Assert intType != nullptr
               return ConvertIntegral(std::move(expression), intType);
          }
     }

     throw std::logic_error("Unsupported conversion");
     return nullptr;
}

Expression ecpps::ir::IR::ConvertIntegral(Expression&& expression,
                                          const std::shared_ptr<typeSystem::IntegralType>& type)
{
     if (const auto integralNode = dynamic_cast<IntegralNode*>(expression->Value().get()); integralNode != nullptr)
     {
          return std::make_unique<PRValue>(type, std::move(*expression).Value(), expression->IsConstantExpression());
     }
     return nullptr; // TODO: Return implicit conversion node
}
