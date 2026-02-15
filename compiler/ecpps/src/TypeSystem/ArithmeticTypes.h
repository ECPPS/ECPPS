#pragma once
#include <memory>
#include "TypeBase.h"

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

     enum struct TypeSizes : std::uint_fast8_t // implementation-defined property; also constitutes alignment in this
                                               // case
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
              : QualifiedType(std::move(name), qualifiers), _sign(sign), _kind(kind)
          {
          }

          [[nodiscard]] Signedness Sign(void) const noexcept { return this->_sign; }
          [[nodiscard]] std::size_t Size(void) const noexcept final;
          [[nodiscard]] std::size_t Alignment(void) const noexcept final { return Size(); }

          [[nodiscard]] std::string RawName(void) const override;

          [[nodiscard]] TypeTraits Traits(void) const noexcept override
          {
               return TypeTraits{TypeTraitEnum::Arithmetic,
                                 TypeTraitEnum::Integral,
                                 TypeTraitEnum::Literal,
                                 TypeTraitEnum::TriviallyCopyable,
                                 TypeTraitEnum::ImplicitLifetime,
                                 TypeTraitEnum::Scalar,
                                 TypeTraitEnum::Object};
          }

          [[nodiscard]] ConversionSequence CompareTo(ecpps::typeSystem::NonowningTypePointer other) const override;
          [[nodiscard]] TypeKind Kind(void) const noexcept { return this->_kind; }

          [[nodiscard]] NonowningTypePointer CommonWith(ecpps::typeSystem::NonowningTypePointer other) const final;

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
          [[nodiscard]] std::string RawName(void) const noexcept final;
          [[nodiscard]] ConversionSequence CompareTo(NonowningTypePointer other) const final;

          [[nodiscard]] TypeTraits Traits(void) const noexcept override
          {
               return TypeTraits{TypeTraitEnum::Arithmetic,       TypeTraitEnum::Integral,
                                 TypeTraitEnum::Literal,          TypeTraitEnum::TriviallyCopyable,
                                 TypeTraitEnum::ImplicitLifetime, TypeTraitEnum::Scalar,
                                 TypeTraitEnum::Character,        TypeTraitEnum::Object};
          }

     private:
          bool _isUnqualified;
     };

     class PointerType final : public QualifiedType
     {
     public:
          explicit PointerType(NonowningTypePointer baseType, std::string name, const Qualifiers qualifiers)
              : QualifiedType(std::move(name), qualifiers), _baseType(baseType)
          {
          }

          [[nodiscard]] NonowningTypePointer BaseType(void) const noexcept { return this->_baseType; }

          [[nodiscard]] std::size_t Size(void) const noexcept override;

          [[nodiscard]] std::size_t Alignment(void) const noexcept override;

          [[nodiscard]] std::string RawName(void) const override { return this->_baseType->RawName() + "*"; }

          [[nodiscard]] ConversionSequence CompareTo(NonowningTypePointer other) const override;

          [[nodiscard]] TypeTraits Traits(void) const noexcept final;
          [[nodiscard]] NonowningTypePointer CommonWith(NonowningTypePointer other) const final;

     private:
          NonowningTypePointer _baseType;
     };

     class ReferenceType final : public TypeBase
     {
     public:
          enum class Kind : std::uint_fast8_t
          {
               LValue,
               RValue
          };

          explicit ReferenceType(NonowningTypePointer baseType, Kind kind, std::string name)
              : TypeBase(std::move(name)), _baseType(baseType), _kind(kind)
          {
          }

          [[nodiscard]] NonowningTypePointer BaseType(void) const noexcept { return this->_baseType; }
          [[nodiscard]] Kind GetKind(void) const noexcept { return this->_kind; }

          [[nodiscard]] std::size_t Size(void) const noexcept override;
          [[nodiscard]] std::size_t Alignment(void) const noexcept override;

          [[nodiscard]] std::string RawName(void) const override
          {
               return this->_baseType->RawName() + (_kind == Kind::LValue ? "&" : "&&");
          }

          [[nodiscard]] ConversionSequence CompareTo(NonowningTypePointer other) const override;
          [[nodiscard]] TypeTraits Traits(void) const noexcept final;
          [[nodiscard]] NonowningTypePointer CommonWith(NonowningTypePointer other) const final;

     private:
          NonowningTypePointer _baseType;
          Kind _kind;
     };

     /// <summary>
     /// Important ones:
     /// No two signed integer types other than char and signed char (if char is signed) have the same rank, even if
     /// they have the same representation. The rank of a signed integer type is greater than the rank of any signed
     /// integer type with a smaller width. The rank of any unsigned integer type equals the rank of the corresponding
     /// signed integer type.
     ///
     /// [conv.rank]/1 "Every integer type has an integer conversion rank defined as follows:"
     /// </summary>
     enum struct IntegerConversionRank : std::uint_fast8_t
     {
          Unknown = static_cast<std::uint_fast8_t>(~0),

          Bool = 0, // lowest
          Char = 1,
          SignedChar = Char,
          UnsignedChar = Char,
          Short = 2,
          UnsignedShort = 2,
          Int = 3,
          UnsignedInt = 3,
          Long = 4,
          UnsignedLong = 4,
          LongLong = 5,
          UnsignedLongLong = 5, // highest
     };
     [[nodiscard]] constexpr auto operator<=>(const IntegerConversionRank left, const IntegerConversionRank right)
     {
          return std::to_underlying(left) <=> std::to_underlying(right);
     }

     IntegerConversionRank RankInteger(const IntegralType* integer);
     IntegerConversionRank RankInteger(const IntegralType& integer);
     const IntegralType* PromoteInteger(const IntegralType* integer);

     // predefined builtin types
     extern std::unique_ptr<VoidType> g_void;
     extern std::unique_ptr<CharacterType> g_char;
     extern std::unique_ptr<CharacterType> g_signedChar;
     extern std::unique_ptr<CharacterType> g_unsignedChar;
     extern std::unique_ptr<IntegralType> g_short;
     extern std::unique_ptr<IntegralType> g_unsignedShort;
     extern std::unique_ptr<IntegralType> g_int;
     extern std::unique_ptr<IntegralType> g_unsignedInt;
     extern std::unique_ptr<IntegralType> g_long;
     extern std::unique_ptr<IntegralType> g_unsignedLong;
     extern std::unique_ptr<IntegralType> g_longLong;
     extern std::unique_ptr<IntegralType> g_unsignedLongLong;

     // commonly used types
     extern std::unique_ptr<CharacterType> g_constChar;
} // namespace ecpps::typeSystem
