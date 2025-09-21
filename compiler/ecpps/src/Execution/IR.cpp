#include "IR.h"
#include <Assert.h>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "../Machine/ABI.h"
#include "../Parsing/ASTs/Type.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "ControlFlow.h"
#include "Operations.h"
#include "Procedural.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

std::vector<IRNodePointer> ecpps::ir::IR::Parse(Diagnostics& diagnostics, const std::vector<ASTNodePointer>& ast)
{
     IR ir{diagnostics};
     ir._context.types.insert(typeSystem::g_void);
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

     IR ir{this->_context.diagnostics.get()};
     const auto returnType = this->ParseType(node.Signature().type);
     abi::Linkage linkage = abi::Linkage::External;
     if (node.Signature().externOptional.has_value())
     {
          const std::string& languageLinkage = node.Signature().externOptional.value();
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.build(
                       "Invalid language linkage specification", node.Source()));
          }
     }
     else if (node.Signature().isInline ||
              node.Signature().constexprSpecifier != ast::ConstantExpressionSpecifier::None)
          linkage = abi::Linkage::Internal;
     // TODO: Error on conflicting linkage specification

     ir._context = this->_context;
     ir._context.contextSequence.Push(std::make_unique<FunctionContext>(returnType));

     for (const auto& line : node.Body()) ir.ParseNode(line);

     this->_built.push_back(std::make_unique<ecpps::ir::ProcedureNode>(
         linkage, node.Signature().callingConvention, returnType, node.Signature().name->ToString(0),
         std::move(parameters), std::move(ir._built), node.Source()));
}

void ecpps::ir::IR::ParseReturn(const ast::ReturnNode& node)
{
     auto returnExpression = ParseExpression(node.Value());

     const auto function = dynamic_cast<FunctionContext*>(this->_context.contextSequence.Back().get());
     runtime_assert(function != nullptr, "Function was null when parsing the function");
     this->_built.push_back(
         std::make_unique<ir::ReturnNode>(ConvertTo(std::move(returnExpression), function->returnType), node.Source()));
}

Expression ecpps::ir::IR::ParseAdditiveExpression(Expression left, ast::Operator operator_, Expression right,
                                                  const Location& source)
{
     runtime_assert(operator_ == ast::Operator::Plus || operator_ == ast::Operator::Minus, "Invalid additive operator");

     auto leftIntegral = std::dynamic_pointer_cast<typeSystem::IntegralType>(left->Type());
     auto rightIntegral = std::dynamic_pointer_cast<typeSystem::IntegralType>(right->Type());

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes

          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.build(
                  "Cannot perform this binary operation on " + left->Type()->Name() + " and " + left->Type()->Name(),
                  left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto resultType = leftIntegral->CommonWith(rightIntegral);
     if (resultType == nullptr)
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.build(
                  "Cannot find a common integral type between " + left->Type()->Name() + " and " + left->Type()->Name(),
                  left->Value()->Source()));
          return nullptr;
     }

     if (operator_ == ast::Operator::Plus)
          return std::make_unique<PRValue>(
              resultType, std::make_unique<AdditionNode>(std::move(left), std::move(right), source), false);

     return std::make_unique<PRValue>(
         resultType, std::make_unique<SubtractionNode>(std::move(left), std::move(right), source), false);
}

Expression ecpps::ir::IR::ParseMultiplicativeExpression(Expression left, ast::Operator operator_, Expression right,
                                                        const Location& source)
{
     runtime_assert(operator_ == ast::Operator::Asterisk || operator_ == ast::Operator::Solidus ||
                        operator_ == ast::Operator::Percent,
                    "Operator was not multiplicative in a multiplicative-expression");

     auto leftIntegral = std::dynamic_pointer_cast<typeSystem::IntegralType>(left->Type());
     auto rightIntegral = std::dynamic_pointer_cast<typeSystem::IntegralType>(right->Type());

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes

          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.build(
                  "Cannot perform this binary operation on " + left->Type()->Name() + " and " + left->Type()->Name(),
                  left->Value()->Source()));

          return nullptr;
     }
     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto resultType = leftIntegral->CommonWith(rightIntegral);
     if (resultType == nullptr)
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.build(
                  "Cannot find a common integral type between " + left->Type()->Name() + " and " + left->Type()->Name(),
                  left->Value()->Source()));
          return nullptr;
     }

     if (operator_ == ast::Operator::Asterisk)
          return std::make_unique<PRValue>(
              resultType, std::make_unique<MultiplicationNode>(std::move(left), std::move(right), source), false);

     if (operator_ == ast::Operator::Solidus)
          return std::make_unique<PRValue>(
              resultType, std::make_unique<DivideNode>(std::move(left), std::move(right), source), false);

     return std::make_unique<PRValue>(resultType,
                                      std::make_unique<ModuloNode>(std::move(left), std::move(right), source), false);
}

Expression ecpps::ir::IR::ParseShiftExpression(Expression left, ast::Operator operator_, Expression right,
                                               const Location& source)
{
     runtime_assert(operator_ == ast::Operator::LeftShift || operator_ == ast::Operator::RightShift,
                    "Invalid operator for a shift-expression");

     throw std::logic_error("Not implemented");

     return Expression();
}

Expression ecpps::ir::IR::ParseBinaryExpression(const ast::BinaryOperatorNode& node)
{
     const auto operator_ = node.value();
     auto left = ParseExpression(node.left());
     auto right = ParseExpression(node.right());
     if (left == nullptr || right == nullptr) return nullptr;

     switch (operator_)
     {
     case ast::Operator::Plus:
     case ast::Operator::Minus:
          return this->ParseAdditiveExpression(std::move(left), operator_, std::move(right), node.Source());
     case ast::Operator::Asterisk:
     case ast::Operator::Percent:
     case ast::Operator::Solidus:
          return this->ParseMultiplicativeExpression(std::move(left), operator_, std::move(right), node.Source());
     case ast::Operator::LeftShift:
     case ast::Operator::RightShift:
          return this->ParseShiftExpression(std::move(left), operator_, std::move(right), node.Source());
     }

     return nullptr;
}
Expression ecpps::ir::IR::ParseExpression(const ast::NodePointer& expression)
{
     if (expression == nullptr) return nullptr;

     if (const auto integerLiteral = dynamic_cast<ast::IntegerLiteralNode*>(expression.get());
         integerLiteral != nullptr)
          return std::make_unique<PRValue>(
              typeSystem::g_int, std::make_unique<ir::IntegralNode>(integerLiteral->Value(), expression->Source()),
              true);
     if (const auto binaryExpression = dynamic_cast<ast::BinaryOperatorNode*>(expression.get());
         binaryExpression != nullptr)
          return this->ParseBinaryExpression(*binaryExpression);

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
     if (expression == nullptr || toType == nullptr) return nullptr;

     const auto comparison = toType->CompareTo(expression->Type());

     if (!comparison.IsValid())
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.build(
                  "Cannot convert from " + expression->Type()->Name() + " (aka " + expression->Type()->RawName() +
                      ") to type " + toType->Name() + " (aka " + toType->RawName() + ")",
                  expression->Value()->Source()));
          return nullptr;
     }

     if (comparison.Sequence().Size() == 0) return std::move(expression);

     if (comparison.Sequence().Size() == 1)
     {
          const auto& conversion = *comparison.Sequence().begin();
          if (conversion == typeSystem::ConversionSequence::ConversionKind::IntegralConversion)
          {
               const auto intType = std::dynamic_pointer_cast<typeSystem::IntegralType>(toType);
               runtime_assert(intType != nullptr,
                              std::format("Presumed integral type {} turned out not to be one", toType->RawName()));
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
