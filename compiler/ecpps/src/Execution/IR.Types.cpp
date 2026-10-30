#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include "../Parsing/ASTs/Type.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "../TypeSystem/ClassType.h"
#include "Constexpr.h"
#include "Context.h"
#include "Expressions.h"
#include "HighLevel.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"
#include "TypeSystem/ClassInfo.h"

#include "IR.Internal.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

using ecpps::ir::detail::DeclaratorIdentifier;
using ecpps::ir::detail::DeclaresNonType;
using ecpps::ir::detail::Derivation;
using ecpps::ir::detail::ErrorRequest;
using ecpps::ir::detail::IsValidIntegerSpecifierCombination;
using ecpps::ir::detail::LastDerivation;
using ecpps::ir::detail::QualifiersFrom;

namespace
{
     ecpps::ir::ComparisonPredicate SelectPredicate(const ecpps::ast::Operator operator_, const bool isSigned)
     {
          using enum ecpps::ir::ComparisonPredicate;
          using ecpps::ast::Operator;
          switch (operator_)
          {
          case Operator::EqualsSign: return Equal;
          case Operator::NotEqual: return NotEqual;
          case Operator::Less: return isSigned ? SignedLess : UnsignedLess;
          case Operator::LessEqual: return isSigned ? SignedLessEqual : UnsignedLessEqual;
          case Operator::Greater: return isSigned ? SignedGreater : UnsignedGreater;
          case Operator::GreaterEqual: return isSigned ? SignedGreaterEqual : UnsignedGreaterEqual;
          default: throw TracedException(std::logic_error("Invalid relational operator"));
          }
     }
} // namespace

[[nodiscard]] ecpps::ir::TypeRequest ecpps::ir::IR::TypeASTToRequest(const ast::NodePointer& type)
{
     auto& typeContext = GetTypeContext();

     const ast::Node* base = type.get();
     ecpps::ir::TypeRequest request{};

     if (const auto* basicType = dynamic_cast<const ast::BasicType*>(base); basicType != nullptr)
     {
          const auto& value = basicType->Value();

          const auto qualifiers = QualifiersFrom(basicType->IsConst(), basicType->IsVolatile());
          const auto lookup = LookupTypeName(value);
          switch (lookup.kind)
          {
          case TypeNameLookup::Kind::Class:
               request.kind = TypeKind::Compound;
               request.qualifiers = qualifiers;
               request.data = ClassRequest{.id = lookup.classId};
               return request;

          case TypeNameLookup::Kind::NonType:
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "'" + value + "' is not a type name", basicType->Source()));
               return ErrorRequest();

          case TypeNameLookup::Kind::Alias:
          {
               const auto* targetType = lookup.alias;
               if (const auto* classType = targetType->CastTo<typeSystem::ClassType>())
               {
                    request.kind = TypeKind::Compound;
                    request.qualifiers = QualifiersFrom(basicType->IsConst() || classType->IsConst(),
                                                        basicType->IsVolatile() || classType->IsVolatile());
                    request.data = ClassRequest{.id = classType->Info().id};
                    return request;
               }
               request.qualifiers = qualifiers;

               if (const auto* intType = targetType->CastTo<typeSystem::IntegralType>())
               {
                    request.kind = TypeKind::Fundamental;
                    StandardSignedIntegerRequest signedRequest{};
                    signedRequest.signedness = intType->Sign();
                    signedRequest.size = intType->Kind();
                    signedRequest.isCharWithoutSign = (intType == typeSystem::g_char.get());
                    request.data = signedRequest;
               }
               else if (const auto* ptrType = targetType->CastTo<typeSystem::PointerType>())
               {
                    request.kind = TypeKind::Compound;
                    request.data = PointerRequest{.elementType = ptrType->BaseType()};
               }
               else if (targetType == typeSystem::g_void.get())
               {
                    request.kind = TypeKind::Fundamental;
                    request.data = VoidRequest{};
               }
               else
                    throw TracedException("Type aliases for this kind of type not yet implemented");

               if (basicType->IsConst() && basicType->IsVolatile())
                    request.qualifiers = typeSystem::Qualifiers::ConstVolatile;
               else if (basicType->IsConst())
                    request.qualifiers = typeSystem::Qualifiers::Const;
               else if (basicType->IsVolatile())
                    request.qualifiers = typeSystem::Qualifiers::Volatile;

               return request;
          }
          case TypeNameLookup::Kind::NotFound: break;
          }

          request.kind = TypeKind::Fundamental;
          request.qualifiers = qualifiers;

          const auto fail = [&](std::string message)
          {
               std::vector<diagnostics::DiagnosticsMessage> diagnosticsList{};
               diagnosticsList.push_back(diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::move(message), basicType->Source()));
               return ErrorRequest(std::move(diagnosticsList));
          };

          if (value == "void")
          {
               request.data = VoidRequest{};
               return request;
          }
          if (value == "bool")
          {
               request.data = BooleanRequest{};
               return request;
          }

          std::vector<std::string_view> tokens;
          std::string_view sv{value};
          while (!sv.empty())
          {
               const auto pos = sv.find(' ');
               tokens.emplace_back(sv.substr(0, pos));
               if (pos == std::string_view::npos) break;
               sv.remove_prefix(pos + 1);
          }
          using std::operator""sv;

          constexpr std::array integerKeywords{"char"sv, "short"sv, "int"sv, "long"sv, "signed"sv, "unsigned"sv};
          constexpr std::array unsupportedKeywords{"float"sv,   "double"sv,   "wchar_t"sv,
                                                   "char8_t"sv, "char16_t"sv, "char32_t"sv};
          for (const auto token : tokens)
          {
               if (std::ranges::contains(unsupportedKeywords, token))
                    return fail(std::format("The type '{}' is not supported yet", token));
               if (!std::ranges::contains(integerKeywords, token))
                    return fail(tokens.size() == 1 ? std::format("Unknown type name '{}'", value)
                                                   : std::format("Invalid type specifier '{}'", value));
          }
          if (!IsValidIntegerSpecifierCombination(tokens))
               return fail(std::format("Invalid type specifier '{}'", value));

          StandardSignedIntegerRequest signedRequest{};
          const auto has = [&](const std::string_view t)
          {
               return std::ranges::contains(tokens, t);
          };
          const auto longCount = std::ranges::count(tokens, "long"sv);
          const bool isChar = has("char");
          const bool isShort = has("short");
          const bool isSigned = has("signed");
          const bool isUnsigned = has("unsigned");

          if (isChar && !isSigned && !isUnsigned) signedRequest.isCharWithoutSign = true;
          else
          {
               signedRequest.signedness =
                    isUnsigned ? typeSystem::Signedness::Unsigned : typeSystem::Signedness::Signed;
               if (isChar) signedRequest.size = typeSystem::TypeKind::Char;
               else if (isShort)
                    signedRequest.size = typeSystem::TypeKind::Short;
               else if (longCount == 1)
                    signedRequest.size = typeSystem::TypeKind::Long;
               else if (longCount >= 2)
                    signedRequest.size = typeSystem::TypeKind::LongLong;
               else
                    signedRequest.size = typeSystem::TypeKind::Int;
          }
          request.data = signedRequest;
          return request;
     }
     if (const auto* pointerType = dynamic_cast<const ast::PointerType*>(base); pointerType != nullptr)
     {
          const auto baseTypeRequest = TypeASTToRequest(pointerType->BaseType());
          const auto* baseType = typeContext.Get(baseTypeRequest);
          request.kind = TypeKind::Compound;

          request.data = PointerRequest{.elementType = baseType};
          return request;
     }
     if (const auto* qualifiedType = dynamic_cast<const ast::QualifiedType*>(base); qualifiedType != nullptr)
     {
          if (qualifiedType->Sections().Size() == 0)
          {
               const auto* unqualified = qualifiedType->UnqualifiedType().get();
               if (dynamic_cast<const ast::BasicType*>(unqualified))
                    return TypeASTToRequest(qualifiedType->UnqualifiedType());
          }

          throw TracedException("Qualified types with namespaces not yet implemented");
     }
     if (const auto* classType = dynamic_cast<const ast::ClassType*>(base); classType != nullptr)
     {
          const auto id = dynamic_cast<const ast::ClassDefNode*>(classType) != nullptr
                               ? DefineClass(static_cast<const ast::ClassDefNode&>(*classType)) // NOLINT
                               : ResolveElaboratedClass(*classType);
          if (id == typeSystem::InvalidClassId) return ErrorRequest();
          request.kind = TypeKind::Compound;
          request.qualifiers = QualifiersFrom(classType->IsConst(), classType->IsVolatile());
          request.data = ClassRequest{.id = id};
          return request;
     }

     throw TracedException("not implemented");
}

ecpps::typeSystem::NonowningTypePointer ecpps::ir::IR::ParseType(const ast::NodePointer& type)
{
     auto& typeContext = GetTypeContext();
     const auto request = TypeASTToRequest(type);

     return typeContext.Get(request);
}

Expression ecpps::ir::IR::ConvertTo(Expression expression, typeSystem::NonowningTypePointer toType) const
{
     if (expression == nullptr || toType == nullptr) return nullptr;

     const auto constexprResult = expression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
     if (constexprResult.has_value())
     {
          const auto& value = *constexprResult;
          try
          {
               expression =
                    ConstantEvaluationResultToExpression(value, expression->Type(), *this->GetContext().nodeAllocator);
          }
          catch (...)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::ConstantEvaluationWarning>(
                         "Failed to use the constant expression evaluation result", expression->Value()->Source()));
          }
     }

     const auto comparison = toType->CompareTo(expression->Type());

     if (!comparison.IsValid())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
          if (conversion == typeSystem::ConversionSequence::ConversionKind::BooleanConversion)
          {
               const auto source = expression->Value()->Source();
               return MakeNonZeroTest(std::move(expression), source);
          }
          if (conversion == typeSystem::ConversionSequence::ConversionKind::IntegralConversion)
          {
               const auto* intType = toType->CastTo<typeSystem::IntegralType>();
               runtime_assert(intType != nullptr,
                              std::format("Presumed integral type {} turned out not to be one", toType->RawName()));
               return ConvertIntegral(std::move(expression), intType);
          }
          if (conversion == typeSystem::ConversionSequence::ConversionKind::ArrayToPointer)
          {
               const auto* pointerType = toType->CastTo<typeSystem::PointerType>();
               runtime_assert(pointerType != nullptr,
                              std::format("Presumed pointer type {} turned out not to be one", toType->RawName()));

               runtime_assert(expression->IsRValue() || expression->IsLValue(), "Expected an rvalue or an lvalue for "
                                                                                "the array-to-pointer conversion. See "
                                                                                "[conv.array]");

               const auto wasConstexpr = expression->IsConstantExpression();
               if (expression->IsPRValue())
               {
                    NodePointer decayNode{};

                    if (auto* const intArray = dynamic_cast<IntegerArrayNode*>(expression->Value().get()))
                    {
                         const auto source = intArray->Source();

                         decayNode = std::unique_ptr<TemporaryIntegerArrayDecayNode, IRDeleter>(
                              new (*this->GetContext().nodeAllocator)
                                   TemporaryIntegerArrayDecayNode(std::move(expression), source));
                    }
                    else
                         throw TracedException("array-to-pointer is not supported yet");

                    return std::make_unique<PRValue>(pointerType, std::move(decayNode), wasConstexpr);
               }

               if (expression->IsLValue())
               {
                    std::string variableName{};
                    if (const auto* const referenceNode = dynamic_cast<const ReferenceNode*>(expression->Value().get()))
                         variableName = referenceNode->Address();
                    else if (const auto* const loadNode = dynamic_cast<const LoadNode*>(expression->Value().get()))
                         variableName = loadNode->Address();

                    const auto source = expression->Value()->Source();
                    if (!variableName.empty())
                    {
                         const auto* const allocReg = ResolveAllocReg(variableName);
                         if (allocReg == nullptr)
                         {
                              this->GetContext().diagnostics.get().diagnosticsList.push_back(
                                   diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                        "Cannot resolve allocation register for variable '" + variableName + "'",
                                        source));
                              return nullptr;
                         }

                         auto& allocator = *this->GetContext().nodeAllocator;
                         auto registerReference =
                              std::make_unique<LValue>(expression->Type(),
                                                       std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                                            new (allocator) SSARegisterReferenceNode(allocReg, source)},
                                                       false);

                         NodePointer decayNode = std::unique_ptr<LoadArrayDecayNode, IRDeleter>(
                              new (allocator) LoadArrayDecayNode(std::move(registerReference), source));

                         return std::make_unique<PRValue>(pointerType, std::move(decayNode), wasConstexpr);
                    }

                    auto arrayAddress = this->ParseAddressOfExpression(std::move(expression), source);
                    if (arrayAddress == nullptr) return nullptr;
                    return std::make_unique<PRValue>(
                         pointerType,
                         std::unique_ptr<high::PointerConversionNode, IRDeleter>{
                              new (*this->GetContext().nodeAllocator)
                                   high::PointerConversionNode(std::move(arrayAddress), pointerType, source)},
                         wasConstexpr);
               }

               throw TracedException(std::logic_error("Unsupported conversion"));
          }
          if (conversion == typeSystem::ConversionSequence::ConversionKind::PointerConversion ||
              conversion == typeSystem::ConversionSequence::ConversionKind::QualifierConversion)
          {
               const auto wasConstexpr = expression->IsConstantExpression();
               const auto source = expression->Value()->Source();
               const bool isPRValue = expression->IsPRValue();
               const bool isXValue = expression->IsXValue();
               const bool isLValue = expression->IsLValue();

               auto castNode = std::unique_ptr<high::PointerConversionNode, IRDeleter>(
                    new (*this->GetContext().nodeAllocator)
                         high::PointerConversionNode(std::move(expression), toType, source));

               if (isPRValue) return std::make_unique<PRValue>(toType, std::move(castNode), wasConstexpr);
               if (isXValue) return std::make_unique<XValue>(toType, std::move(castNode), wasConstexpr);
               if (isLValue) return std::make_unique<LValue>(toType, std::move(castNode), wasConstexpr);

               throw TracedException(std::logic_error("Unknown value category"));
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
          return std::make_unique<PRValue>(type, std::move(std::move(*expression).Value()),
                                           expression->IsConstantExpression());
     }
     if (IsArithmetic(expressionType))
          return std::make_unique<PRValue>(type,
                                           std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::ConvertNode(std::move(expression), type, source)},
                                           false);
     return nullptr;
}

Expression ecpps::ir::IR::MakeNonZeroTest(Expression operand, const Location& source) const
{
     auto& allocator = *this->GetContext().nodeAllocator;
     const auto* operandType = operand->Type();
     const bool wasConstexpr = operand->IsConstantExpression();

     TypeRequest boolRequest{.kind = TypeKind::Fundamental, .data = BooleanRequest{}};
     const auto* boolType = GetTypeContext().Get(boolRequest);

     auto zero = std::make_unique<PRValue>(
          operandType, std::unique_ptr<IntegralNode, IRDeleter>{new (allocator) IntegralNode(0, source)}, true);

     auto compare = std::unique_ptr<high::CompareNode, IRDeleter>{new (allocator) high::CompareNode(
          std::move(operand), std::move(zero), ComparisonPredicate::NotEqual, source)};

     return std::make_unique<PRValue>(boolType, std::move(compare), wasConstexpr);
}

std::optional<ecpps::ir::IR::CommonOperands> ecpps::ir::IR::UsualArithmeticConversions(Expression left,
                                                                                       Expression right) const
{
     const auto* leftIntegral = left->Type()->CastTo<typeSystem::IntegralType>();
     const auto* rightIntegral = right->Type()->CastTo<typeSystem::IntegralType>();

     if (leftIntegral == nullptr || rightIntegral == nullptr)
     {
          // TODO: Classes
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot perform this binary operation on " + left->Type()->Name() + " and " + right->Type()->Name(),
                    left->Value()->Source()));
          return std::nullopt;
     }

     leftIntegral = typeSystem::PromoteInteger(leftIntegral);
     rightIntegral = typeSystem::PromoteInteger(rightIntegral);

     const auto* commonType = leftIntegral->CommonWith(rightIntegral)->CastTo<typeSystem::IntegralType>();
     if (commonType == nullptr)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot find a common integral type between " + left->Type()->Name() + " and " +
                         right->Type()->Name(),
                    left->Value()->Source()));
          return std::nullopt;
     }

     const auto convert = [&](Expression operand) -> Expression
     {
          if (operand->Type() == commonType) return operand;

          const auto innerSource = operand->Value()->Source();
          const auto wasConstexpr = operand->IsConstantExpression();
          return std::make_unique<PRValue>(commonType,
                                           std::unique_ptr<high::ConvertNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     high::ConvertNode(std::move(operand), commonType, innerSource)},
                                           wasConstexpr);
     };

     return CommonOperands{.left = convert(std::move(left)), .right = convert(std::move(right)), .type = commonType};
}

Expression ecpps::ir::IR::ParseRelationalExpression(Expression left, const ast::Operator operator_, Expression right,
                                                    const Location& source) const
{
     const auto* leftPointer = left->Type()->CastTo<typeSystem::PointerType>();
     const auto* rightPointer = right->Type()->CastTo<typeSystem::PointerType>();

     bool isSigned = false;
     if (leftPointer != nullptr && rightPointer != nullptr)
     {
          if (leftPointer->BaseType() != rightPointer->BaseType())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Cannot compare pointers to different types (" + left->Type()->Name() + " and " +
                              right->Type()->Name() + ")",
                         source));
               return nullptr;
          }
     }
     else
     {
          auto operands = UsualArithmeticConversions(std::move(left), std::move(right));
          if (!operands) return nullptr;

          isSigned = operands->type->Sign() == typeSystem::Signedness::Signed;
          left = std::move(operands->left);
          right = std::move(operands->right);
     }

     TypeRequest boolRequest{.kind = TypeKind::Fundamental, .data = BooleanRequest{}};
     const auto* boolType = GetTypeContext().Get(boolRequest);

     return std::make_unique<PRValue>(
          boolType,
          std::unique_ptr<high::CompareNode, IRDeleter>{new (*this->GetContext().nodeAllocator) high::CompareNode(
               std::move(left), std::move(right), SelectPredicate(operator_, isSigned), source)},
          false);
}

ecpps::ir::IR::TypeNameLookup ecpps::ir::IR::LookupTypeName(const std::string& name) const
{
     for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
     {
          const auto& scope = context->GetScope();
          if (DeclaresNonType(scope, name)) return {.kind = TypeNameLookup::Kind::NonType};
          if (const auto it = scope.typeAliases.find(name); it != scope.typeAliases.end())
               return {.kind = TypeNameLookup::Kind::Alias, .alias = it->second};
          if (const auto it = scope.classes.find(name); it != scope.classes.end())
               return {.kind = TypeNameLookup::Kind::Class, .classId = it->second};
     }
     return {};
}

std::optional<ecpps::typeSystem::ClassId> ecpps::ir::IR::LookupClassForElaboration(const std::string& name,
                                                                                   const Location& source)
{
     for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
     {
          const auto& scope = context->GetScope();
          if (const auto it = scope.classes.find(name); it != scope.classes.end()) return it->second;
          if (scope.typeAliases.contains(name))
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("'{}' names a typedef, not a class; it cannot be used in an elaborated "
                                     "type specifier",
                                     name),
                         source));
               return typeSystem::InvalidClassId;
          }
     }
     return std::nullopt;
}

ecpps::ir::Scope& ecpps::ir::IR::NearestNamespaceOrBlockScope(void) const
{
     for (const auto& context : this->GetContext().contextSequence | std::views::reverse)
          if (dynamic_cast<const ClassContext*>(context.get()) == nullptr) return context->GetScope();
     throw TracedException("No enclosing namespace scope");
}

std::vector<ecpps::typeSystem::ScopeSegment> ecpps::ir::IR::EnclosingPathUpTo(const Scope& target) const
{
     std::vector<typeSystem::ScopeSegment> path{};
     for (const auto& context : this->GetContext().contextSequence)
     {
          const auto& scope = context->GetScope();
          if (const auto* ns = dynamic_cast<const NamespaceScope*>(&scope); ns && ns->Name().has_value())
               path.push_back({typeSystem::ScopeKind::Namespace, *ns->Name()}); // TODO: unnamed namespaces are skipped
          else if (const auto* cs = dynamic_cast<const ClassScope*>(&scope); cs && !cs->name.empty())
               path.push_back({typeSystem::ScopeKind::Class, cs->name});
          else if (const auto* fn = dynamic_cast<const FunctionScope*>(&scope))
               path.push_back({typeSystem::ScopeKind::Function, fn->Name().value_or("__unknown")}); // local class
          if (&scope == &target) break;
     }
     return path;
}

bool ecpps::ir::IR::RequireComplete(const typeSystem::NonowningTypePointer type, const Location& source,
                                    const std::string_view what) const
{
     if (type == nullptr || type->IsComplete()) return true;
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               std::format("Invalid use of incomplete type '{}' in {}", type->Name(), what), source));
     return false;
}

std::optional<std::uint64_t> ecpps::ir::IR::EvaluateArrayBound(const ast::NodePointer& bound, const std::string& name)
{
     auto& list = this->GetContext().diagnostics.get().diagnosticsList;

     const auto expression = ParseExpression(bound);
     if (expression == nullptr) return std::nullopt;

     auto constant = expression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
     if (!constant.has_value())
     {
          list.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("The bound of array '{}' must be a constant expression", name), bound->Source()));
          auto& nested = constant.error();
          while (!nested.empty())
          {
               list.push_back(std::move(nested.top()));
               nested.pop();
          }
          return std::nullopt;
     }
     if (!std::holds_alternative<std::uint64_t>(constant->variant))
     {
          list.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("The bound of array '{}' must be an integer", name), bound->Source()));
          return std::nullopt;
     }
     const auto length = std::get<std::uint64_t>(constant->variant);
     if (length == 0)
     {
          list.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("The bound of array '{}' must be greater than zero", name), bound->Source()));
          return std::nullopt;
     }
     return length;
}

ecpps::ir::IR::DeclaratorTypeResult ecpps::ir::IR::ResolveDeclaratorType(const ast::Declarator& declarator,
                                                                         typeSystem::NonowningTypePointer base,
                                                                         const std::string& name,
                                                                         const bool allowUnboundedOutermost)
{
     const DeclaratorTypeResult failure{.type = nullptr, .unboundedArray = false, .failed = true};
     auto& list = this->GetContext().diagnostics.get().diagnosticsList;
     const auto* idNode = DeclaratorIdentifier(declarator);
     const Location idSource = idNode != nullptr ? idNode->Source() : Location{0, 0, 0};
     const auto* current = base;

     for (const auto& op : declarator.ptrOperators)
     {
          if (op.kind == ast::PtrOperator::Kind::Pointer)
          {
               TypeRequest request{};
               request.kind = TypeKind::Compound;
               request.qualifiers = QualifiersFrom(op.isConst, op.isVolatile);
               request.data = PointerRequest{.elementType = current};
               current = GetTypeContext().Get(request);
          }
          else if (op.kind == ast::PtrOperator::Kind::LValueReference ||
                   op.kind == ast::PtrOperator::Kind::RValueReference)
          {
               auto referenceKind = op.kind == ast::PtrOperator::Kind::LValueReference
                                         ? typeSystem::ReferenceType::Kind::LValue
                                         : typeSystem::ReferenceType::Kind::RValue;
               if (const auto* existingReference = current->CastTo<typeSystem::ReferenceType>())
               {
                    referenceKind = existingReference->GetKind() == typeSystem::ReferenceType::Kind::LValue ||
                                              referenceKind == typeSystem::ReferenceType::Kind::LValue
                                         ? typeSystem::ReferenceType::Kind::LValue
                                         : typeSystem::ReferenceType::Kind::RValue;
                    current = existingReference->BaseType();
               }

               TypeRequest request{};
               request.kind = TypeKind::Compound;
               request.data = ReferenceRequest{.elementType = current, .kind = referenceKind};
               current = GetTypeContext().Get(request);
          }
          else
               throw TracedException("TODO: pointers to members are not implemented yet");
     }

     bool unbounded = false;
     for (std::size_t index = declarator.suffixes.size(); index-- > 0;)
     {
          const auto* array = std::get_if<ast::ArrayBound>(&declarator.suffixes[index]);
          if (array == nullptr) throw TracedException("TODO: function declarators are not implemented yet");

          if (array->size == nullptr)
          {
               if (current->CastTo<typeSystem::ReferenceType>() != nullptr)
               {
                    list.push_back(std::make_unique<diagnostics::TypeError>(
                         std::format("Declaration of '{}' introduces an array of references, which is not allowed",
                                     name),
                         idSource));
                    return failure;
               }
               if (index != 0) // only the outermost array (applied last) may omit its bound
               {
                    list.push_back(std::make_unique<diagnostics::TypeError>(
                         std::format("Declaration of '{}' introduces an array of unbounded arrays which is not "
                                     "allowed, only the outermost array might be unbounded",
                                     name),
                         idSource));
                    return failure;
               }
               if (!RequireComplete(current, idSource, "an array element type")) return failure;
               unbounded = true;
               continue;
          }

          const auto length = EvaluateArrayBound(array->size, name);
          if (!length.has_value()) return failure;
          if (current->CastTo<typeSystem::ReferenceType>() != nullptr)
          {
               list.push_back(std::make_unique<diagnostics::TypeError>(
                    std::format("Declaration of '{}' introduces an array of references, which is not allowed", name),
                    array->size->Source()));
               return failure;
          }
          if (!RequireComplete(current, array->size->Source(), "an array element type")) return failure;

          TypeRequest request{};
          request.kind = TypeKind::Compound;
          request.data = BoundedArrayRequest{.elementType = current, .size = *length};
          current = GetTypeContext().Get(request);
     }

     const auto rejectUnbounded = [&]
     {
          list.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("The declaration of '{}' requires an array bound", name), idSource));
          return failure;
     };

     if (declarator.nested != nullptr)
     {
          if (unbounded && LastDerivation(*declarator.nested) != Derivation::None)
               throw TracedException("TODO: pointers to arrays of unknown bound are not implemented yet");
          auto inner = ResolveDeclaratorType(*declarator.nested, current, name, allowUnboundedOutermost);
          if (unbounded && !inner.failed)
          {
               if (!allowUnboundedOutermost) return rejectUnbounded();
               inner.unboundedArray = true;
          }
          return inner;
     }

     if (unbounded && !allowUnboundedOutermost) return rejectUnbounded();
     return {.type = current, .unboundedArray = unbounded, .failed = false};
}

bool ecpps::ir::IR::RejectClassByValue(const typeSystem::NonowningTypePointer type, const Location& source,
                                       const std::string_view what) const
{
     if (!typeSystem::IsClass(type)) return false;
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               std::format("Class type '{}' is not supported as {} yet", type->Name(), what), source));
     return true;
}
