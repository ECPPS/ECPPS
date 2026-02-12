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
#include "../TypeSystem/CompoundTypes.h"
#include "ControlFlow.h"
#include "Expressions.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
#include "Procedural.h"
#include "Shared/Diagnostics.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

std::vector<IRNodePointer> ecpps::ir::IR::Parse(Diagnostics& diagnostics, BumpAllocator& allocator,
                                                const std::vector<ASTNodePointer>& ast)
{
     IR ir{diagnostics, allocator};
     ir._context.globalScope->types.insert(typeSystem::g_void.get());
     ir._context.globalScope->types.insert(typeSystem::g_char.get());
     ir._context.globalScope->types.insert(typeSystem::g_signedChar.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedChar.get());
     ir._context.globalScope->types.insert(typeSystem::g_short.get());
     ir._context.globalScope->types.insert(typeSystem::g_int.get());
     ir._context.globalScope->types.insert(typeSystem::g_long.get());
     ir._context.globalScope->types.insert(typeSystem::g_longLong.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedShort.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedInt.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedLong.get());
     ir._context.globalScope->types.insert(typeSystem::g_unsignedLongLong.get());
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

     this->_built.push_back(std::move(*expression).Value()); // TODO: warn on nodiscard
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
     if (parameters.size() == 1 && *parameters.at(0).type == typeSystem::g_void.get()) parameters.clear();

     IR ir{this->_context.diagnostics.get(), *this->_context.nodeAllocator};
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

     const auto functionName = node.Signature().name->ToString(0);

     if (returnType != nullptr && typeSystem::g_void->CommonWith(returnType))
          ir._built.push_back(std::unique_ptr<ir::ReturnNode, IRDeleter>{new (*ir._context.nodeAllocator)
                                                                             ir::ReturnNode(nullptr, node.Source())});

     if (functionName == "main" && (!ir._built.empty() && ir._built.back()->Kind() != NodeKind::Return))
          ir._built.push_back(
              std::unique_ptr<ir::ReturnNode, IRDeleter>{new (*ir._context.nodeAllocator) ir::ReturnNode(
                  std::make_unique<PRValue>(typeSystem::g_int.get(),
                                            std::unique_ptr<IntegralNode, IRDeleter>{
                                                new (*ir._context.nodeAllocator) ir::IntegralNode(0, node.Source())},
                                            true),
                  node.Source())});

     this->_built.push_back(std::unique_ptr<ecpps::ir::ProcedureNode, IRDeleter>{
         new (*this->_context.nodeAllocator)
             ecpps::ir::ProcedureNode(linkage, node.Signature().callingConvention, returnType, functionName,
                                      std::move(parameters), std::move(locals), std::move(ir._built), node.Source())});
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
          this->_built.push_back(std::unique_ptr<ir::ReturnNode, IRDeleter>{
              new (*this->_context.nodeAllocator) ir::ReturnNode(nullptr, node.Source())});
     }

     auto returnExpression = ParseExpression(node.Value());

     auto converted = ConvertTo(std::move(returnExpression), function->returnType);
     this->_built.push_back(std::unique_ptr<ir::ReturnNode, IRDeleter>{
         new (*this->_context.nodeAllocator) ir::ReturnNode(std::move(converted), node.Source())});
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
          auto variableType = declaredType;
          bool inferLastArrayFromInitialiser = false;
          const auto isArray = !decl.arrayLevels.empty();

          for (const auto& arrayLevel : decl.arrayLevels)
          {
               if (inferLastArrayFromInitialiser)
                    this->_context.diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::TypeError>(
                        std::format("Declaration of '{}' introduces an array of unbounded arrays which is not allowed, "
                                    "only the top-level array might be unbounded",
                                    varName),
                        arrayLevel == nullptr ? node.Source() : arrayLevel->Source()));

               if (arrayLevel == nullptr)
               {
                    inferLastArrayFromInitialiser = true;

                    continue;
               }
               throw TracedException(std::logic_error("Constant expression evaluator not implemented yet"));

               // variableType = std::make_shared<typeSystem::ArrayType>()
          }

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
          varEntry.type = variableType;
          varEntry.isStatic = node.GetFlags().isStatic;
          varEntry.isExtern = node.GetFlags().isExtern;

          auto& registeredVar = fscope.locals.emplace_back(std::move(varEntry));

          if (inferLastArrayFromInitialiser)
          {
               if (decl.initialiser == nullptr)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                            std::format("Unbounded array '{}' must have an initialiser", varName),
                            decl.name->Source()));
               }
               if (IsEligibleForStringLiteralInitialisation(variableType))
               {
                    if (auto* const stringInitialiser =
                            dynamic_cast<ecpps::ast::StringLiteralNode*>(decl.initialiser.get());
                        stringInitialiser != nullptr)
                    {
                         const auto& string = stringInitialiser->Value();
                         const auto arrayLength = string.length() + 1;
                         const auto* elementType = variableType->CastTo<ecpps::typeSystem::CharacterType>();
                         runtime_assert(elementType != nullptr,
                                        "Expected a character type for string-literal initialisation");
                         variableType = GetContext().Get(GetContext().PushType(
                             std::make_unique<ecpps::typeSystem::ArrayType>(arrayLength, elementType)));
                         inferLastArrayFromInitialiser = false;

                         std::vector<std::uint32_t> arrayValues{};
                         arrayValues.reserve(arrayLength);
                         for (const auto character : string) arrayValues.emplace_back(character);
                         arrayValues.emplace_back(0);
                         std::unique_ptr<ecpps::ir::IntegerArrayNode, IRDeleter> arrayNode{
                             new (*this->_context.nodeAllocator) ecpps::ir::IntegerArrayNode(
                                 std::move(arrayValues), std::move(elementType), decl.initialiser->Source())};
                         auto initialiserExpression =
                             std::make_unique<ecpps::PRValue>(variableType, std::move(arrayNode), true);

                         this->_built.push_back(std::unique_ptr<ir::StoreNode, IRDeleter>{
                             new (*this->_context.nodeAllocator) ir::StoreNode(
                                 registeredVar.name, std::move(initialiserExpression), decl.initialiser->Source())});
                    }
               }
               if (inferLastArrayFromInitialiser)
               {
                    this->_context.diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                            std::format("Array '{}' must be initialised with initialiser-lists", varName),
                            decl.name->Source()));
               }

               registeredVar.type = variableType;
          }
          else if (decl.initialiser)
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

               this->_built.push_back(std::unique_ptr<ir::StoreNode, IRDeleter>{
                   new (*this->_context.nodeAllocator)
                       ir::StoreNode(registeredVar.name, std::move(converted), decl.initialiser->Source())});
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

     auto leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     auto rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

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

     if (left->Type() != leftIntegral) // got promoted
     {
          const auto innerSource = left->Value()->Source();
          const auto wasConstexpr = left->IsConstantExpression();

          left = std::make_unique<PRValue>(
              leftIntegral,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(left), leftIntegral, innerSource)},
              wasConstexpr);
     }

     if (right->Type() != rightIntegral) // got promoted
     {
          const auto innerSource = right->Value()->Source();
          const auto wasConstexpr = right->IsConstantExpression();

          right = std::make_unique<PRValue>(
              rightIntegral,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(right), rightIntegral, innerSource)},
              wasConstexpr);
     }

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
              resultType,
              std::unique_ptr<AdditionNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                           AdditionNode(std::move(left), std::move(right), source)},
              false);

     return std::make_unique<PRValue>(
         resultType,
         std::unique_ptr<SubtractionNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                         SubtractionNode(std::move(left), std::move(right), source)},
         false);
}

Expression ecpps::ir::IR::ParseMultiplicativeExpression(Expression left, ast::Operator operator_, Expression right,
                                                        const Location& source) const
{
     runtime_assert(operator_ == ast::Operator::Asterisk || operator_ == ast::Operator::Solidus ||
                        operator_ == ast::Operator::Percent,
                    "Operator was not multiplicative in a multiplicative-expression");

     auto leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     auto rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

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

     if (left->Type() != leftIntegral) // got promoted
     {
          const auto innerSource = left->Value()->Source();
          const auto wasConstexpr = left->IsConstantExpression();

          left = std::make_unique<PRValue>(
              leftIntegral,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(left), leftIntegral, innerSource)},
              wasConstexpr);
     }

     if (right->Type() != rightIntegral) // got promoted
     {
          const auto innerSource = right->Value()->Source();
          const auto wasConstexpr = right->IsConstantExpression();

          right = std::make_unique<PRValue>(
              rightIntegral,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(right), rightIntegral, innerSource)},
              wasConstexpr);
     }

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
          return std::make_unique<PRValue>(resultType,
                                           std::unique_ptr<MultiplicationNode, IRDeleter>{
                                               new (*this->_context.nodeAllocator)
                                                   MultiplicationNode(std::move(left), std::move(right), source)},
                                           false);

     if (operator_ == ast::Operator::Solidus)
          return std::make_unique<PRValue>(
              resultType,
              std::unique_ptr<DivideNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                         DivideNode(std::move(left), std::move(right), source)},
              false);

     return std::make_unique<PRValue>(
         resultType,
         std::unique_ptr<ModuloNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                    ModuloNode(std::move(left), std::move(right), source)},
         false);
}

Expression ecpps::ir::IR::ParseShiftExpression([[maybe_unused]] Expression left,
                                               [[maybe_unused]] ast::Operator operator_,
                                               [[maybe_unused]] Expression right,
                                               [[maybe_unused]] const Location& source)
{
     runtime_assert(operator_ == ast::Operator::LeftShift || operator_ == ast::Operator::RightShift,
                    "Invalid operator for a shift-expression");

     throw ecpps::TracedException(std::logic_error("Not implemented"));
}

// indirection
Expression ecpps::ir::IR::ParseDereferenceExpression(Expression operand, const Location& source) const
{
     runtime_assert(operand != nullptr, "Operand was null");

     if (IsArray(operand->Type()))
     {
          const auto arrayType = operand->Type()->CastTo<typeSystem::ArrayType>();

          runtime_assert(arrayType != nullptr, "Expected an array type");
          operand = ConvertTo(std::move(operand),
                              GetContext().Get(GetContext().PushType(std::make_unique<typeSystem::PointerType>(
                                  arrayType->ElementType(), std::format("{}*", arrayType->ElementType()->Name()),
                                  typeSystem::Qualifiers::None))));
     }
     auto pointerType = operand->Type()->CastTo<typeSystem::PointerType>();

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
                                     std::unique_ptr<DereferenceNode, IRDeleter>{new (
                                         *this->_context.nodeAllocator) DereferenceNode(std::move(operand), source)},
                                     false);
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

     auto pointerType = GetContext().Get(GetContext().PushType(std::make_unique<typeSystem::PointerType>(
         operand->Type(), operand->Type()->Name() + "*", typeSystem::Qualifiers::None)));

     return std::make_unique<PRValue>(pointerType,
                                      std::unique_ptr<AddressOfNode, IRDeleter>{new (
                                          *this->_context.nodeAllocator) AddressOfNode(std::move(operand), source)},
                                      false);
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
              std::unique_ptr<AdditionAssignNode, IRDeleter>{new (*this->_context.nodeAllocator) AdditionAssignNode(
                  std::move(operand),
                  std::make_unique<PRValue>(operandType,
                                            std::unique_ptr<IntegralNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                                                         IntegralNode(1, source)},
                                            true),
                  source)},
              false);
     }

     throw TracedException("Not implemented");
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

          return std::make_unique<PRValue>(
              operandType,
              std::unique_ptr<PostIncrementNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                                PostIncrementNode(std::move(operand), 1, source)},
              false);
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

               auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{
                   new (*this->_context.nodeAllocator)
                       FunctionCallNode(candidate, std::move(evaluatedArguments), node.Source())};

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

Expression ecpps::ir::IR::ParseStringLiteral(const ast::StringLiteralNode& expression) const
{
     const auto length = expression.Value().length();
     const auto elementType = typeSystem::g_constChar.get();
     std::vector<std::uint32_t> values{};
     values.reserve(length + 1);
     for (const auto character : expression.Value()) values.emplace_back(character);
     values.emplace_back(0);
     auto arrayType =
         GetContext().Get(GetContext().PushType(std::make_unique<typeSystem::ArrayType>(length + 1, elementType)));
     auto node = std::unique_ptr<IntegerArrayNode, ecpps::BumpAllocator::Deleter>(
         new (*this->_context.nodeAllocator)
             IntegerArrayNode(std::move(values), elementType->CastTo<typeSystem::IntegralType>(), expression.Source()));
     return std::make_unique<PRValue>(arrayType, std::move(node), true);
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
                             local.type,
                             std::unique_ptr<LoadNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                                      LoadNode(local.name, expression.Source())},
                             false);
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
          return std::make_unique<PRValue>(typeSystem::g_int.get(),
                                           std::unique_ptr<ir::IntegralNode, IRDeleter>{
                                               new (*this->_context.nodeAllocator)
                                                   ir::IntegralNode(integerLiteral->Value(), expression->Source())},
                                           true);

     if (auto* const characterLiteral = dynamic_cast<ast::CharacterLiteralNode*>(expression.get());
         characterLiteral != nullptr)
          return std::make_unique<PRValue>(typeSystem::g_char.get(),
                                           std::unique_ptr<ir::IntegralNode, IRDeleter>{
                                               new (*this->_context.nodeAllocator)
                                                   ir::IntegralNode(characterLiteral->Value(), expression->Source())},
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
     if (auto* const stringLiteral = dynamic_cast<ast::StringLiteralNode*>(expression.get()); stringLiteral != nullptr)
          return this->ParseStringLiteral(*stringLiteral);

     this->_context.diagnostics.get().diagnosticsList.push_back(
         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
             expression->ToString(0) + " cannot appear in this context.", expression->Source()));

     return nullptr;
}

ecpps::typeSystem::NonowningTypePointer ecpps::ir::IR::ParseType(const ast::NodePointer& type)
{
     auto ResolveBasicType = [this](const std::string& name) -> typeSystem::NonowningTypePointer
     {
          if (name == "int" || name == "signed" || name == "signed int" || name == "int signed")
               return typeSystem::g_int.get();
          if (name == "unsigned" || name == "unsigned int" || name == "int unsigned")
               return typeSystem::g_unsignedInt.get();

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

     typeSystem::NonowningTypePointer result = nullptr;

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

          if (const auto* const unqualified = dynamic_cast<ast::BasicType*>(qualifiedType->UnqualifiedType().get());
              unqualified != nullptr)
          {
               for (const auto& currentType : currentScope->types)
               {
                    if (currentType->Name() == unqualified->Value()) result = currentType;
               }
          }
     }
     else if (const auto* ptr = dynamic_cast<const ast::PointerType*>(base); ptr)
     {
          auto inner = ParseType(ptr->BaseType());
          typeSystem::Qualifiers qualifiers{};
          // TOOD: Implement qualifiers
          result = GetContext().Get(
              GetContext().PushType(std::make_unique<typeSystem::PointerType>(inner, base->ToString(0), qualifiers)));
     }
     else if (const auto* ref = dynamic_cast<const ast::ReferenceType*>(base); ref)
     {
          auto inner = ParseType(ref->BaseType());
          result = GetContext().Get(GetContext().PushType(std::make_unique<typeSystem::ReferenceType>(
              inner,
              ref->GetKind() == ast::ReferenceType::Kind::RValue ? typeSystem::ReferenceType::Kind::RValue
                                                                 : typeSystem::ReferenceType::Kind::RValue,
              base->ToString(0))));
     }

     if (!result) return nullptr;

     if (isConst || isVolatile) {} // TODO: Implement

     return result; // NOLINT(clang-diagnostic-nrvo)
}

Expression ecpps::ir::IR::ConvertTo(Expression expression, typeSystem::NonowningTypePointer toType) const
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
               const auto intType = toType->CastTo<typeSystem::IntegralType>();
               runtime_assert(intType != nullptr,
                              std::format("Presumed integral type {} turned out not to be one", toType->RawName()));
               return ConvertIntegral(std::move(expression), intType);
          }
          if (conversion == typeSystem::ConversionSequence::ConversionKind::ArrayToPointer)
          {
               const auto pointerType = toType->CastTo<typeSystem::PointerType>();
               runtime_assert(pointerType != nullptr,
                              std::format("Presumed pointer type {} turned out not to be one", toType->RawName()));

               runtime_assert(expression->IsRValue() || expression->IsLValue(), "Expected an rvalue or an lvalue for "
                                                                                "the array-to-pointer conversion. See "
                                                                                "[conv.array]"); // [conv.array]

               const auto wasConstexpr = expression->IsConstantExpression();
               if (expression->IsPRValue())
               {
                    NodePointer decayNode{};

                    if (auto* const intArray = dynamic_cast<IntegerArrayNode*>(expression->Value().get()))
                    {
                         const auto source = intArray->Source();

                         decayNode = std::unique_ptr<TemporaryIntegerArrayDecayNode, IRDeleter>(
                             new (*this->_context.nodeAllocator)
                                 TemporaryIntegerArrayDecayNode(std::move(expression), source));
                    }
                    else
                         throw TracedException("array-to-pointer is not supported yet");

                    return std::make_unique<PRValue>(pointerType, std::move(decayNode), wasConstexpr);
               }

               if (expression->IsLValue())
               {
                    NodePointer decayNode{};

                    if (auto* const loadNode = dynamic_cast<LoadNode*>(expression->Value().get()); loadNode != nullptr)
                    {
                         const auto source = loadNode->Source();

                         decayNode = std::unique_ptr<LoadArrayDecayNode, IRDeleter>(
                             new (*this->_context.nodeAllocator) LoadArrayDecayNode(std::move(expression), source));
                    }
                    else
                         throw TracedException("array-to-pointer is not supported yet");

                    return std::make_unique<PRValue>(pointerType, std::move(decayNode), wasConstexpr);
               }

               throw TracedException(std::logic_error("Unsupported conversion"));
          }
     }

     throw TracedException(std::logic_error("Unsupported conversion"));
}

bool ecpps::ir::IR::IsEligibleForStringLiteralInitialisation(typeSystem::NonowningTypePointer type) const
{
     return IsCharacter(type);
}

Expression ecpps::ir::IR::ConvertIntegral(Expression expression, const typeSystem::IntegralType* type) const
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
          return std::make_unique<PRValue>(
              type,
              std::unique_ptr<ConvertNode, IRDeleter>{new (*this->_context.nodeAllocator)
                                                          ConvertNode(std::move(expression), type, source)},
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
          // if (IsReference(toType)) // TODO: Implement references
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
                                                                 typeSystem::NonowningTypePointer type)
{
     ImplicitConversion::RefBindingKind referenceKind{};
     // if not a reference
     referenceKind = expression->IsPRValue() ? ImplicitConversion::RefBindingKind::BindToTemporary
                                             : ImplicitConversion::RefBindingKind::None;
     // TODO: references (the sole reason this function even exists)

     return ImplicitConversion{type->CompareTo(expression->Type()), referenceKind, true};
}
