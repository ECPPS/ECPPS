#pragma once
#include "ClassInfo.h"
#include "SBOVector.h"
#include "Shared/Diagnostics.h"
#include "TypeSystem/TypeBase.h"

namespace ecpps::typeSystem
{
     class ClassType final : public QualifiedType
     {
     public:
          ClassType(const ClassInfo* info, std::string name, const Qualifiers qualifiers)
              : QualifiedType(std::move(name), qualifiers), _info(info)
          {
          }

          [[nodiscard]] const ClassInfo& Info(void) const noexcept
          {
               return *this->_info;
          }
          [[nodiscard]] bool IsComplete(void) const noexcept override
          {
               return this->_info->IsComplete();
          }

          [[nodiscard]] std::size_t Size(void) const noexcept override
          {
               runtime_assert(this->_info->IsComplete(), "Size() of an incomplete class");
               return this->_info->size;
          }
          [[nodiscard]] std::size_t Alignment(void) const noexcept override
          {
               runtime_assert(this->_info->IsComplete(), "Alignment() of an incomplete class");
               return this->_info->alignment;
          }

          [[nodiscard]] std::string RawName(void) const override
          {
               return std::format("{}{}{}", this->IsConst() ? "const " : "", this->IsVolatile() ? "volatile " : "",
                                  this->_info->RawName());
          }
          [[nodiscard]] TypeTraits Traits(void) const noexcept override
          {
               TypeTraits traits{TypeTraitEnum::Object, TypeTraitEnum::Class};
               if (this->_info->kind == ClassKind::Union) traits.Set(TypeTraitEnum::Union);
               // TODO: Literal needs the constexpr constructor/destructor rules.
               if (!this->_info->IsComplete())
               {
                    traits.Set(TypeTraitEnum::Incomplete);
                    return traits;
               }
               if (this->_info->triviallyCopyable)
               {
                    traits.Set(TypeTraitEnum::TriviallyCopyable);
                    traits.Set(TypeTraitEnum::ImplicitLifetime);
               }
               return traits;
          }

          [[nodiscard]] ConversionSequence CompareTo(const TypeBase* other) const override
          {
               if (other == this) return ConversionSequence{SBOVector<ConversionSequence::ConversionKind>{}};
               throw TracedException("TODO: conversions involving class types are not implemented");
          }

          [[nodiscard]] const TypeBase* CommonWith(const TypeBase* other) const override
          {
               if (other == this) return this;
               const auto* otherClass = other != nullptr ? other->CastTo<ClassType>() : nullptr;
               if (otherClass == nullptr) return nullptr;
               if (otherClass->_info == this->_info)
                    throw TracedException("TODO: cv-combined common type of class types");
               throw TracedException("TODO: common type of different class types (derived-to-base)");
          }

     private:
          const ClassInfo* _info;
     };
     [[nodiscard]] inline bool IsClass(NonowningTypePointer t) noexcept
     {
          return t != nullptr && t->CastTo<ClassType>() != nullptr;
     }
} // namespace ecpps::typeSystem
