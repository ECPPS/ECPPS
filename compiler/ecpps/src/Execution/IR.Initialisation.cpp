#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "../TypeSystem/ArithmeticTypes.h"
#include "../TypeSystem/CompoundTypes.h"
#include "Constexpr.h"
#include "Context.h"
#include "Execution/Entities.h"
#include "Expressions.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"
#include "TypeSystem/ClassType.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

std::optional<std::uint64_t> ecpps::ir::IR::InferArrayLength(const ast::NodePointer& initialiser,
                                                             typeSystem::NonowningTypePointer elementType,
                                                             const std::string& name, const Location& source)
{
     if (initialiser == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Unbounded array '{}' must have an initialiser", name), source));
          return std::nullopt;
     }

     if (const auto* const stringInitialiser = dynamic_cast<const ast::StringLiteralNode*>(initialiser.get());
         stringInitialiser != nullptr && IsEligibleForStringLiteralInitialisation(elementType))
     {
          return static_cast<std::uint64_t>(stringInitialiser->Value().length() + 1);
     }

     if (const auto* const listInitialiser = dynamic_cast<const ast::InitialiserListNode*>(initialiser.get());
         listInitialiser != nullptr)
     {
          std::uint64_t count{};
          for ([[maybe_unused]] const auto& element : listInitialiser->Initialisers()) ++count;

          if (count == 0)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Cannot deduce the bound of array '{}' from an empty initialiser list", name),
                         initialiser->Source()));
               return std::nullopt;
          }
          return count;
     }

     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               std::format("Array '{}' must be initialised with initialiser-lists", name), initialiser->Source()));
     return std::nullopt;
}

Expression ecpps::ir::IR::ParseArrayInitialiser(const ast::NodePointer& initialiser,
                                                typeSystem::NonowningTypePointer arrayTypeBase, const std::string& name)
{
     const auto* arrayType = arrayTypeBase->CastTo<typeSystem::ArrayType>();
     runtime_assert(arrayType != nullptr, "Expected an array type for array initialisers");

     if (const auto* const stringInitialiser = dynamic_cast<const ast::StringLiteralNode*>(initialiser.get());
         stringInitialiser != nullptr && IsEligibleForStringLiteralInitialisation(arrayType->ElementType()))
     {
          auto& allocator = *this->GetContext().nodeAllocator;
          const auto& string = stringInitialiser->Value();
          const auto stringLength = string.length() + 1;
          const auto* elementType = arrayType->ElementType()->CastTo<typeSystem::IntegralType>();
          runtime_assert(elementType != nullptr, "Expected a character type for string-literal initialisation");

          if (stringLength > arrayType->ElementCount())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Cannot initialise an array with more elements than it can "
                                     "hold. Provided {} elements for an array of `{}` `{}`s",
                                     stringLength, arrayType->ElementCount(), arrayType->ElementType()->RawName()),
                         initialiser->Source()));
               return nullptr;
          }

          std::vector<std::uint32_t> arrayValues{};
          arrayValues.reserve(arrayType->ElementCount());
          for (const auto character : string) arrayValues.emplace_back(character);
          arrayValues.emplace_back(0u);
          arrayValues.resize(arrayType->ElementCount(), 0u); // the rest is zero-initialised

          std::unique_ptr<IntegerArrayNode, IRDeleter> arrayNode{
               new (allocator) IntegerArrayNode(std::move(arrayValues), elementType, initialiser->Source())};
          return std::make_unique<PRValue>(arrayTypeBase, std::move(arrayNode), true);
     }

     if (dynamic_cast<const ast::InitialiserListNode*>(initialiser.get()) != nullptr)
          return ParseListInitialisation(initialiser, arrayTypeBase);

     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               std::format("Array '{}' must be initialised with initialiser-lists", name), initialiser->Source()));
     return nullptr;
}

void ecpps::ir::IR::ParseVariableDeclaration(const ast::VariableDeclarationNode& node)
{
     auto& allocator = *this->GetContext().nodeAllocator;

     if (node.GetFlags().isTypedef)
     {
          const auto* declaredType = ParseType(node.Type());
          if (declaredType == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Unknown type {} in typedef declaration", node.Type()->ToString(0)),
                         node.Source()));
               return;
          }

          auto& currentScope = this->GetContext().contextSequence.back()->GetScope();
          for (const auto& decl : node.Declarators())
          {
               const auto* idNodePtr = decl.name.get();
               const auto* idExpr = dynamic_cast<const ast::IdentifierNode*>(idNodePtr);
               if (idExpr == nullptr)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                              "Expected identifier in typedef declarator", decl.name->Source()));
                    continue;
               }

               const std::string typedefName = idExpr->Value();
               currentScope.typeAliases[typedefName] = declaredType;
          }

          return;
     }

     auto* const functionContext = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(functionContext != nullptr, "Variable declaration outside of a function is not supported");

     auto& fscope = functionContext->GetScope<FunctionScope>(); // NOLINT(clang-analyzer-core.CallAndMessage)

     const auto* declaredType = ParseType(node.Type());
     if (declaredType == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                         "Expected identifier in variable declarator", decl.name->Source()));
               continue;
          }

          const std::string varName = idExpr->Value();
          const auto* variableType = declaredType;
          bool inferLastArrayFromInitialiser = false;
          bool arrayBoundsFailed = false;
          const auto isArray = !decl.arrayLevels.empty();

          std::vector<const ast::NodePointer*> arrayLevels{};
          arrayLevels.reserve(decl.arrayLevels.size());
          for (const auto& arrayLevel : decl.arrayLevels) arrayLevels.push_back(&arrayLevel);

          for (std::size_t levelIndex = arrayLevels.size(); levelIndex-- > 0;)
          {
               const auto& arrayLevel = *arrayLevels[levelIndex];

               if (arrayLevel == nullptr)
               {
                    if (levelIndex != 0)
                    {
                         this->GetContext().diagnostics.get().diagnosticsList.push_back(
                              std::make_unique<diagnostics::TypeError>(
                                   std::format("Declaration of '{}' introduces an array of unbounded arrays which is "
                                               "not allowed, only the top-level array might be unbounded",
                                               varName),
                                   node.Source()));
                         arrayBoundsFailed = true;
                         break;
                    }
                    inferLastArrayFromInitialiser = true;
                    continue;
               }

               const auto arrayLevelExpression = ParseExpression(arrayLevel);
               if (arrayLevelExpression == nullptr)
               {
                    arrayBoundsFailed = true;
                    break;
               }

               auto constexprArraySize =
                    arrayLevelExpression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
               if (!constexprArraySize.has_value())
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::TypeError>(
                              std::format("Arrays bounds must be defined by a constant expression", varName),
                              arrayLevel->Source()));

                    auto& nestedDiagnostics = constexprArraySize.error();

                    while (!nestedDiagnostics.empty())
                    {
                         this->GetContext().diagnostics.get().diagnosticsList.push_back(
                              std::move(nestedDiagnostics.top()));
                         nestedDiagnostics.pop();
                    }
                    arrayBoundsFailed = true;
                    break;
               }
               const auto& arraySize = *constexprArraySize;
               if (!std::holds_alternative<std::uint64_t>(arraySize.variant))
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::TypeError>(
                              std::format("Arrays bounds must be defined by an integer", varName),
                              arrayLevel->Source()));
                    arrayBoundsFailed = true;
                    break;
               }
               const auto length = std::get<std::uint64_t>(arraySize.variant);

               TypeRequest arrayRequest{};
               arrayRequest.kind = TypeKind::Compound;
               arrayRequest.data = BoundedArrayRequest{.elementType = variableType, .size = length};

               variableType = GetTypeContext().Get(arrayRequest);
          }

          if (arrayBoundsFailed) continue;

          if (inferLastArrayFromInitialiser)
          {
               const auto inferredLength =
                    InferArrayLength(decl.initialiser, variableType, varName, decl.name->Source());
               if (!inferredLength.has_value()) continue;

               TypeRequest arrayRequest{};
               arrayRequest.kind = TypeKind::Compound;
               arrayRequest.data = BoundedArrayRequest{.elementType = variableType, .size = *inferredLength};
               if (!RequireComplete(variableType, decl.name->Source(), "an array element type"))
               {
                    arrayBoundsFailed = true;
                    break;
               }
               variableType = GetTypeContext().Get(arrayRequest);
          }

          bool duplicate = false;
          for (const auto& v : fscope.Locals())
          {
               if (v.Name() == varName)
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Redefinition of variable '" + varName + "'", decl.name->Source()));
               continue;
          }
          if (!RequireComplete(variableType, decl.name->Source(), "an object definition")) continue;

          auto* const function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
          runtime_assert(function != nullptr, "Function was null when parsing the function");

          Variable varEntry{varName, variableType, StorageDuration::Automatic};
          const auto ssaIndex = function->GetNextRegisterIndex();

          fscope.Locals().emplace_back(FunctionScope::LocalEntity{std::move(varEntry)});
          RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
          auto ssaNodeUnique =
               std::unique_ptr<ir::SingleAssignRegisterNode, IRDeleter>{new (allocator) ir::SingleAssignRegisterNode(
                    ssaIndex, info, variableType->Width(), variableType->Alignment(), decl.name->Source())};
          auto* ssaNode = ssaNodeUnique.get();

          function->RegisterAllocReg(varName, ssaNode);

          this->_built.push_back(std::unique_ptr<ir::AllocationNode, IRDeleter>{
               new (allocator) ir::AllocationNode(std::move(ssaNodeUnique), decl.name->Source())});

          const auto arrayContainsClass = [](typeSystem::NonowningTypePointer type)
          {
               while (true)
               {
                    const auto* arrayType = type->CastTo<typeSystem::ArrayType>();
                    if (arrayType == nullptr) break;
                    type = arrayType->ElementType();
               }
               return typeSystem::IsClass(type);
          };
          const auto diagnoseConstScalarDefaultInitialisation = [&](typeSystem::NonowningTypePointer type)
          {
               const auto* qualifiedType = type->CastTo<typeSystem::QualifiedType>();
               if (qualifiedType == nullptr || !qualifiedType->IsConst()) return;
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A const-qualified scalar object requires an initializer during default-initialization",
                         decl.name->Source()));
          };

          if (typeSystem::IsClass(variableType))
          {
               const auto* objectAddress = AddressOfAllocation(ssaNode, variableType, decl.name->Source());
               if (decl.initialiser == nullptr)
                    InitialiseAtAddress(variableType, objectAddress, nullptr, ast::InitialisationType::Default,
                                        ObjectInitialisationMode::Default, decl.name->Source());
               else
                    InitialiseAtAddress(variableType, objectAddress, &decl.initialiser, decl.initialiserType,
                                        ObjectInitialisationMode::Explicit, decl.initialiser->Source());
               continue;
          }

          if (variableType->CastTo<typeSystem::ReferenceType>() != nullptr)
          {
               if (decl.initialiser == nullptr)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "A reference variable requires an initializer", decl.name->Source()));
                    continue;
               }
               const auto* referenceStorage = AddressOfAllocation(ssaNode, variableType, decl.name->Source());
               InitialiseReferenceAtAddress(variableType, referenceStorage, &decl.initialiser, decl.initialiserType,
                                            decl.initialiser->Source());
               continue;
          }

          if (decl.initialiser == nullptr)
          {
               if (isArray && arrayContainsClass(variableType))
               {
                    const auto* objectAddress = AddressOfAllocation(ssaNode, variableType, decl.name->Source());
                    InitialiseAtAddress(variableType, objectAddress, nullptr, ast::InitialisationType::Default,
                                        ObjectInitialisationMode::Default, decl.name->Source());
               }
               else if (isArray)
               {
                    auto elementType = variableType;
                    while (const auto* arrayType = elementType->CastTo<typeSystem::ArrayType>())
                         elementType = arrayType->ElementType();
                    diagnoseConstScalarDefaultInitialisation(elementType);
               }
               else
                    diagnoseConstScalarDefaultInitialisation(variableType);
               continue;
          }

          if (isArray)
          {
               if (arrayContainsClass(variableType))
               {
                    const auto* objectAddress = AddressOfAllocation(ssaNode, variableType, decl.name->Source());
                    if (dynamic_cast<const ast::InitialiserListNode*>(decl.initialiser.get()) != nullptr)
                         InitialiseAtAddress(variableType, objectAddress, &decl.initialiser, decl.initialiserType,
                                             ObjectInitialisationMode::Explicit, decl.initialiser->Source());
                    else
                         this->GetContext().diagnostics.get().diagnosticsList.push_back(
                              diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                   "An array of class objects requires a braced initializer",
                                   decl.initialiser->Source()));
                    continue;
               }
               // Arrays are initialised with a single aggregate store. Elements without an initialiser are
               // value-initialised (zero), which is part of the aggregate that is stored.
               auto arrayInitialiser = ParseArrayInitialiser(decl.initialiser, variableType, varName);
               if (arrayInitialiser == nullptr) continue;

               auto ownedArrayNode = std::move(*arrayInitialiser).Value();
               this->_built.push_back(std::unique_ptr<ir::SSAArrayStoreNode, IRDeleter>{new (
                    allocator) ir::SSAArrayStoreNode(ssaNode, std::move(ownedArrayNode), decl.initialiser->Source())});
               continue;
          }

          auto initExpr = PickInitialiser(decl.initialiser, variableType, decl.initialiserType);
          if (initExpr == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Invalid initialiser for variable '" + varName + "'", decl.initialiser->Source()));
               continue;
          }

          if (const auto* integralInitExpr = dynamic_cast<IntegralNode*>(initExpr->Value().get());
              integralInitExpr != nullptr)
          {
               this->_built.push_back(std::unique_ptr<ir::SSAStoreIntegerNode, IRDeleter>{
                    new (allocator)
                         ir::SSAStoreIntegerNode(ssaNode, integralInitExpr->Value(), decl.initialiser->Source())});
          }
          else
          {
               // The initialiser is read as a VALUE (lvalue-to-rvalue), e.g. `int* p = otherP;` loads otherP.
               const auto* initReg = LowerExpressionLoaded(std::move(initExpr), this->_built);
               if (initReg != nullptr)
               {
                    this->_built.push_back(std::unique_ptr<ir::SSAStoreNode, IRDeleter>{
                         new (allocator) ir::SSAStoreNode(ssaNode, initReg, decl.initialiser->Source())});
               }
          }
     }
}

Expression ecpps::ir::IR::PickInitialiser(const ast::NodePointer& expression,
                                          typeSystem::NonowningTypePointer desiredType, ast::InitialisationType type)
{
     switch (type)
     {
     case ast::InitialisationType::ParenthesisedExpressionList:
          return this->ParseDirectInitialisation(expression, desiredType);
     case ast::InitialisationType::BracedInitialiser: return this->ParseListInitialisation(expression, desiredType);
     case ast::InitialisationType::EqualInitialiser:
     {
          return this->ParseCopyInitialisation(expression, desiredType);
     }
     case ast::InitialisationType::Default:
     {
          // TODO: Implement
     }
     break;
     default:
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build("Unknown initialisation type",
                                                                               expression->Source()));
          break;
     }
     throw TracedException(std::logic_error("Unknown initialisation type"));
}

Expression ecpps::ir::IR::ParseDirectInitialisation(const ecpps::ast::NodePointer& expression,
                                                    ecpps::typeSystem::NonowningTypePointer desiredType)
{
     auto operand = ParseExpression(expression);
     if (IsBoolean(desiredType))
     {
          if (IsBoolean(operand->Type())) return operand;
     }
     auto converted = ConvertTo(std::move(operand), desiredType);
     if (converted == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot convert initialiser to variable type of '" + desiredType->Name() + "'",
                    expression->Source()));
     }
     return converted;
}

Expression ecpps::ir::IR::ParseCopyInitialisation(const ecpps::ast::NodePointer& expression,
                                                  ecpps::typeSystem::NonowningTypePointer desiredType)
{
     runtime_assert(desiredType != nullptr, "Invalid variable type");
     if (desiredType == nullptr || expression == nullptr) return nullptr;
     if (dynamic_cast<const ast::InitialiserListNode*>(expression.get()) != nullptr)
          return this->ParseListInitialisation(expression, desiredType);
     auto operand = ParseExpression(expression);

     auto converted = ConvertTo(std::move(operand), desiredType);
     if (converted == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot convert initialiser to variable type of '" + desiredType->Name() + "'",
                    expression->Source()));
     }
     return converted;
}

Expression ecpps::ir::IR::ParseZeroInitialisation(typeSystem::NonowningTypePointer desiredType, const Location& source)
{
     if (desiredType == nullptr) return nullptr;
     if (desiredType->CastTo<typeSystem::PointerType>() != nullptr)
     {
          return std::make_unique<PRValue>(desiredType,
                                           std::unique_ptr<IntegralNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator) IntegralNode(0, source)},
                                           true);
     }
     if (IsScalar(desiredType))
     {
          if (IsIntegral(desiredType))
          {
               return std::make_unique<PRValue>(desiredType,
                                                std::unique_ptr<IntegralNode, IRDeleter>{
                                                     new (*this->GetContext().nodeAllocator) IntegralNode(0, source)},
                                                true);
          }
     }
     else if (const auto* const arrayType = desiredType->CastTo<typeSystem::ArrayType>(); arrayType != nullptr)
     {
          const auto* const elementType = arrayType->ElementType()->CastTo<typeSystem::IntegralType>();
          if (elementType != nullptr)
          {
               std::vector<std::uint32_t> values(arrayType->ElementCount(), 0u);
               auto arrayNode = std::unique_ptr<IntegerArrayNode, IRDeleter>{
                    new (*this->GetContext().nodeAllocator) IntegerArrayNode(std::move(values), elementType, source)};
               return std::make_unique<PRValue>(desiredType, std::move(arrayNode), true);
          }
     }
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "Zero initialisation is not supported for this type", source));
     return nullptr;
}

Expression ecpps::ir::IR::ParseValueInitialisation(typeSystem::NonowningTypePointer desiredType, const Location& source)
{
     // TODO: Classes
     if (desiredType == nullptr) return nullptr;
     return ParseZeroInitialisation(desiredType, source);
}

Expression ecpps::ir::IR::ParseListInitialisation(const ast::NodePointer& expression,
                                                  typeSystem::NonowningTypePointer desiredType)
{
     if (expression == nullptr || desiredType == nullptr) return nullptr;
     const auto* initialiserListNode = dynamic_cast<const ast::InitialiserListNode*>(expression.get());
     if (initialiserListNode == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Expected an initialiser list for list initialisation", expression->Source()));
          return nullptr;
     }

     // TODO: Check if aggregate
     if (IsClass(desiredType) /* && IsAggregate(desiredType) */)
     {
          return ParseAggregateInitialisation(expression, desiredType);
     }
     std::vector<Expression> initialisers{};
     for (const auto& init : initialiserListNode->Initialisers())
     {
          auto parsedInit = ParseExpression(init);
          if (parsedInit == nullptr) return nullptr;
          initialisers.push_back(std::move(parsedInit));
     }
     if (initialisers.empty())
     {
          return ParseValueInitialisation(desiredType, expression->Source());
     }

     if (const auto* const arrayType = desiredType->CastTo<typeSystem::ArrayType>(); arrayType != nullptr)
     {
          const auto* const elementType = arrayType->ElementType()->CastTo<typeSystem::IntegralType>();
          if (elementType == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "List initialisation is only supported for arrays of integral types", expression->Source()));
               return nullptr;
          }

          if (initialisers.size() > arrayType->ElementCount())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Cannot initialise an array with more elements than it can hold. Provided {} "
                                     "elements for an array of `{}` `{}`s",
                                     initialisers.size(), arrayType->ElementCount(),
                                     arrayType->ElementType()->RawName()),
                         expression->Source()));
               return nullptr;
          }

          std::vector<std::uint32_t> values{};
          values.reserve(arrayType->ElementCount());
          for (const auto& initialiser : initialisers)
          {
               if (IsNarrowingConversion(initialiser, arrayType->ElementType()))
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              std::format("Narrowing conversion from '{}' to '{}' is not allowed in array "
                                          "aggregate initialization",
                                          initialiser->Type()->Name(), arrayType->ElementType()->Name()),
                              initialiser->Value()->Source()));
                    return nullptr;
               }
               const auto constant = initialiser->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
               if (!constant.has_value() || !std::holds_alternative<std::uint64_t>(constant->variant))
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Array elements must currently be initialised with constant integers",
                              initialiser->Value()->Source()));
                    return nullptr;
               }
               values.push_back(static_cast<std::uint32_t>(std::get<std::uint64_t>(constant->variant)));
          }
          values.resize(arrayType->ElementCount(), 0u);

          auto arrayNode = std::unique_ptr<IntegerArrayNode, IRDeleter>{
               new (*this->GetContext().nodeAllocator)
                    IntegerArrayNode(std::move(values), elementType, expression->Source())};
          return std::make_unique<PRValue>(desiredType, std::move(arrayNode), true);
     }

     if (initialisers.size() == 1 && !IsReference(desiredType))
     {
          auto initialiserExpression = std::move(initialisers.front());
          if (IsNarrowingConversion(initialiserExpression, desiredType))
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Narrowing conversion from '" + initialiserExpression->Type()->Name() + "' to '" +
                              desiredType->Name() + "' is not allowed in list initialisation",
                         expression->Source()));
               return nullptr;
          }
          return ConvertTo(std::move(initialiserExpression), desiredType);
     }
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "List initialisation with multiple initialisers is not supported yet", expression->Source()));
     return nullptr;
}
