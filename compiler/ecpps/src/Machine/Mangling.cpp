#include "Mangling.h"
#include <string>
#include "../TypeSystem/ArithmeticTypes.h"
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

// floating point
constexpr std::string_view Float32 = "f";
constexpr std::string_view Float64 = "F";

std::string ecpps::abi::Mangling::MangleType(typeSystem::NonowningTypePointer type)
{
     if (typeSystem::IsArithmetic(type))
     {
          if (const auto* integralType = type->CastTo<typeSystem::IntegralType>())
          {
               switch (integralType->Kind())
               {
               case ecpps::typeSystem::TypeKind::Char:
                    return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedByte
                                                                                              : UnsignedByte};
               case ecpps::typeSystem::TypeKind::Short:
                    return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedWord
                                                                                              : UnsignedWord};
               case ecpps::typeSystem::TypeKind::Int:
                    return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedInt32
                                                                                              : UnsignedInt32};
               case ecpps::typeSystem::TypeKind::Long:
                    return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedDword
                                                                                              : UnsignedDword};
               case ecpps::typeSystem::TypeKind::LongLong:
                    return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedQword
                                                                                              : UnsignedQword};
               default:
                    switch (integralType->Size())
                    {
                    case 16:
                         return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedXmmWord
                                                                                                   : UnsignedXmmWord};
                    case 32:
                         return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedYmmWord
                                                                                                   : UnsignedYmmWord};
                    case 64:
                         return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedZmmWord
                                                                                                   : UnsignedZmmWord};
                    }
                    break;
               }
               return "_";
          }
          if (const auto* floatingType = type->CastTo<typeSystem::IntegralType>()) // TODO: ???
          {
               switch (floatingType->Size())
               {
               case 4: return std::string{Float32};
               case 8: return std::string{Float64};
               }
               return "_";
          }
     }
     if (typeSystem::IsIncomplete(type)) return "v";

     return "_"; // unknown
}

std::string ecpps::abi::Mangling::MangleName(const Linkage linkage, const std::string& name,
                                             const CallingConventionName callingConvention,
                                             const typeSystem::NonowningTypePointer returnType,
                                             const std::vector<typeSystem::NonowningTypePointer>& parameters,
                                             const std::vector<std::string>& namespacePath)
{
     std::string built = "?";
     switch (linkage)
     {
     case Linkage::NoLinkage: return ""; // error?
     case Linkage::CLinkage: return name;

     case Linkage::Internal: built += InternalLinkage; break;
     case Linkage::External: built += ExternalLinkage; break;
     case Linkage::Module: built += ModuleLinkage; break;
     }
     switch (callingConvention)
     {
     case CallingConventionName::Microsoftx64: built += CallingConventionMsCall; break;
     }
     for (const auto& ns : namespacePath) built += ns + "??";
     built += MangleType(returnType);
     built += "@";
     built += name;
     for (const auto& param : parameters) built += "$" + MangleType(param);
     if (parameters.empty()) built += "$v";

     return built + "?";
}
