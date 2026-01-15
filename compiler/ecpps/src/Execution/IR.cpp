#include "IR.h"
#include <Assert.h>
#include <TypeSystem/TypeBase.h>
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
#include "Expressions.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
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
     ir._context.globalScope->types.insert(typeSystem::g_unsignedShort);
     ir._context.globalScope->types.insert(typeSystem::g_unsignedInt);
     ir._context.globalScope->types.insert(typeSystem::g_unsignedLong);
     ir._context.globalScope->types.insert(typeSystem::g_unsignedLongLong);
     ir._context.contextSequence.Push(std::make_unique<NamespaceContext>(ir._context.globalScope.get()));
     for (const auto& node : ast) ir.ParseNode(node);
     auto built = std::move(ir._built);
     ir._context.contextSequence.Pop();
     return built;
}

void ecpps::ir::IR::ParseNode(const ast::NodePointer& node)
{
     if (auto* const functionDefinitionNode = dynamic_cast<ast::FunctionDefinitionNode*>(node.get());
         functionDefinitionNode != nullptr)
     {
          ParseFunctionDefinition(*functionDefinitionNode);
          return;
     }
     if (auto* const functionDeclarationNode = dynamic_cast<ast::FunctionDeclarationNode*>(node.get());
         functionDeclarationNode != nullptr)
     {
          ParseFunctionDeclaration(*functionDeclarationNode);
          return;
     }

     if (auto* const returnNode = dynamic_cast<ast::ReturnNode*>(node.get()); returnNode != nullptr)
     {
          ParseReturn(*returnNode);
          return;
     }
     if (auto* const variableDeclarationNode = dynamic_cast<ast::VariableDeclarationNode*>(node.get());
         variableDeclarationNode != nullptr)
     {
          ParseVariableDeclaration(*variableDeclarationNode);
          return;
     }

     auto expression = ParseExpression(node);
     if (expression == nullptr) return;

     this->_built.push_back(std::move(*expression.release()).Value()); // TODO: warn on nodiscard
}

void ecpps::ir::IR::ParseFunctionDeclaration(const ast::FunctionDeclarationNode& node)
{
     std::vector<FunctionScope::Parameter> parameters{};
     const auto& astParams = node.Signature().parameters.parameters;
     parameters.reserve(astParams.size());
     for (const auto& param : astParams)
     {
          parameters.emplace_back(param.name ? param.name->ToString(0) : "", ParseType(param.type), false);
     }

     const auto returnType = this->ParseType(node.Signature().type);
     abi::Linkage linkage = abi::Linkage::External;
     if (node.Signature().externOptional.has_value())
     {
          const std::string& languageLinkage =
              node.Signature().externOptional.value(); // NOLINT(bugprone-unchecked-optional-access)
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                       "Invalid language linkage specification", node.Source()));
          }
     }
     else if (node.Signature().isInline ||
              node.Signature().constexprSpecifier != ast::ConstantExpressionSpecifier::None)
          linkage = ecpps::abi::Linkage::Internal;
     // TODO: Error on conflicting linkage specification

     bool isDllImportExport{};

     for (const auto& attribute : node.Signature().attributes)
     {
          if (attribute->Name() == "dllexport" || attribute->Name() == "dllimport") isDllImportExport = true;
     }

     auto functionScope =
         MakeFunctionScope()
             .Name(node.Signature().name->ToString(0))
             .ReturnType(returnType)
             .CallingConvention(node.Signature().callingConvention)
             .Linkage(linkage)
             .IsDllImportExport(isDllImportExport)
             .IsExtern(node.Signature().isExtern)
             .IsFriend(node.Signature().isFriend)
             .IsInline(node.Signature().isInline)
             .IsStatic(false) // TODO: node.Signature().isStatic
             .ConstexprSpecifier(node.Signature().constexprSpecifier == ast::ConstantExpressionSpecifier::Constexpr
                                     ? ecpps::ir::ConstexprType::Constexpr
                                     : ecpps::ir::ConstexprType::None)
             .Parameters(parameters)
             .Build();
     functionScope->parameters = parameters;
     this->_context.contextSequence.Back()->GetScope().functions.push_back(std::move(functionScope));
}
void ecpps::ir::IR::ParseFunctionDefinition(const ast::FunctionDefinitionNode& node)
{
     std::vector<FunctionScope::Parameter> parameters{};
     parameters.reserve(node.Signature().parameters.parameters.size());
     for (const auto& param : node.Signature().parameters.parameters)
     {
          parameters.emplace_back(param.name ? param.name->ToString(0) : "", ParseType(param.type), false);
     }

     IR ir{this->_context.diagnostics.get()};
     const auto returnType = this->ParseType(node.Signature().type);
     abi::Linkage linkage = abi::Linkage::External;
     if (node.Signature().externOptional.has_value())
     {
          const std::string& languageLinkage =
              node.Signature().externOptional.value(); // NOLINT(bugprone-unchecked-optional-access)
          if (languageLinkage == "C") linkage = abi::Linkage::CLinkage;
          else
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                       "Invalid language linkage specification", node.Source()));
          }
     }
     else if (node.Signature().isInline ||
              node.Signature().constexprSpecifier != ast::ConstantExpressionSpecifier::None)
          linkage = abi::Linkage::Internal;
     // TODO: Error on conflicting linkage specification

     bool isDllImportExport{};

     for (const auto& attribute : node.Signature().attributes)
     {
          if (attribute->Name() == "dllexport" || attribute->Name() == "dllimport") isDllImportExport = true;
     }

     auto functionScope =
         MakeFunctionScope()
             .Name(node.Signature().name->ToString(0))
             .ReturnType(returnType)
             .CallingConvention(node.Signature().callingConvention)
             .Linkage(linkage)
             .IsDllImportExport(isDllImportExport)
             .IsExtern(node.Signature().isExtern)
             .IsFriend(node.Signature().isFriend)
             .IsInline(node.Signature().isInline)
             .IsStatic(false) // TODO: node.Signature().isStatic
             .ConstexprSpecifier(node.Signature().constexprSpecifier == ast::ConstantExpressionSpecifier::Constexpr
                                     ? ecpps::ir::ConstexprType::Constexpr
                                     : ecpps::ir::ConstexprType::None)
             .Parameters(parameters)
             .Build();
     functionScope->parameters = parameters;
     functionScope->linkage = linkage;
     auto functionContext = std::make_shared<FunctionContext>(
         functionScope.get(), node.Signature().callingConvention, returnType, node.Signature().name->ToString(0),
         parameters |
             std::views::transform([](const FunctionScope::Parameter& parameter) -> decltype(auto)
                                   { return parameter.type; }) |
             std::ranges::to<std::vector>());

     ir._context = this->_context;
     auto* vFunctionScope = functionScope.get();
     ir._context.contextSequence.Push(std::move(functionContext));

     this->_context.contextSequence.Back()->GetScope().functions.push_back(std::move(functionScope));

     for (const auto& line : node.Body()) ir.ParseNode(line);
     std::vector<FunctionScope::Variable> locals{};
     locals.reserve(vFunctionScope->locals.size());
     for (const auto& toCopy : vFunctionScope->locals) locals.emplace_back(toCopy);

     if (returnType != nullptr && typeSystem::g_void->CommonWith(returnType))
          ir._built.push_back(std::make_unique<ir::ReturnNode>(nullptr, node.Source()));

     this->_built.push_back(std::make_unique<ecpps::ir::ProcedureNode>(
         linkage, node.Signature().callingConvention, returnType, node.Signature().name->ToString(0),
         std::move(parameters), std::move(locals), std::move(ir._built), node.Source()));
}

void ecpps::ir::IR::ParseReturn(const ast::ReturnNode& node)
{
     auto* const function = dynamic_cast<FunctionContext*>(this->_context.contextSequence.Back().get());

     runtime_assert(function != nullptr, "Function was null when parsing the function");

     if (node.Value() == nullptr)
     {
          if (!typeSystem::g_void->CommonWith(function->returnType))
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "Cannot convert from void to type " + function->returnType->Name() + " (aka " +
                           function->returnType->RawName() + ")",
                       node.Source()));
          }
          this->_built.push_back(std::make_unique<ir::ReturnNode>(nullptr, node.Source()));
     }

     auto returnExpression = ParseExpression(node.Value());

     auto converted = ConvertTo(std::move(returnExpression), function->returnType);
     this->_built.push_back(std::make_unique<ir::ReturnNode>(std::move(converted), node.Source()));
}

void ecpps::ir::IR::ParseVariableDeclaration(const ast::VariableDeclarationNode& node)
{
     auto* const functionContext = dynamic_cast<FunctionContext*>(this->_context.contextSequence.Back().get());
     runtime_assert(functionContext != nullptr, "Variable declaration outside of a function is not supported");

     auto& fscope = functionContext->GetScope<FunctionScope>(); // NOLINT(clang-analyzer-core.CallAndMessage)

     auto declaredType = ParseType(node.Type());
     if (declaredType == nullptr)
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  std::format("Unknown type {} in variable declaration", node.Type()->ToString(0)), node.Source()));
          return;
     }

     for (const auto& decl : node.Declarators())
     {
          auto* const idNodePtr = decl.name.get();
          const auto* const idExpr = dynamic_cast<const ast::IdentifierNode*>(idNodePtr);
          if (idExpr == nullptr)
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                       "Expected identifier in variable declarator", decl.name->Source()));
               continue;
          }

          const std::string varName = idExpr->Value();

          bool duplicate = false;
          for (const auto& v : fscope.locals)
          {
               if (v.name == varName)
               {
                    duplicate = true;
                    break;
               }
          }
          if (!duplicate)
          {
               for (const auto& p : fscope.parameters)
               {
                    if (p.name == varName)
                    {
                         duplicate = true;
                         break;
                    }
               }
          }
          if (duplicate)
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "Redefinition of variable '" + varName + "'", decl.name->Source()));
               continue;
          }

          FunctionScope::Variable varEntry;
          varEntry.name = varName;
          varEntry.type = declaredType;
          varEntry.isStatic = node.GetFlags().isStatic;
          varEntry.isExtern = node.GetFlags().isExtern;

          fscope.locals.emplace_back(std::move(varEntry));
          const auto& registeredVar = fscope.locals.back();

          if (decl.initialiser)
          {
               auto initExpr = ParseExpression(decl.initialiser);
               if (initExpr == nullptr)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                            "Invalid initialiser for variable '" + varName + "'", decl.initialiser->Source()));
                    continue;
               }

               auto converted = ConvertTo(std::move(initExpr), declaredType);
               if (converted == nullptr)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                            "Cannot convert initialiser to variable type for '" + varName + "'",
                            decl.initialiser->Source()));
                    continue;
               }

               this->_built.push_back(std::make_unique<ir::StoreNode>(registeredVar.name, std::move(converted),
                                                                      decl.initialiser->Source()));
          }
          else
          {
               // TODO: default-initialiser
               // for scalars it is an indeterminate Value, but TODO: class types
               // TODO: Error for references
          }
     }
}

Expression ecpps::ir::IR::ParseAdditiveExpression(Expression left, ast::Operator operator_, Expression right,
                                                  const Location& source) const
{
     runtime_assert(operator_ == ast::Operator::Plus || operator_ == ast::Operator::Minus, "Invalid additive operator");

     auto leftIntegral = std::dynamic_pointer_cast<typeSystem::IntegralType>(left->Type());
     auto rightIntegral = std::dynamic_pointer_cast<typeSystem::IntegralType>(right->Type());

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes

          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
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
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
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
                                                        const Location& source) const
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
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
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
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
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

Expression ecpps::ir::IR::ParseShiftExpression([[maybe_unused]] Expression left,
                                               [[maybe_unused]] ast::Operator operator_,
                                               [[maybe_unused]] Expression right,
                                               [[maybe_unused]] const Location& source)
{
     runtime_assert(operator_ == ast::Operator::LeftShift || operator_ == ast::Operator::RightShift,
                    "Invalid operator for a shift-expression");

     throw std::logic_error("Not implemented");

     return {};
}

// indirection
Expression ecpps::ir::IR::ParseDereferenceExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     auto pointerType = std::dynamic_pointer_cast<typeSystem::PointerType>(operand->Type());

     if (pointerType == nullptr)
     {
          // TODO: Classes

          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "Cannot perform this unary operation on " + operand->Type()->Name(), operand->Value()->Source()));

          return nullptr;
     }

     // TODO: lvalue-to-rvalue conversions
     // if (!operand->IsPRValue())
     // {

     //      this->_context.diagnostics.get().diagnosticsList.push_back(
     //          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
     //              "A prvalue is required for an indirection", operand->Value()->Source()));

     //      return nullptr;
     // }

     return std::make_unique<LValue>(pointerType->BaseType(),
                                     std::make_unique<DereferenceNode>(std::move(operand), source), false);
}

Expression ecpps::ir::IR::ParseAddressOfExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     if (!operand->IsLValue())
     {

          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "An lvalue is required for a the address-of operator", operand->Value()->Source()));

          return nullptr;
     }

     // TODO:
     // 1. pointer-to-member
     // 2. function pointer

     auto pointerType = std::make_unique<typeSystem::PointerType>(operand->Type(), operand->Type()->Name() + "*",
                                                                  typeSystem::Qualifiers::None);

     return std::make_unique<PRValue>(std::move(pointerType),
                                      std::make_unique<AddressOfNode>(std::move(operand), source), false);
}
Expression ecpps::ir::IR::ParsePreIncrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "A modifiable lvalue is required for a the builtin pre-increment operator",
                       operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<LValue>(
              operandType,
              std::make_unique<AdditionAssignNode>(
                  std::move(operand),
                  std::make_unique<PRValue>(operandType, std::make_unique<IntegralNode>(1, source), true), source),
              false);
     }

     throw TracedException("Not implemented");

     return nullptr;
}

Expression ecpps::ir::IR::ParsePostIncrementExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     const auto& operandType = operand->Type();
     if (IsScalar(operandType))
     {
          if (!operand->IsLValue())
          {
               this->_context.diagnostics.get().diagnosticsList.push_back(
                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                       "A modifiable lvalue is required for a the builtin post-increment operator",
                       operand->Value()->Source()));
               return nullptr;
          }

          return std::make_unique<PRValue>(operandType,
                                           std::make_unique<PostIncrementNode>(std::move(operand), 1, source), false);
     }

     throw TracedException("Not implemented");
}

Expression ecpps::ir::IR::ParseUnaryExpression(const ast::UnaryOperatorNode& node)
{
     const auto operator_ = node.Value();
     auto operand = ParseExpression(node.Operand());
     if (operand == nullptr) return nullptr;

     switch (operator_)
     {
     case ast::Operator::Plus:
     case ast::Operator::Minus: throw TracedException(std::logic_error("Not implemented"));
     case ast::Operator::Asterisk: return this->ParseDereferenceExpression(std::move(operand), node.Source());
     case ast::Operator::Ampersand: return this->ParseAddressOfExpression(std::move(operand), node.Source());
     case ast::Operator::Increment:
          return node.UnaryType() == ast::UnaryOperatorType::Prefix
                     ? this->ParsePreIncrementExpression(std::move(operand), node.Source())
                     : this->ParsePostIncrementExpression(std::move(operand), node.Source());
     default: throw TracedException(std::logic_error("Invalid unary operator"));
     }
}

Expression ecpps::ir::IR::ParseBinaryExpression(const ast::BinaryOperatorNode& node)
{
     const auto operator_ = node.Value();
     auto left = ParseExpression(node.Left());
     auto right = ParseExpression(node.Right());
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
          return ecpps::ir::IR::ParseShiftExpression(std::move(left), operator_, std::move(right), node.Source());
     default: throw TracedException(std::logic_error("Invalid binary operator"));
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
     if (auto* const identifierFunction = dynamic_cast<ast::IdentifierNode*>(node.Function().get()))
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

                    const auto match = ecpps::ir::IR::MatchFunction(candidate, arguments);
                    if (!match) continue;
                    candidates.emplace(match, candidate);
               }

               if (candidates.empty()) continue;
               // TODO: Ambiguity
               const auto& candidate = candidates.top().second;
               auto moveRange = std::ranges::subrange(std::make_move_iterator(arguments.begin()),
                                                      std::make_move_iterator(arguments.end()));

               std::vector<Expression> evaluatedArguments = std::views::zip(candidate->parameters, moveRange) |
                                                            std::views::transform(
                                                                [this](auto&& pair)
                                                                {
                                                                     auto&& [param, arg] = pair;
                                                                     return ConvertTo(std::move(arg), param.type);
                                                                }) |
                                                            std::ranges::to<std::vector>();

               auto call = std::make_unique<FunctionCallNode>(candidate, std::move(evaluatedArguments), node.Source());

               // TODO: Check for references; lvalue reference => lvalue; rvalue reference => xvalue
               return std::make_unique<PRValue>(candidate->returnType, std::move(call), false);
          }
          this->_context.diagnostics.get().diagnosticsList.push_back(
              ecpps::diagnostics::DiagnosticsBuilder<diagnostics::UnresolvedSymbolError>{}.Build(
                  name, "Unresolved function " + name, identifierFunction->Source()));
          return nullptr;
     }

     // TODO: More
     return nullptr;
}

Expression ecpps::ir::IR::ParseIdExpression(const ast::IdentifierNode& expression)
{
     // TODO: Proper name lookup please. Also CONTEXT MATTERS REALLY! Overload resolution! Hello?

     const std::string& name = expression.Value();

     for (const auto& context : this->_context.contextSequence)
     {
          const auto& scope = context->GetScope();
          if (const auto* const functionScope = dynamic_cast<const FunctionScope*>(&scope))
          {
               for (const auto& local : functionScope->locals)
               {
                    // TODO: Constexpr evaluation
                    if (local.name == name)
                    {
                         return std::make_unique<LValue>(
                             local.type, std::make_unique<LoadNode>(local.name, expression.Source()), false);
                    }
               }
          }
     }

     this->_context.diagnostics.get().diagnosticsList.push_back(
         ecpps::diagnostics::DiagnosticsBuilder<diagnostics::UnresolvedSymbolError>{}.Build(
             name, "Unresolved identifier " + name, expression.Source()));

     return nullptr;
}

Expression ecpps::ir::IR::ParseExpression(const ast::NodePointer& expression)
{
     if (expression == nullptr) return nullptr;

     if (auto* const integerLiteral = dynamic_cast<ast::IntegerLiteralNode*>(expression.get());
         integerLiteral != nullptr)
          return std::make_unique<PRValue>(
              typeSystem::g_int, std::make_unique<ir::IntegralNode>(integerLiteral->Value(), expression->Source()),
              true);

     if (auto* const characterLiteral = dynamic_cast<ast::CharacterLiteralNode*>(expression.get());
         characterLiteral != nullptr)
          return std::make_unique<PRValue>(
              typeSystem::g_char, std::make_unique<ir::IntegralNode>(characterLiteral->Value(), expression->Source()),
              true);
     if (auto* const binaryExpression = dynamic_cast<ast::BinaryOperatorNode*>(expression.get());
         binaryExpression != nullptr)
          return this->ParseBinaryExpression(*binaryExpression);
     if (auto* const unaryExpression = dynamic_cast<ast::UnaryOperatorNode*>(expression.get());
         unaryExpression != nullptr)
          return this->ParseUnaryExpression(*unaryExpression);
     if (auto* const functionCall = dynamic_cast<ast::CallOperatorNode*>(expression.get()); functionCall != nullptr)
          return this->ParseCallExpression(*functionCall);
     if (auto* const identifier = dynamic_cast<ast::IdentifierNode*>(expression.get()); identifier != nullptr)
          return this->ParseIdExpression(*identifier);

     this->_context.diagnostics.get().diagnosticsList.push_back(
         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
             expression->ToString(0) + " cannot appear in this context.", expression->Source()));

     return nullptr;
}

ecpps::typeSystem::TypePointer ecpps::ir::IR::ParseType(const ast::NodePointer& type)
{
     auto ResolveBasicType = [this](const std::string& name) -> typeSystem::TypePointer
     {
          for (const auto& scope : this->_context.contextSequence)
          {
               for (const auto& type : scope->GetScope().types)
               {
                    if (type->Name() == name) return type;
               }
          }

          return nullptr;
     };

     bool isConst = false;
     bool isVolatile = false;
     const ast::Node* base = type.get();
     // if (auto cvQualified = dynamic_cast<const ast::CVQualifiedType*>(base); cvQualified)
     //{
     //      isConst = cvQualified->IsConst();
     //      isVolatile = cvQualified->IsVolatile();
     //      base = cvQualified->UnqualifiedType().get();
     // }

     typeSystem::TypePointer result = nullptr;

     if (const auto* basicType = dynamic_cast<const ast::BasicType*>(base); basicType)
     {
          result = ResolveBasicType(basicType->Value());
     }
     else if (auto* const qualifiedType = dynamic_cast<ast::QualifiedType*>(type.get()); qualifiedType != nullptr)
     {
          Scope* currentScope = &this->_context.contextSequence.Back()->GetScope();
          for (const auto& section : qualifiedType->Sections())
          {
               if (auto* nsScope = dynamic_cast<ir::NamespaceScope*>(currentScope))
               {
                    bool found = false;
                    for (auto& sub : nsScope->subNamespaces)
                    {
                         if (sub->name == section.node->ToString(0))
                         {
                              currentScope = sub.get();
                              found = true;
                              break;
                         }
                    }
                    if (!found) return nullptr;
               }
               else if (auto* classScope = dynamic_cast<ir::ClassScope*>(currentScope))
               {
                    bool found = false;
                    for (auto& nested : classScope->nestedClasses)
                    {
                         if (nested->name == section.node->ToString(0))
                         {
                              currentScope = nested.get();
                              found = true;
                              break;
                         }
                    }
                    if (!found) return nullptr;
               }
               else
                    return nullptr;
          }

          if (auto* const unqualified = dynamic_cast<ast::BasicType*>(qualifiedType->UnqualifiedType().get());
              unqualified != nullptr)
          {
               for (const auto& type : currentScope->types)
               {
                    if (type->Name() == unqualified->Value()) result = type;
               }
          }
     }
     else if (const auto* ptr = dynamic_cast<const ast::PointerType*>(base); ptr)
     {
          auto inner = ParseType(ptr->BaseType());
          typeSystem::Qualifiers qualifiers{};
          // TOOD: Implement qualifiers
          result = std::make_shared<typeSystem::PointerType>(inner, base->ToString(0), qualifiers);
     }
     else if (const auto* ref = dynamic_cast<const ast::ReferenceType*>(base); ref)
     {
          auto inner = ParseType(ref->BaseType());
          result = std::make_shared<typeSystem::ReferenceType>(inner,
                                                               ref->GetKind() == ast::ReferenceType::Kind::RValue
                                                                   ? typeSystem::ReferenceType::Kind::RValue
                                                                   : typeSystem::ReferenceType::Kind::RValue,
                                                               base->ToString(0));
     }

     if (!result) return nullptr;

     if (isConst || isVolatile) {} // TODO: Implement

     return result;
}

Expression ecpps::ir::IR::ConvertTo(Expression expression, const typeSystem::TypePointer& toType) const
{
     if (expression == nullptr || toType == nullptr) return nullptr;

     const auto comparison = toType->CompareTo(expression->Type());

     if (!comparison.IsValid())
     {
          this->_context.diagnostics.get().diagnosticsList.push_back(
              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                  "Cannot convert from " + expression->Type()->Name() + " (aka " + expression->Type()->RawName() +
                      ") to type " + toType->Name() + " (aka " + toType->RawName() + ")",
                  expression->Value()->Source()));
          return nullptr;
     }

     if (comparison.Sequence().Size() == 0) return expression;

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

Expression ecpps::ir::IR::ConvertIntegral(Expression expression, const std::shared_ptr<typeSystem::IntegralType>& type)
{
     const auto& expressionType = expression->Type();
     const auto& source = expression->Value()->Source();

     if (auto* const integralNode = dynamic_cast<IntegralNode*>(expression->Value().get()); integralNode != nullptr)
     {
          // promotion
          return std::make_unique<PRValue>(type, std::move(std::move(*expression).Value()),
                                           expression->IsConstantExpression());
     }
     if (IsArithmetic(expressionType))
          return std::make_unique<PRValue>(type, std::make_unique<ConvertNode>(std::move(expression), type, source),
                                           false);
     return nullptr; // TODO: Return implicit conversion node
}

ecpps::ir::MatchingScore ecpps::ir::IR::MatchFunction(const std::shared_ptr<FunctionScope>& function,
                                                      const std::vector<Expression>& arguments)
{
     if (arguments.size() != function->parameters.size()) return MatchingScore::NotMatching; // TODO: default arguments

     MatchingScore totalScore = MatchingScore::MaxScore;

     auto parameterIterator = function->parameters.begin();
     for (const auto& argument : arguments)
     {
          const auto& parameter = *parameterIterator++;
          if (argument == nullptr) continue;
          const auto& fromType = argument->Type();
          const auto& toType = parameter.type;

          ecpps::typeSystem::ConversionSequence seq = fromType->CompareTo(toType);

          ImplicitConversion::RefBindingKind refKind = ImplicitConversion::RefBindingKind::None;
          // if (IsReference(toType)) // TODO: Implmenent references
          //{
          //      const auto& referenceType = dynamic_cast<typeSystem::ReferenceType&>(toType);
          //      if (referenceType.IsLValueReference())
          //      {
          //           if (argument->IsLValue()) refKind = ImplicitConversion::RefBindingKind::LValueRef;
          //           else
          //                refKind = ImplicitConversion::RefBindingKind::BindToTemporary;
          //      }
          //      else if (referenceType.IsRValueReference())
          //      {
          //           if (argument->IsXValue()) refKind = ImplicitConversion::RefBindingKind::RValueRef;
          //           else
          //                refKind = ImplicitConversion::RefBindingKind::ConstRValueRef;
          //      }
          // }

          const bool valid = seq.IsValid() && refKind != ImplicitConversion::RefBindingKind::IllFormed;
          if (!valid) return MatchingScore::NotMatching;

          const ImplicitConversion conversion{std::move(seq), refKind, true};

          totalScore += conversion.Rank();
     }

     return totalScore;
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
