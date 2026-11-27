#include "Mangling.h"
#include <array>
#include <ranges>
#include <string>
#include "../TypeSystem/ArithmeticTypes.h"
#include "Shared/Diagnostics.h"
#include "TypeSystem/ClassType.h"
#include "TypeSystem/CompoundTypes.h"
#include "TypeSystem/TypeBase.h"

// calling conventions
constexpr std::string_view CallingConventionMsCall = "M";

// linkage
constexpr std::string_view InternalLinkage = "i";
constexpr std::string_view ExternalLinkage = "e";
constexpr std::string_view ModuleLinkage = "n";

// scalar types
constexpr std::string_view SignedByte = "s";
constexpr std::string_view UnsignedByte = "b";
constexpr std::string_view SignedWord = "S";
constexpr std::string_view UnsignedWord = "w";
constexpr std::string_view SignedInt32 = "i";   // NOLINT
constexpr std::string_view UnsignedInt32 = "u"; // NOLINT
constexpr std::string_view SignedDword = "l";
constexpr std::string_view UnsignedDword = "d";
constexpr std::string_view SignedQword = "L";
constexpr std::string_view UnsignedQword = "q";
constexpr std::string_view SignedXmmWord = "X";
constexpr std::string_view UnsignedXmmWord = "x";
constexpr std::string_view SignedYmmWord = "Y";
constexpr std::string_view UnsignedYmmWord = "y";
constexpr std::string_view SignedZmmWord = "Z";
constexpr std::string_view UnsignedZmmWord = "z";
constexpr std::string_view BooleanType = "B";
constexpr std::string_view PointerPrefix = "P";
constexpr std::string_view LValueReferencePrefix = "R";
constexpr std::string_view RValueReferencePrefix = "O";
constexpr std::string_view ArrayPrefix = "A";
constexpr std::string_view ClassPrefix = "C";
constexpr std::string_view ConstQualifier = "K";
constexpr std::string_view VolatileQualifier = "V";

static std::string MangleIdentifier(const std::string_view identifier)
{
     const auto isStart = [](const unsigned char value)
     {
          return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
     };
     const auto isContinue = [&](const unsigned char value)
     {
          return isStart(value) || (value >= '0' && value <= '9');
     };
     const bool plainIdentifier = !identifier.empty() && identifier.front() != 'Q' &&
                                  isStart(static_cast<unsigned char>(identifier.front())) &&
                                  std::ranges::all_of(identifier.substr(1),
                                                      [&](const char value)
                                                      {
                                                           return isContinue(static_cast<unsigned char>(value));
                                                      });
     if (plainIdentifier) return std::string{identifier};

     constexpr std::array HexDigits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
     std::string result = std::format("Q{}_", identifier.size());
     result.reserve(result.size() + (identifier.size() * 2));
     for (const char character : identifier)
     {
          const auto value = static_cast<unsigned char>(character);
          const auto unsignedValue = static_cast<std::uint32_t>(value);
          result.push_back(HexDigits[unsignedValue >> 4u]);
          result.push_back(HexDigits[unsignedValue & 0x0Fu]);
     }
     return result;
}

std::string MangleTypeImpl(ecpps::typeSystem::NonowningTypePointer type, bool stripTopLevelCv);
static std::string MangleParameter(const ecpps::typeSystem::NonowningTypePointer type)
{
     if (const auto* array = type->CastTo<ecpps::typeSystem::ArrayType>())
          return std::string{PointerPrefix} + MangleTypeImpl(array->ElementType(), false);
     return MangleTypeImpl(type, true);
}
static std::string MangleArithmetic(const ecpps::typeSystem::NonowningTypePointer type)
{
     if (const auto* integralType = type->CastTo<ecpps::typeSystem::IntegralType>())
     {
          switch (integralType->Kind())
          {
          case ecpps::typeSystem::TypeKind::Char:
               return std::string{integralType->Sign() == ecpps::typeSystem::Signedness::Signed ? SignedByte
                                                                                                : UnsignedByte};
          case ecpps::typeSystem::TypeKind::Short:
               return std::string{integralType->Sign() == ecpps::typeSystem::Signedness::Signed ? SignedWord
                                                                                                : UnsignedWord};
          case ecpps::typeSystem::TypeKind::Int:
               return std::string{integralType->Sign() == ecpps::typeSystem::Signedness::Signed ? SignedInt32
                                                                                                : UnsignedInt32};
          case ecpps::typeSystem::TypeKind::Long:
               return std::string{integralType->Sign() == ecpps::typeSystem::Signedness::Signed ? SignedDword
                                                                                                : UnsignedDword};
          case ecpps::typeSystem::TypeKind::LongLong:
               return std::string{integralType->Sign() == ecpps::typeSystem::Signedness::Signed ? SignedQword
                                                                                                : UnsignedQword};
          default:
               switch (integralType->Size())
               {
               case 16:
                    return std::string{integralType->Sign() == ecpps::typeSystem::Signedness::Signed ? SignedXmmWord
                                                                                                     : UnsignedXmmWord};
               case 32:
                    return std::string{integralType->Sign() == ecpps::typeSystem::Signedness::Signed ? SignedYmmWord
                                                                                                     : UnsignedYmmWord};
               case 64:
                    return std::string{integralType->Sign() == ecpps::typeSystem::Signedness::Signed ? SignedZmmWord
                                                                                                     : UnsignedZmmWord};
               }
               break;
          }
          return "_";
     }
     return "_";
}
std::string MangleTypeImpl(const ecpps::typeSystem::NonowningTypePointer type, const bool stripTopLevelCv)
{
     runtime_assert(type != nullptr, "Cannot mangle a null type");
     if (type == nullptr) return "__";
     using namespace ecpps;

     std::string result;
     if (!stripTopLevelCv)
          if (const auto* qualified = type->CastTo<typeSystem::QualifiedType>())
          {
               if (qualified->IsConst()) result += ConstQualifier;
               if (qualified->IsVolatile()) result += VolatileQualifier;
          }

     if (const auto* classType = type->CastTo<typeSystem::ClassType>())
          return result + abi::Mangling::MangleClassPath(classType->Info());
     if (const auto* pointer = type->CastTo<typeSystem::PointerType>())
          return result + std::string{PointerPrefix} + MangleTypeImpl(pointer->BaseType(), false);
     if (const auto* array = type->CastTo<typeSystem::ArrayType>())
          return result + std::format("{}{}_", ArrayPrefix, array->ElementCount()) +
                 MangleTypeImpl(array->ElementType(), false);
     if (const auto* reference = type->CastTo<typeSystem::ReferenceType>())
          return result +
                 std::string{reference->GetKind() == typeSystem::ReferenceType::Kind::LValue ? LValueReferencePrefix
                                                                                             : RValueReferencePrefix} +
                 MangleTypeImpl(reference->BaseType(), false);
     if (typeSystem::IsBoolean(type)) return result + std::string{BooleanType};
     if (typeSystem::IsArithmetic(type)) return result + MangleArithmetic(type);
     if (typeSystem::IsIncomplete(type)) return result + "v"; // void
     return result + "_";                                     // unknown
}

static std::string MangleMemberBlock(const ecpps::abi::MemberFunctionInfo& member)
{
     using ecpps::abi::MemberRefQualifier;
     std::string block = "m";
     if (member.isStatic)
     {
          runtime_assert(!member.isConst && !member.isVolatile && member.refQualifier == MemberRefQualifier::None,
                         "A static member function cannot have cv- or ref-qualifiers");
          block += 's';
     }
     else
     {
          if (member.isConst) block += ConstQualifier;
          if (member.isVolatile) block += VolatileQualifier;
          if (member.refQualifier == MemberRefQualifier::LValue) block += 'l';
          if (member.refQualifier == MemberRefQualifier::RValue) block += 'r';
     }
     return block + ";";
}

static std::string BuildName(const ecpps::abi::Linkage linkage, const std::string& name,
                             const ecpps::abi::CallingConventionName callingConvention,
                             const ecpps::typeSystem::NonowningTypePointer returnType,
                             const std::vector<ecpps::typeSystem::NonowningTypePointer>& parameters,
                             const std::vector<ecpps::typeSystem::ScopeSegment>& scope,
                             const ecpps::abi::MemberFunctionInfo* member)
{
     using ecpps::abi::Linkage;
     std::string built = "?";
     switch (linkage)
     {
     case Linkage::NoLinkage: return ""; // error?
     case Linkage::CLinkage:
          if (member != nullptr) throw ecpps::TracedException("A member function cannot have C language linkage");
          return name;
     case Linkage::Internal: built += InternalLinkage; break;
     case Linkage::External: built += ExternalLinkage; break;
     case Linkage::Module: built += ModuleLinkage; break;
     }
     if (callingConvention == ecpps::abi::CallingConventionName::Microsoftx64) built += CallingConventionMsCall;

     if (member != nullptr) built += MangleMemberBlock(*member);

     for (const auto& segment : scope)
     {
          switch (segment.kind)
          {
          case ecpps::typeSystem::ScopeKind::Namespace: built += MangleIdentifier(segment.name) + "??"; break;
          case ecpps::typeSystem::ScopeKind::Class: built += MangleIdentifier(segment.name) + "?C"; break;
          case ecpps::typeSystem::ScopeKind::Function: built += MangleIdentifier(segment.name) + "?F"; break;
          }
     }

     built += returnType == nullptr ? std::string{"v"} : MangleTypeImpl(returnType, true);
     built += '@';
     built += MangleIdentifier(name);
     for (const auto& param : parameters) built += "$" + MangleParameter(param);
     if (parameters.empty()) built += "$v";

     return built + "?";
}

std::string ecpps::abi::Mangling::MangleType(typeSystem::NonowningTypePointer type)
{
     return MangleTypeImpl(type, false);
}

std::string ecpps::abi::Mangling::MangleName(const Linkage linkage, const std::string& name,
                                             const CallingConventionName callingConvention,
                                             const typeSystem::NonowningTypePointer returnType,
                                             const std::vector<typeSystem::NonowningTypePointer>& parameters,
                                             const std::vector<std::string>& namespacePath)
{
     std::vector<typeSystem::ScopeSegment> scope;
     scope.reserve(namespacePath.size());
     for (const auto& ns : namespacePath) scope.push_back({.kind = typeSystem::ScopeKind::Namespace, .name = ns});
     return BuildName(linkage, name, callingConvention, returnType, parameters, scope, nullptr);
}

std::string ecpps::abi::Mangling::MangleName(const Linkage linkage, const std::string& name,
                                             const CallingConventionName callingConvention,
                                             const typeSystem::NonowningTypePointer returnType,
                                             const std::vector<typeSystem::NonowningTypePointer>& parameters,
                                             const std::vector<typeSystem::ScopeSegment>& scope)
{
     return BuildName(linkage, name, callingConvention, returnType, parameters, scope, nullptr);
}
std::string ecpps::abi::Mangling::MangleClassPath(const typeSystem::ClassInfo& info)
{
     std::string path;
     for (const auto& segment : info.enclosingPath)
     {
          path += MangleIdentifier(segment.name);
          path += segment.kind == typeSystem::ScopeKind::Function ? "?F:"
                  : segment.kind == typeSystem::ScopeKind::Class  ? "?C:"
                                                                  : "??:";
     }
     std::string className = info.name;
     if (className.empty())
          className =
               std::format("<unnamed {} {} at {}:{}>", info.Keyword(), info.id, info.source.line, info.source.position);
     if (info.isLocal && std::ranges::none_of(info.enclosingPath,
                                              [](const auto& segment)
                                              {
                                                   return segment.kind == typeSystem::ScopeKind::Function;
                                              }))
          path += MangleIdentifier("<local class>") + ":";
     if (info.instantiationKey.has_value()) className += "<template arguments:" + *info.instantiationKey + ">";
     return std::string{ClassPrefix} + path + MangleIdentifier(className);
}

std::string ecpps::abi::Mangling::MangleMemberName(const Linkage linkage, const std::string& name,
                                                   const CallingConventionName callingConvention,
                                                   const typeSystem::NonowningTypePointer returnType,
                                                   const std::vector<typeSystem::NonowningTypePointer>& parameters,
                                                   const std::vector<typeSystem::ScopeSegment>& scope,
                                                   const MemberFunctionInfo& member)
{
     runtime_assert(!scope.empty() && scope.back().kind == typeSystem::ScopeKind::Class,
                    "A member function's scope must end with its class");
     return BuildName(linkage, name, callingConvention, returnType, parameters, scope, &member);
}
