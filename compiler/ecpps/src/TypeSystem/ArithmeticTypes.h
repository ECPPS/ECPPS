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
               return TypeTraits{TypeTraitEnum::Arithmetic,
                                 TypeTraitEnum::Integral,
                                 TypeTraitEnum::Literal,
                                 TypeTraitEnum::TriviallyCopyable,
                                 TypeTraitEnum::ImplicitLifetime,
                                 TypeTraitEnum::Scalar,
                                 TypeTraitEnum::Object};
          }

          [[nodiscard]] ConversionSequence CompareTo(const std::shared_ptr<TypeBase>& other) override;
          [[nodiscard]] TypeKind Kind(void) const noexcept { return this->_kind; }

          [[nodiscard]] std::shared_ptr<TypeBase> CommonWith(const std::shared_ptr<TypeBase>& other) final;

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
          [[nodiscard]] ConversionSequence CompareTo(const std::shared_ptr<TypeBase>& other) final;

     private:
          bool _isUnqualified;
     };

     class PointerType final : public QualifiedType
     {
     public:
          explicit PointerType(std::shared_ptr<TypeBase> baseType, std::string name, const Qualifiers qualifiers)
              : _baseType(std::move(baseType)), QualifiedType(std::move(name), qualifiers)
          {
          }

          [[nodiscard]] std::shared_ptr<TypeBase> BaseType(void) const noexcept { return this->_baseType; }

          [[nodiscard]] std::size_t Size(void) const noexcept override;

          [[nodiscard]] std::size_t Alignment(void) const noexcept override;

          [[nodiscard]] std::string RawName(void) const noexcept override { return this->_baseType->RawName() + "*"; }

          [[nodiscard]] ConversionSequence CompareTo(const std::shared_ptr<TypeBase>& other) override;

          [[nodiscard]] TypeTraits Traits(void) const noexcept final;
          [[nodiscard]] std::shared_ptr<TypeBase> CommonWith(const std::shared_ptr<TypeBase>& other) final;

     private:
          std::shared_ptr<TypeBase> _baseType;
     };

     class ReferenceType final : public TypeBase
     {
     public:
          enum class Kind
          {
               LValue,
               RValue
          };

          explicit ReferenceType(std::shared_ptr<TypeBase> baseType, Kind kind, std::string name)
              : _baseType(std::move(baseType)), _kind(kind), TypeBase(std::move(name))
          {
          }

          [[nodiscard]] std::shared_ptr<TypeBase> BaseType(void) const noexcept { return this->_baseType; }
          [[nodiscard]] Kind GetKind(void) const noexcept { return this->_kind; }

          [[nodiscard]] std::size_t Size(void) const noexcept override;
          [[nodiscard]] std::size_t Alignment(void) const noexcept override;

          [[nodiscard]] std::string RawName(void) const noexcept override
          {
               return this->_baseType->RawName() + (_kind == Kind::LValue ? "&" : "&&");
          }

          [[nodiscard]] ConversionSequence CompareTo(const std::shared_ptr<TypeBase>& other) override;
          [[nodiscard]] TypeTraits Traits(void) const noexcept final;
          [[nodiscard]] std::shared_ptr<TypeBase> CommonWith(const std::shared_ptr<TypeBase>& other) final;

     private:
          std::shared_ptr<TypeBase> _baseType;
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

     IntegerConversionRank RankInteger(const std::shared_ptr<IntegralType>& integer);
     IntegerConversionRank RankInteger(const IntegralType& integer);
     std::shared_ptr<IntegralType> PromoteInteger(const std::shared_ptr<IntegralType>& integer);

     // predefined builtin types
     extern std::shared_ptr<VoidType> g_void;
     extern std::shared_ptr<CharacterType> g_char;
     extern std::shared_ptr<CharacterType> g_signedChar;
     extern std::shared_ptr<CharacterType> g_unsignedChar;
     extern std::shared_ptr<IntegralType> g_short;
     extern std::shared_ptr<IntegralType> g_unsignedShort;
     extern std::shared_ptr<IntegralType> g_int;
     extern std::shared_ptr<IntegralType> g_unsignedInt;
     extern std::shared_ptr<IntegralType> g_long;
     extern std::shared_ptr<IntegralType> g_unsignedLong;
     extern std::shared_ptr<IntegralType> g_longLong;
     extern std::shared_ptr<IntegralType> g_unsignedLongLong;
} // namespace ecpps::typeSystem