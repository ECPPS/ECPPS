#pragma once
#include <SBOVector.h>
#include <bitset>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>

namespace ecpps::typeSystem
{
     constexpr std::size_t CharWidth = 8; // implementation-defined property

     enum struct TypeTraitEnum : std::uint_fast8_t
     {
          ImplicitLifetime,
          Arithmetic,
          Integral,
          FloatingPoint,
          Literal,
          TriviallyCopyable,
          Character,
          Incomplete,
          Boolean,
          Pointer,
          Scalar,
          Reference,
          Object,

          Count
     };

     struct ConversionSequence
     {
          enum struct ConversionKind : std::uint_fast8_t
          {
               LvalueToRValue = 16,
               ArrayToPointer = 15,
               FunctionToPointer = 14,
               Materialisation = 13,
               QualifierConversion = 12,
               IntegralPromotion = 11,
               FloatingPointPromotion = 10,
               IntegralConversion = 9,
               FloatingPointConversion = 8,
               FloatingIntegralConversion = 7,
               PointerConversion = 6,
               MemberFunctionConversion = 5,
               FunctionPointerConversion = 4,
               BooleanConversion = 3,
               UserDefined = 2,
               Ellipsis = 1 // Variadic function (worst match)
          };

          explicit ConversionSequence(std::optional<SBOVector<ConversionKind>> sequence)
              : _isValid(sequence.has_value()),
                _sequence(this->_isValid ? sequence.value() : SBOVector<ConversionKind>{})
          {
          }
          [[nodiscard]] bool IsValid(void) const noexcept { return this->_isValid; }
          [[nodiscard]] const SBOVector<ConversionKind>& Sequence(void) const noexcept { return this->_sequence; }
          [[nodiscard]] bool SameAs(void) const noexcept { return this->_isValid && this->_sequence.Size() == 0; }

     private:
          bool _isValid;
          SBOVector<ConversionKind> _sequence{};
     };

     struct TypeTraits
     {
          constexpr explicit TypeTraits(void) = default;
          constexpr explicit TypeTraits(std::initializer_list<TypeTraitEnum> traits)
          {
               for (const auto trait : traits) Set(trait);
          }
          [[nodiscard]] constexpr bool Has(TypeTraitEnum trait) const noexcept
          {
               return this->_traits.test(static_cast<std::size_t>(trait));
          }
          void Set(const TypeTraitEnum trait) { this->_traits.set(static_cast<std::size_t>(trait)); }
          void Remove(const TypeTraitEnum trait) { this->_traits.reset(static_cast<std::size_t>(trait)); }

     private:
          std::bitset<static_cast<std::size_t>(TypeTraitEnum::Count)> _traits{};
     };

     struct TypePointerHash;
     struct TypePointerEqual;

     /// <summary>
     /// Base type construct
     /// N4950/6.8.1 [basic.types.general]
     /// Note 1 : 6.8 and the subclauses thereof impose requirements on implementations regarding the representation of
     /// types. There are two kinds of types : fundamental types and compound types. Types describe objects(6.7.2),
     /// references(9.3.4.3), or functions(9.3.4.6).- end note]
     /// </summary>
     class TypeBase
     {
     public:
          virtual ~TypeBase(void) = default;
          explicit TypeBase(std::string name) : _name(std::move(name)) {}
          [[nodiscard]] const std::string& Name(void) const noexcept { return this->_name; }
          [[nodiscard]] virtual std::string RawName(void) const = 0;

          [[nodiscard]] virtual TypeTraits Traits(void) const noexcept = 0;

          /// <summary>
          /// Returns size in bytes. For references, returns the size of the object
          /// </summary>
          [[nodiscard]] virtual std::size_t Size(void) const noexcept = 0;
          /// <summary>
          /// Returns alignment in bytes. For references, returns the alignment of the object
          /// </summary>
          [[nodiscard]] virtual std::size_t Alignment(void) const noexcept = 0;

          [[nodiscard]] virtual ConversionSequence CompareTo(const std::shared_ptr<TypeBase>& other) = 0;
          /// <summary>
          /// Get the common type between 2 types. Returns nullptr if no common type is found.
          /// </summary>
          /// <param name="other"></param>
          /// <returns></returns>
          [[nodiscard]] virtual std::shared_ptr<TypeBase> CommonWith(const std::shared_ptr<TypeBase>& other) = 0;

     private:
          std::string _name;
          friend struct TypePointerHash;
          friend struct TypePointerEqual;
     };

#define TraitCheckerFunction(traitName)                                                                                \
     [[nodiscard]] constexpr bool Is##traitName(const std::shared_ptr<TypeBase>& type)                                 \
     {                                                                                                                 \
          return type->Traits().Has(TypeTraitEnum::traitName);                                                         \
     }

     TraitCheckerFunction(ImplicitLifetime);
     TraitCheckerFunction(Arithmetic);
     TraitCheckerFunction(Integral);
     TraitCheckerFunction(FloatingPoint);
     TraitCheckerFunction(Literal);
     TraitCheckerFunction(TriviallyCopyable);
     TraitCheckerFunction(Character);
     TraitCheckerFunction(Incomplete);
     TraitCheckerFunction(Boolean);
     TraitCheckerFunction(Pointer);

#undef TraitCheckerFunction
     using TypePointer = std::shared_ptr<TypeBase>;

     struct TypePointerHash
     {
          using is_transparent = void;

          std::size_t operator()(const TypePointer& ptr) const noexcept { return std::hash<std::string>{}(ptr->_name); }
          std::size_t operator()(const std::string& str) const noexcept { return std::hash<std::string>{}(str); }
     };
     struct TypePointerEqual
     {
          using is_transparent = void;

          bool operator()(const TypePointer& lhs, const TypePointer& rhs) const noexcept
          {
               if (lhs == rhs) return true;
               if (!lhs || !rhs) return false;

               return lhs->_name == rhs->_name;
          }

          bool operator()(const TypePointer& lhs, const std::string& rhs) const noexcept
          {
               return lhs && lhs->_name == rhs;
          }

          bool operator()(const std::string& lhs, const TypePointer& rhs) const noexcept
          {
               return rhs && lhs == rhs->_name;
          }
     };

     enum struct Qualifiers : std::uint_fast8_t
     {
          None = 0,
          Const = 1 << 0,
          Volatile = 1 << 1,
          ConstVolatile = Const | Volatile,
     };

     class QualifiedType : public TypeBase
     {
     public:
          explicit QualifiedType(std::string name, const Qualifiers qualifiers)
              : TypeBase(std::move(name)), _qualifiers(qualifiers)
          {
          }

          [[nodiscard]] Qualifiers qualifiers(void) const noexcept // NOLINT(readability-identifier-naming)
          {
               return this->_qualifiers;
          }
          [[nodiscard]] Qualifiers qualifiers( // NOLINT(readability-identifier-naming)
              const Qualifiers newQualifiers) noexcept
          {
               return std::exchange(this->_qualifiers, newQualifiers);
          }

          [[nodiscard]] constexpr bool IsConst(void) const noexcept
          {
               return (std::to_underlying(this->_qualifiers) & std::to_underlying(Qualifiers::Const)) != 0;
          }
          [[nodiscard]] constexpr bool IsVolatile(void) const noexcept
          {
               return (std::to_underlying(this->_qualifiers) & std::to_underlying(Qualifiers::Volatile)) != 0;
          }

     private:
          Qualifiers _qualifiers;
     };

     class VoidType final : public QualifiedType
     {
     public:
          explicit VoidType(std::string name, const Qualifiers qualifiers) : QualifiedType(std::move(name), qualifiers)
          {
          }
          [[nodiscard]] std::string RawName(void) const noexcept override { return "void"; }

          [[nodiscard]] TypeTraits Traits(void) const noexcept override
          {
               return TypeTraits{TypeTraitEnum::Incomplete};
          }

          [[nodiscard]] std::size_t Size(void) const noexcept override { return 0; }
          [[nodiscard]] std::size_t Alignment(void) const noexcept override { return 0; };

          [[nodiscard]] ConversionSequence CompareTo(const std::shared_ptr<TypeBase>& other) override
          {
               return typeid(*other) == typeid(VoidType) // NOLINT
                          ? ConversionSequence{SBOVector<ConversionSequence::ConversionKind>{}}
                          : ConversionSequence{std::nullopt};
          }

          [[nodiscard]] std::shared_ptr<TypeBase> CommonWith(const std::shared_ptr<TypeBase>& other) override;
     };
} // namespace ecpps::typeSystem
