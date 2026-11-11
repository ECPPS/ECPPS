#pragma once

#include <algorithm>
#include "Machine/Encoders/API/Platform.h"
#include "TypeSystem/ClassType.h"
#include "TypeSystem/CompoundTypes.h"

namespace ecpps::abi::api
{
     [[nodiscard]] inline bool IsIntegerAggregateField(const typeSystem::ClassField& field) noexcept;

     [[nodiscard]] inline bool IsIntegerAggregate(const typeSystem::TypeBase* type) noexcept
     {
          if (type == nullptr) return false;
          const auto traits = type->Traits();
          if (traits.Has(typeSystem::TypeTraitEnum::Integral) || traits.Has(typeSystem::TypeTraitEnum::Pointer) ||
              traits.Has(typeSystem::TypeTraitEnum::Reference))
               return true;

          if (const auto* array = type->CastTo<typeSystem::ArrayType>())
               return IsIntegerAggregate(array->ElementType());

          const auto* classType = type->CastTo<typeSystem::ClassType>();
          if (classType == nullptr || !classType->IsComplete()) return false;
          return std::ranges::all_of(classType->Info().fields, IsIntegerAggregateField);
     }

     [[nodiscard]] inline bool IsIntegerAggregateField(const typeSystem::ClassField& field) noexcept
     {
          return IsIntegerAggregate(field.type);
     }

     [[nodiscard]] inline AggregateValueInfo AggregateInfo(const typeSystem::ClassType& type) noexcept
     {
          const auto& info = type.Info();
          return {.size = info.size,
                  .triviallyCopyable = info.triviallyCopyable,
                  .integerOnly = IsIntegerAggregate(&type),
                  .hasUserDeclaredSpecialMembers =
                       info.Has(typeSystem::ClassInfo::Property::UserDeclaredConstructors) ||
                       info.Has(typeSystem::ClassInfo::Property::UserDeclaredCopyAssignment) ||
                       info.Has(typeSystem::ClassInfo::Property::UserDeclaredMoveAssignment) ||
                       info.Has(typeSystem::ClassInfo::Property::UserDeclaredDestructor)};
     }

     [[nodiscard]] inline AggregatePassingPlan ClassArgumentPassing(const PlatformBase& platform,
                                                                    const typeSystem::TypeBase* type)
     {
          const auto* classType = type != nullptr ? type->CastTo<typeSystem::ClassType>() : nullptr;
          return classType == nullptr ? AggregatePassingPlan{}
                                      : platform.ClassArgumentPassing(AggregateInfo(*classType));
     }

     [[nodiscard]] inline AggregatePassingPlan ClassReturnPassing(const PlatformBase& platform,
                                                                  const typeSystem::TypeBase* type)
     {
          const auto* classType = type != nullptr ? type->CastTo<typeSystem::ClassType>() : nullptr;
          return classType == nullptr ? AggregatePassingPlan{} : platform.ClassReturnPassing(AggregateInfo(*classType));
     }

     [[nodiscard]] inline bool IsSingleIntegerRegister(const AggregatePassingPlan& plan,
                                                       const std::size_t size) noexcept
     {
          return plan.kind == AggregatePassingKind::Direct && plan.pieces.size() == 1 &&
                 plan.pieces.front().offset == 0 && plan.pieces.front().size == size &&
                 plan.pieces.front().registerClass == AggregateRegisterClass::Integer;
     }
} // namespace ecpps::abi::api
