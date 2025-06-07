#pragma once
#include "TypeBase.h"
#include <memory>

namespace ecpps::typeSystem
{
     enum struct Signedness : bool
     {
          Signed = true,
          Unsigned = false
     };

     constexpr Signedness DefaultCharacterSign = Signedness::Signed; // implementation-defined property

     enum struct TypeKind : std::uint_fast8_t
     {
          Char,
          Short,
          Int,
          Long,
          LongLong,
     };

     enum struct TypeSizes : std::size_t // implementation-defined property; also constitutes alignment in this case
     {
          Char = 1,
          Short = 2,
          Int = 4,
          Long = 4,
          LongLong = 8,
     };

     class IntegralType : public QualifiedType
     {
     public:
          explicit IntegralType(const Signedness sign, const TypeKind kind, std::string name,
                                const Qualifiers qualifiers)
              : _sign(sign), _kind(kind), QualifiedType(std::move(name), qualifiers)
          {
          }

          [[nodiscard]] Signedness Sign(void) const noexcept { return this->_sign; }
          [[nodiscard]] std::size_t Size(void) const noexcept final;
          [[nodiscard]] std::size_t Alignment(void) const noexcept final { return Size(); }

          [[nodiscard]] std::string RawName(void) const noexcept override;

          [[nodiscard]] TypeTraits Traits(void) const noexcept override
          {
               return TypeTraits{TypeTraitEnum::Arithmetic, TypeTraitEnum::Integral, TypeTraitEnum::Literal,
                                 TypeTraitEnum::TriviallyCopyable, TypeTraitEnum::ImplicitLifetime};
          }

     private:
          Signedness _sign;
          TypeKind _kind;
     };
     enum struct CharacterSign : std::uint_fast8_t
     {
          Char,
          SignedChar,
          UnsignedChar
     };
     class CharacterType final : public IntegralType
     {
     public:
          explicit CharacterType(const CharacterSign sign, std::string name, const Qualifiers qualifiers)
              : IntegralType(sign == CharacterSign::Char         ? DefaultCharacterSign
                             : sign == CharacterSign::SignedChar ? Signedness::Signed
                                                                 : Signedness::Unsigned,
                             TypeKind::Char, std::move(name), qualifiers),
                _isUnqualified(sign == CharacterSign::Char)
          {
          }
          [[nodiscard]] std::string RawName(void) const noexcept override;

     private:
          bool _isUnqualified;
     };

     // predefined builtin types
     extern std::shared_ptr<CharacterType> g_char;
     extern std::shared_ptr<CharacterType> g_signedChar;
     extern std::shared_ptr<CharacterType> g_unsignedChar;
     extern std::shared_ptr<IntegralType> g_short;
     extern std::shared_ptr<IntegralType> g_int;
     extern std::shared_ptr<IntegralType> g_long;
     extern std::shared_ptr<IntegralType> g_longLong;
} // namespace ecpps::typeSystem