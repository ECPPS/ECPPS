#include "Mangling.h"
#include <string>
#include "../TypeSystem/ArithmeticTypes.h"

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
constexpr std::string_view SignedInt32 = "i";
constexpr std::string_view UnsignedInt32 = "u";
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

std::string ecpps::abi::Mangling::MangleType(const typeSystem::TypePointer& type)
{
     if (typeSystem::IsArithmetic(type))
     {
          if (const auto integralType = std::dynamic_pointer_cast<typeSystem::IntegralType>(type))
          {
               switch (integralType->Size())
               {
               case 1:
                    return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedByte
                                                                                              : UnsignedByte};
               case 2:
                    return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedWord
                                                                                              : UnsignedWord};
               case 4:
                    return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedDword
                                                                                              : UnsignedDword};
               case 8:
                    return std::string{integralType->Sign() == typeSystem::Signedness::Signed ? SignedQword
                                                                                              : UnsignedQword};
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
               return "_";
          }
          if (const auto floatingType = std::dynamic_pointer_cast<typeSystem::IntegralType>(type))
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
                                             const typeSystem::TypePointer& returnType,
                                             const std::vector<typeSystem::TypePointer>& parameters)
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
     built += MangleType(returnType);
     built += "@";
     built += name;
     for (const auto& param : parameters) built += "$" + MangleType(param);
     if (parameters.empty()) built += "$v";

     return built + "?";
}