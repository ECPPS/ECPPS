#include "IR.Internal.h"
#include <RuntimeAssert.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include "Context.h"
#include "Execution/MemoryAccess.h"
#include "Expressions.h"
#include "HighLevel.h"
#include "Machine/Mangling.h"
#include "Parsing/AST.h"
#include "Parsing/ASTs/General.h"
#include "Parsing/ASTs/Type.h"
#include "Shared/Error.h"
#include "TypeSystem/ArithmeticTypes.h"
#include "TypeSystem/ClassInfo.h"
#include "TypeSystem/TypeBase.h"

namespace ecpps::ir::detail
{
     void InitialiseBuiltinTypes(Context& context)
     {
          auto& types = context.globalScope->types;
          auto& typeContext = GetTypeContext();
          const auto addIntegerType = [&](const typeSystem::TypeKind size, const typeSystem::Signedness signedness,
                                          const bool isCharWithoutSign = false)
          {
               TypeRequest request{.kind = TypeKind::Fundamental,
                                   .data = StandardSignedIntegerRequest{.size = size,
                                                                        .signedness = signedness,
                                                                        .isCharWithoutSign = isCharWithoutSign}};
               types.insert(typeContext.Get(request));
          };

          TypeRequest voidRequest{.kind = TypeKind::Fundamental, .data = VoidRequest{}};
          types.insert(typeContext.Get(voidRequest));
          addIntegerType(typeSystem::TypeKind::Int, typeSystem::Signedness::Signed, true);
          addIntegerType(typeSystem::TypeKind::Char, typeSystem::Signedness::Signed);
          addIntegerType(typeSystem::TypeKind::Char, typeSystem::Signedness::Unsigned);
          addIntegerType(typeSystem::TypeKind::Short, typeSystem::Signedness::Signed);
          addIntegerType(typeSystem::TypeKind::Int, typeSystem::Signedness::Signed);
          addIntegerType(typeSystem::TypeKind::Long, typeSystem::Signedness::Signed);
          addIntegerType(typeSystem::TypeKind::LongLong, typeSystem::Signedness::Signed);
          addIntegerType(typeSystem::TypeKind::Short, typeSystem::Signedness::Unsigned);
          addIntegerType(typeSystem::TypeKind::Int, typeSystem::Signedness::Unsigned);
          addIntegerType(typeSystem::TypeKind::Long, typeSystem::Signedness::Unsigned);
          addIntegerType(typeSystem::TypeKind::LongLong, typeSystem::Signedness::Unsigned);

          TypeRequest boolRequest{.kind = TypeKind::Fundamental, .data = BooleanRequest{}};
          types.insert(typeContext.Get(boolRequest));
     }

     [[nodiscard]] std::string FormatTypeName(const ecpps::typeSystem::NonowningTypePointer type)
     {
          if (type == nullptr) return "__unknown";
          std::string name = type->Name();
          while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())) != 0) name.pop_back();
          return name;
     }

     [[nodiscard, maybe_unused]] ecpps::ir::MemoryAccess AccessFor(const ecpps::typeSystem::NonowningTypePointer type)
     {
          ecpps::ir::MemoryAccess access{};
          access.alignment = static_cast<std::uint32_t>(type->Alignment());
          access.isAggregate = type->Traits().Has(ecpps::typeSystem::TypeTraitEnum::Class) ||
                               type->Traits().Has(ecpps::typeSystem::TypeTraitEnum::Array);
          if (const auto* qualified = type->CastTo<ecpps::typeSystem::QualifiedType>())
               access.isVolatile = qualified->IsVolatile();
          return access;
     }
     [[nodiscard]] std::size_t ConversionCost(const ecpps::typeSystem::ConversionSequence::ConversionKind kind) noexcept
     {
          using Kind = ecpps::typeSystem::ConversionSequence::ConversionKind;
          switch (kind)
          {
          case Kind::LvalueToRValue:
          case Kind::ArrayToPointer:
          case Kind::FunctionToPointer:
          case Kind::Materialisation:
          case Kind::QualifierConversion: return 0;
          case Kind::IntegralPromotion:
          case Kind::FloatingPointPromotion: return 1;
          case Kind::IntegralConversion:
          case Kind::FloatingPointConversion:
          case Kind::FloatingIntegralConversion:
          case Kind::PointerConversion:
          case Kind::MemberFunctionConversion:
          case Kind::FunctionPointerConversion: return 2;
          case Kind::BooleanConversion: return 3;
          case Kind::UserDefined: return 4;
          case Kind::Ellipsis: return 5;
          }
          return 5;
     }
     std::string FormatFunctionSignature(const std::shared_ptr<ecpps::ir::FunctionScope>& func)
     {
          const bool isConstructor = func->memberClassInfo != nullptr && func->Name().has_value() &&
                                     *func->Name() == func->memberClassInfo->name;
          const bool isDestructor = func->memberClassInfo != nullptr && func->Name().has_value() &&
                                    *func->Name() == "~" + func->memberClassInfo->name;
          std::string signature;
          if (func->memberClassInfo != nullptr)
          {
               if (func->isStatic) signature += "static ";
               signature += func->memberClassInfo->DisplayName() + "::";
          }
          signature += func->Name().value_or("__unknown") + "(";
          bool first = true;
          for (const auto& param : func->parameters)
          {
               if (!first) signature += ", ";
               first = false;
               signature += FormatTypeName(param.type);
               if (!param.name.empty() && param.name != "<unnamed>") signature += " " + param.name;
          }
          signature += ")";
          if (func->memberClassInfo != nullptr && !func->isStatic)
          {
               if (func->memberFunctionInfo.isConst) signature += " const";
               if (func->memberFunctionInfo.isVolatile) signature += " volatile";
               switch (func->memberFunctionInfo.refQualifier)
               {
               case ecpps::abi::MemberRefQualifier::None: break;
               case ecpps::abi::MemberRefQualifier::LValue: signature += " &"; break;
               case ecpps::abi::MemberRefQualifier::RValue: signature += " &&"; break;
               }
          }
          if (func->returnType && !isConstructor && !isDestructor)
          {
               signature += " -> " + FormatTypeName(func->returnType);
          }
          return signature;
     }
     [[nodiscard]] bool IsIndirectLValue(const ecpps::Expression& expression)
     {
          return expression != nullptr &&
                 dynamic_cast<const ecpps::ir::high::DereferenceNode*>(expression->Value().get()) != nullptr;
     }

     std::size_t LevenshteinDistance(std::string_view string1, std::string_view string2,
                                     const std::size_t maximumDistance)
     {
          const std::size_t lengthDifference =
               string1.size() > string2.size() ? string1.size() - string2.size() : string2.size() - string1.size();
          if (lengthDifference > maximumDistance) return maximumDistance + 1;

          while (!string1.empty() && !string2.empty() && string1.front() == string2.front())
          {
               string1.remove_prefix(1);
               string2.remove_prefix(1);
          }

          while (!string1.empty() && !string2.empty() && string1.back() == string2.back())
          {
               string1.remove_suffix(1);
               string2.remove_suffix(1);
          }

          if (string1.size() < string2.size()) std::swap(string1, string2);

          const std::size_t rowCount = string1.size();
          const std::size_t columnCount = string2.size();
          if (rowCount - columnCount > maximumDistance) return maximumDistance + 1;
          if (rowCount == 0) return 0;

          const std::size_t overLimit = maximumDistance + 1;
          const std::size_t cap = (maximumDistance * 2) + 2;
          const std::size_t width = columnCount + 1;

          std::vector<std::size_t> rows(width * 3, cap);
          auto* previousPrevious = rows.data();
          auto* previous = previousPrevious + width;
          auto* current = previous + width;

          for (std::size_t column = 0; column <= columnCount && column <= maximumDistance; column++)
               previous[column] = column * 2;

          const auto addCapped = [cap](const std::size_t value, const std::size_t cost)
          {
               return value >= cap - cost ? cap : value + cost;
          };
          const auto lower = [](const char value)
          {
               return std::tolower(static_cast<unsigned char>(value));
          };

          for (std::size_t row = 1; row <= rowCount; row++)
          {
               const std::size_t firstColumn = row > maximumDistance ? row - maximumDistance : 0;
               const std::size_t lastColumn =
                    row >= columnCount || maximumDistance >= columnCount - row ? columnCount : row + maximumDistance;
               if (firstColumn != 0) current[firstColumn - 1] = cap;

               for (std::size_t column = firstColumn; column <= lastColumn; column++)
               {
                    if (column == 0)
                    {
                         current[column] = addCapped(previous[column], 2);
                         continue;
                    }

                    const char left = string1[row - 1];
                    const char right = string2[column - 1];
                    const int foldedLeft = lower(left);
                    const int foldedRight = lower(right);
                    const std::size_t substitutionCost = left == right ? 0 : foldedLeft == foldedRight ? 1 : 2;

                    auto distance = std::min(addCapped(previous[column], 2), addCapped(current[column - 1], 2));
                    distance = std::min(distance, addCapped(previous[column - 1], substitutionCost));

                    if (row > 1 && column > 1 && foldedLeft == lower(string2[column - 2]) &&
                        lower(string1[row - 2]) == foldedRight)
                         distance = std::min(distance, addCapped(previousPrevious[column - 2], 2));

                    current[column] = distance;
               }

               auto* const reused = previousPrevious;
               previousPrevious = previous;
               previous = current;
               current = reused;
          }

          return previous[columnCount] >= cap ? overLimit : previous[columnCount] / 2;
     }

     std::uint64_t ConvertConstant(std::uint64_t value, const ecpps::typeSystem::IntegralType* from,
                                   const ecpps::typeSystem::IntegralType* to)
     {
          const auto fromBits = static_cast<unsigned>(from->Size() * 8); // assumes Size() is bytes
          if (fromBits < 64)
          {
               const std::uint64_t mask = (1ull << fromBits) - 1;
               value &= mask;
               if (from->Sign() == ecpps::typeSystem::Signedness::Signed && ((value >> (fromBits - 1)) & 1))
                    value |= ~mask;
          }
          const auto toBits = static_cast<unsigned>(to->Size() * 8);
          return toBits < 64 ? value & ((1ull << toBits) - 1) : value;
     }

     std::vector<std::shared_ptr<ecpps::ir::FunctionScope>> CollectExactMatches(
          const std::string& name, std::deque<ecpps::ir::ContextPointer>& contextSequence)
     {
          std::vector<std::shared_ptr<ecpps::ir::FunctionScope>> exactMatches;
          for (const auto& context : contextSequence)
               for (const auto& func : context->GetScope().functions)
                    if (func->Name() == name) exactMatches.push_back(func);

          return exactMatches;
     }

     std::vector<std::pair<std::string, std::size_t>> CollectSimilarIdentifierNames(
          const std::string& name, std::deque<ecpps::ir::ContextPointer>& contextSequence)
     {
          std::vector<std::pair<std::string, std::size_t>> similar;
          std::set<std::string> seen;

          for (const auto& context : contextSequence)
          {
               const auto& scope = context->GetScope();
               if (const auto* const functionScope = dynamic_cast<const ecpps::ir::FunctionScope*>(&scope))
               {
                    for (const auto& local : functionScope->Locals())
                    {
                         const auto localName = local.Name();
                         if (seen.contains(localName)) continue;
                         seen.insert(localName);

                         const std::size_t maximumDistance = std::max<std::size_t>(3, localName.length() / 3);
                         const auto distance = LevenshteinDistance(name, localName, maximumDistance);
                         if (distance > 0 && distance <= maximumDistance) similar.emplace_back(localName, distance);
                    }
               }

               for (const auto& func : scope.functions)
               {
                    runtime_assert(func != nullptr, "Function pointer cannot be null");
                    runtime_assert(func->Name().has_value(), "Function must have a name");
                    const auto& functionName = func->Name().value(); // NOLINT
                    if (seen.contains(functionName)) continue;
                    seen.insert(functionName);

                    const std::size_t maximumDistance = std::max<std::size_t>(3, functionName.length() / 3);
                    const auto distance = LevenshteinDistance(name, functionName, maximumDistance);
                    if (distance > 0 && distance <= maximumDistance) similar.emplace_back(functionName, distance);
               }
          }

          std::ranges::sort(similar,
                            [](const auto& a, const auto& b)
                            {
                                 return a.second < b.second;
                            });

          return similar;
     }

     CandidateFailureInfo AnalyseCandidateFailure(const std::shared_ptr<ecpps::ir::FunctionScope>& function,
                                                  const std::vector<ecpps::Expression>& arguments)
     {
          CandidateFailureInfo info{.function = function, .parameterFailures = {}};

          if (arguments.size() != function->parameters.size())
          {
               using std::string_view_literals::operator""sv;
               info.parameterFailures.emplace_back(
                    static_cast<std::size_t>(-1),
                    std::format("Expected {} argument{}, got {}", function->parameters.size(),
                                function->parameters.size() == 1 ? ""sv : "s"sv, arguments.size()));
          }

          const std::size_t matchedCount = std::min(arguments.size(), function->parameters.size());
          for (std::size_t index = 0; index < matchedCount; index++)
          {
               const auto& argument = arguments[index];
               const auto& parameter = function->parameters[index];
               const std::size_t argumentIndex = index + 1;
               if (argument == nullptr)
               {
                    info.parameterFailures.emplace_back(
                         argumentIndex,
                         std::format("Argument {} cannot be checked because its expression is invalid", argumentIndex));
                    continue;
               }

               const auto& fromType = argument->Type();
               const auto& toType = parameter.type;

               ecpps::typeSystem::ConversionSequence seq = toType->CompareTo(fromType);

               if (!seq.IsValid())
               {
                    std::string reason =
                         std::format("Argument {} of type '{}' cannot convert to parameter '{}' of type '{}'",
                                     argumentIndex, FormatTypeName(fromType),
                                     parameter.name.empty() ? "<unnamed>" : parameter.name, FormatTypeName(toType));
                    info.parameterFailures.emplace_back(argumentIndex, std::move(reason));
               }
          }

          for (std::size_t index = matchedCount; index < arguments.size(); index++)
               info.parameterFailures.emplace_back(
                    index + 1, std::format("Argument {} has no corresponding parameter", index + 1));

          for (std::size_t index = matchedCount; index < function->parameters.size(); index++)
          {
               const auto& parameter = function->parameters[index];
               info.parameterFailures.emplace_back(
                    index + 1,
                    std::format("Missing argument {} for parameter '{}' of type '{}'", index + 1,
                                parameter.name.empty() ? "<unnamed>" : parameter.name, FormatTypeName(parameter.type)));
          }

          return info;
     }

     bool DeclaresNonType(const ecpps::ir::Scope& scope, const std::string& name)
     {
          for (const auto& func : scope.functions)
               if (func->Name() == name) return true;
          if (const auto* fn = dynamic_cast<const ecpps::ir::FunctionScope*>(&scope))
               for (const auto& local : fn->Locals())
                    if (local.Name() == name) return true;
          return false;
     }

     ecpps::typeSystem::ClassKind ToClassKind(const ecpps::ast::ClassKey key)
     {
          switch (key)
          {
          case ecpps::ast::ClassKey::Class: return ecpps::typeSystem::ClassKind::Class;
          case ecpps::ast::ClassKey::Struct: return ecpps::typeSystem::ClassKind::Struct;
          case ecpps::ast::ClassKey::Union: return ecpps::typeSystem::ClassKind::Union;
          }
          return ecpps::typeSystem::ClassKind::Struct;
     }

     ecpps::typeSystem::Access ToAccess(const ecpps::ast::AccessSpecifier spec)
     {
          switch (spec)
          {
          case ecpps::ast::AccessSpecifier::Public: return ecpps::typeSystem::Access::Public;
          case ecpps::ast::AccessSpecifier::Protected: return ecpps::typeSystem::Access::Protected;
          case ecpps::ast::AccessSpecifier::Private: return ecpps::typeSystem::Access::Private;
          }
          return ecpps::typeSystem::Access::Private;
     }

     ecpps::typeSystem::Qualifiers QualifiersFrom(const bool isConst, const bool isVolatile)
     {
          using ecpps::typeSystem::Qualifiers;
          if (isConst && isVolatile) return Qualifiers::ConstVolatile;
          return isConst ? Qualifiers::Const : isVolatile ? Qualifiers::Volatile : Qualifiers::None;
     }

     ecpps::ir::TypeRequest ErrorRequest(std::vector<ecpps::diagnostics::DiagnosticsMessage> diagnostics)
     {
          ecpps::ir::TypeRequest request{};
          request.kind = ecpps::ir::TypeKind::Error;
          request.data = ecpps::ir::InvalidRequest(nullptr, std::move(diagnostics));
          return request;
     }
     [[nodiscard]] bool IsValidIntegerSpecifierCombination(const std::vector<std::string_view>& tokens)
     {
          const auto count = [&](const std::string_view keyword)
          {
               return std::ranges::count(tokens, keyword);
          };
          const auto chars = count("char");
          const auto shorts = count("short");
          const auto ints = count("int");
          const auto longs = count("long");
          const auto isSigned = count("signed");
          const auto isUnsigned = count("unsigned");
          if (chars > 1 || shorts > 1 || ints > 1 || isSigned > 1 || isUnsigned > 1 || longs > 2) return false;
          if (isSigned != 0 && isUnsigned != 0) return false;
          if (chars != 0 && (shorts != 0 || ints != 0 || longs != 0)) return false;
          if (shorts != 0 && longs != 0) return false;
          return true;
     }

     Derivation LastDerivation(const ecpps::ast::Declarator& declarator)
     {
          if (declarator.nested != nullptr)
               if (const auto inner = LastDerivation(*declarator.nested); inner != Derivation::None) return inner;
          if (!declarator.suffixes.empty())
               return std::holds_alternative<ecpps::ast::ArrayBound>(declarator.suffixes.front())
                           ? Derivation::Array
                           : Derivation::Function;
          if (!declarator.ptrOperators.empty())
               return declarator.ptrOperators.back().kind == ecpps::ast::PtrOperator::Kind::Pointer ||
                                declarator.ptrOperators.back().kind == ecpps::ast::PtrOperator::Kind::PointerToMember
                           ? Derivation::Pointer
                           : Derivation::Reference;
          return Derivation::None;
     }
     const ecpps::ast::IdentifierNode* DeclaratorIdentifier(const ecpps::ast::Declarator& declarator)
     {
          const auto* level = &declarator;
          while (level->nested != nullptr) level = level->nested.get();
          return dynamic_cast<const ecpps::ast::IdentifierNode*>(level->declaredIdExpression.get());
     }

} // namespace ecpps::ir::detail

ecpps::ir::MemoryAccess ecpps::ir::detail::AccessForStorage(const typeSystem::NonowningTypePointer type)
{
     if (const auto* reference = type->CastTo<typeSystem::ReferenceType>())
     {
          TypeRequest request{};
          request.kind = TypeKind::Compound;
          request.data = PointerRequest{.elementType = reference->BaseType()};
          return AccessFor(GetTypeContext().Get(request));
     }
     return AccessFor(type);
}
