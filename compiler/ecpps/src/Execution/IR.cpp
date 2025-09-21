#include "IR.h"
#include <Assert.h>
#include <format>
#include <memory>
#include <queue>
#include <ranges>
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
     ir._context.globalScope->types.insert(typeSystem::g_void);
     ir._context.globalScope->types.insert(typeSystem::g_char);
     ir._context.globalScope->types.insert(typeSystem::g_signedChar);
     ir._context.globalScope->types.insert(typeSystem::g_unsignedChar);
     ir._context.globalScope->types.insert(typeSystem::g_short);
     ir._context.globalScope->types.insert(typeSystem::g_int);
     ir._context.globalScope->types.insert(typeSystem::g_long);
     ir._context.globalScope->types.insert(typeSystem::g_longLong);
     ir._context.contextSequence.Push(std::make_unique<NamespaceContext>(ir._context.globalScope.get()));
     for (const auto& node : ast) ir.ParseNode(node);
     auto built = std::move(ir._built);
     ir._context.contextSequence.Pop();
     return built;
}

void ecpps::ir::IR::ParseNode(const ast::NodePointer& node)
{
     if (const auto functionDefinitionNode = dynamic_cast<ast::FunctionDefinitionNode*>(node.get());
         functionDefinitionNode != nullptr)
          return ParseFunctionDefinition(*functionDefinitionNode);
     if (const auto functionDeclarationNode = dynamic_cast<ast::FunctionDeclarationNode*>(node.get());
         functionDeclarationNode != nullptr)
          return ParseFunctionDeclaration(*functionDeclarationNode);

     if (const auto returnNode = dynamic_cast<ast::ReturnNode*>(node.get()); returnNode != nullptr)
          return ParseReturn(*returnNode);

     auto expression = ParseExpression(node);
     if (expression == nullptr) return;

     this->_built.push_back(std::move(*expression.release()).Value()); // TODO: warn on nodiscard
}

void ecpps::ir::IR::ParseFunctionDeclaration(const ast::FunctionDeclarationNode& node)
{
     std::vector<FunctionScope::Parameter> parameters{};

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

     auto functionScope = MakeFunctionScope().Name(node.Signature().name->ToString(0)).ReturnType(returnType).Build();
     functionScope->parameters = parameters;
     this->_context.contextSequence.Back()->GetScope().functions.push_back(std::move(functionScope));
}
void ecpps::ir::IR::ParseFunctionDefinition(const ast::FunctionDefinitionNode& node)
{
     std::vector<FunctionScope::Parameter> parameters{};
     for (const auto& param : node.Signature().parameters.parameters)
     {
          parameters.emplace_back(param.name->ToString(0), ParseType(param.type), false);
     }

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

     auto functionScope = MakeFunctionScope().Name(node.Signature().name->ToString(0)).ReturnType(returnType).Build();
     functionScope->parameters = parameters;
     functionScope->linkage = linkage;
     auto functionContext = std::make_shared<FunctionContext>(
         functionScope.get(), node.Signature().callingConvention, returnType, node.Signature().name->ToString(0),
         parameters |
             std::views::transform([](const FunctionScope::Parameter& parameter) -> decltype(auto)
                                   { return parameter.type; }) |
             std::ranges::to<std::vector>());

     for (const auto& attribute : node.Signature().attributes)
     {
          if (attribute->Name() == "dllexport")
          {
               functionScope->isDllImportExport = true;
               break;
          }
     }

     ir._context = this->_context;
     ir._context.contextSequence.Push(std::move(functionContext));

     this->_context.contextSequence.Back()->GetScope().functions.push_back(std::move(functionScope));

     for (const auto& line : node.Body()) ir.ParseNode(line);
     if (typeSystem::g_void->CommonWith(returnType))
          ir._built.push_back(std::make_unique<ir::ReturnNode>(nullptr, node.Source()));

     this->_built.push_back(std::make_unique<ecpps::ir::ProcedureNode>(
         linkage, node.Signature().callingConvention, returnType, node.Signature().name->ToString(0),
         std::move(parameters), std::move(ir._built), node.Source()));
}

void ecpps::ir::IR::ParseReturn(const ast::ReturnNode& node)
{
     const auto function = dynamic_cast<FunctionContext*>(this->_context.contextSequence.Back().get());

     if (node.Value() == nullptr)
     {
          if (!typeSystem::g_void->CommonWith(function->returnType))
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.build(
                       "Cannot convert from void to type " + function->returnType->Name() + " (aka " +
                           function->returnType->RawName() + ")",
                       node.Source()));
          }
          this->_built.push_back(std::make_unique<ir::ReturnNode>(nullptr, node.Source()));
     }

     auto returnExpression = ParseExpression(node.Value());

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

struct CompareByPriority
{
     bool operator()(const std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>& a,
                     const std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>& b) const
     {
          return a.first < b.first;
     }
};

Expression ecpps::ir::IR::ParseCallExpression(const ast::CallOperatorNode& node)
{
     // fast/hot path for identifiers
     if (const auto identifierFunction = dynamic_cast<ast::IdentifierNode*>(node.Function().get()))
     {
          const std::string& name = identifierFunction->Value();
          std::priority_queue<
              std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>,
              std::vector<std::pair<ecpps::ir::MatchingScore, std::shared_ptr<ecpps::ir::FunctionScope>>>,
              CompareByPriority>
              candidates{};
          // TODO: Traverse contexts
          for (const auto& context : this->_context.contextSequence)
          {
               std::vector<Expression> arguments = node.Arguments() |
                                                   std::views::transform([this](const ast::NodePointer& argument)
                                                                         { return this->ParseExpression(argument); }) |
                                                   std::ranges::to<std::vector>();

               for (const auto& candidate : context->GetScope().functions)
               {
                    if (candidate->name != name) continue;

                    const auto match = this->MatchFunction(candidate, arguments);
                    if (!match) continue;
                    candidates.emplace(match, candidate);
               }

               if (candidates.empty()) continue;
               // TODO: Ambiguity
               const auto& candidate = candidates.top().second;

               auto call = std::make_unique<FunctionCallNode>(candidate, std::move(arguments), node.Source());

               // TODO: Check for references; lvalue reference => lvalue; rvalue reference => xvalue
               return std::make_unique<PRValue>(candidate->returnType, std::move(call), false);
          }
          this->_context.diagnostics.get().diagnosticsList.push_back(
              ecpps::diagnostics::DiagnosticsBuilder<diagnostics::UnresolvedSymbolError>{}.build(
                  name, "Unresolved function " + name, identifierFunction->Source()));
          return nullptr;
     }

     // TODO: More
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
     if (const auto functionCall = dynamic_cast<ast::CallOperatorNode*>(expression.get()); functionCall != nullptr)
          return this->ParseCallExpression(*functionCall);

     // TODO: Error
     return nullptr;
}

ecpps::typeSystem::TypePointer ecpps::ir::IR::ParseType(const ast::NodePointer& type)
{
     if (const auto basicType = dynamic_cast<ast::BasicType*>(type.get()); basicType != nullptr)
     {
          if (this->_context.contextSequence.Back()->GetScope().types.contains(basicType->Value()))
               return *this->_context.contextSequence.Back()->GetScope().types.find(basicType->Value());
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

ecpps::ir::MatchingScore ecpps::ir::IR::MatchFunction(const std::shared_ptr<FunctionScope>& function,
                                                      const std::vector<Expression>& arguments)
{
     if (arguments.size() != function->parameters.size()) return MatchingScore::NotMatching; // TODO: default arguments

     MatchingScore score = MatchingScore::MaxScore;

     auto parameterIterator = function->parameters.begin();
     for (const auto& argument : arguments) { const auto& parameter = *parameterIterator++; }

     return score;
}

ecpps::ir::MatchingScore ecpps::ir::ImplicitConversion::Rank(void) const noexcept
{
     if (!this->isValid || !this->typeSequence.IsValid()) return MatchingScore::MaxScore; // invalid

     MatchingScore cost = MatchingScore::NotMatching;

     for (const auto kind : this->typeSequence.Sequence()) cost += std::to_underlying(kind);

     switch (this->refKind)
     {
     case RefBindingKind::None: break;
     case RefBindingKind::LValueRef: cost += 2; break;
     case RefBindingKind::ConstLValueRef: cost += 3; break;
     case RefBindingKind::RValueRef: cost += 4; break;
     case RefBindingKind::ConstRValueRef: cost += 5; break;
     case RefBindingKind::BindToTemporary: cost += 6; break;
     case RefBindingKind::IllFormed: return MatchingScore::MaxScore; // invalid
     }

     return cost;
}

ecpps::ir::ImplicitConversion ecpps::ir::MatchImplicitConversion(const Expression& expression,
                                                                 const typeSystem::TypePointer& type)
{
     ImplicitConversion::RefBindingKind referenceKind{};
     // if not a reference
     referenceKind = expression->IsPRValue() ? ImplicitConversion::RefBindingKind::BindToTemporary
                                             : ImplicitConversion::RefBindingKind::None;
     // TODO: references (the sole reason this function even exists)

     return ImplicitConversion{type->CompareTo(expression->Type()), referenceKind, true};
}
