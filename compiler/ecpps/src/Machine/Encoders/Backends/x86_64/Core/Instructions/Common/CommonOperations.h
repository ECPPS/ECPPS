#pragma once

#include <cstdint>
#include <tuple>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"

namespace ecpps::abi::encoders::x8664::inline common
{
     enum struct AssignedValueType : std::uint16_t // NOLINT(performance-enum-size)
     {
          Copy, // copies something to something
     };
     enum struct MaterialisationType : std::uint16_t // NOLINT(performance-enum-size)
     {
          PhysicalRegisterAssigned, // parameters: RegisterIndex
     };
     struct AssignedValueBase
     {
          AssignedValueType type;

          explicit AssignedValueBase(AssignedValueType type) : type(type)
          {
          }
     };
     template <AssignedValueType TType, typename... TParameters> struct AssignmentValue : AssignedValueBase
     {
          constexpr static auto ConstType = TType;

          explicit AssignmentValue(void) : AssignedValueBase(TType)
          {
          }

          std::tuple<TParameters...> parameters{};
     };

     struct MaterialisationBase
     {
          MaterialisationType type;

          explicit MaterialisationBase(MaterialisationType type) : type(type)
          {
          }
     };
     template <MaterialisationType TType, typename... TParameters> struct CurrentMaterialisation : MaterialisationBase
     {
          constexpr static auto ConstType = TType;

          explicit CurrentMaterialisation(void) : MaterialisationBase(TType)
          {
          }

          std::tuple<TParameters...> parameters{};
     };

     namespace values
     {
          using CopyRegisterToRegister =
               AssignmentValue<AssignedValueType::Copy, ir::abstract::VirtualRegister, ir::abstract::VirtualRegister>;
     } // namespace values
     namespace materialisations
     {
          using PhysicalRegister = CurrentMaterialisation<MaterialisationType::PhysicalRegisterAssigned, RegisterIndex>;
     }
} // namespace ecpps::abi::encoders::x8664::inline common
