#include "ArithmeticTypes.h"
#include <string>
#include <utility>

std::size_t ecpps::typeSystem::IntegralType::Size(void) const noexcept
{
     switch (this->_kind)
     {
     case TypeKind::Char: return std::to_underlying(TypeSizes::Char);
     case TypeKind::Short: return std::to_underlying(TypeSizes::Short);
     case TypeKind::Int: return std::to_underlying(TypeSizes::Int);
     case TypeKind::Long: return std::to_underlying(TypeSizes::Long);
     case TypeKind::LongLong: return std::to_underlying(TypeSizes::LongLong);
     }

     return 0;
}
std::string ecpps::typeSystem::IntegralType::RawName(void) const noexcept
{
     switch (this->_kind)
     {
     case TypeKind::Char: return this->_sign == Signedness::Signed ? "signed char" : "unsigned char";
     case TypeKind::Short: return this->_sign == Signedness::Signed ? "short" : "unsigned short";
     case TypeKind::Int: return this->_sign == Signedness::Signed ? "int" : "unsigned int";
     case TypeKind::Long: return this->_sign == Signedness::Signed ? "long" : "unsigned long";
     case TypeKind::LongLong: return this->_sign == Signedness::Signed ? "long long" : "unsigned long long";
     }

     return "__integer";
}

std::string ecpps::typeSystem::CharacterType::RawName(void) const noexcept
{
     if (this->_isUnqualified) return "char";
     return this->Sign() == Signedness::Signed ? "signed char" : "unsigned char";
}

// predefined builtin types
std::shared_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_char =
    std::make_shared<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::Char, "char",
                                                ecpps::typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_signedChar =
    std::make_shared<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::SignedChar, "signed char",
                                                ecpps::typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_unsignedChar =
    std::make_shared<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::UnsignedChar, "unsigned char",
                                                ecpps::typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_short =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Signed, typeSystem::TypeKind::Short, "short",
                                               typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_int = std::make_shared<typeSystem::IntegralType>(
    typeSystem::Signedness::Signed, typeSystem::TypeKind::Int, "int", typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_long = std::make_shared<typeSystem::IntegralType>(
    typeSystem::Signedness::Signed, typeSystem::TypeKind::Long, "long", typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_longLong =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Signed, typeSystem::TypeKind::LongLong,
                                               "long long", typeSystem::Qualifiers::None);