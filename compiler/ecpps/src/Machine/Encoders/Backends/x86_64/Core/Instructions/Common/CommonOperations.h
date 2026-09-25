#pragma once

#include <cstdint>
#include <tuple>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"

namespace ecpps::abi::encoders::x8664::inline common
{
     enum struct AssignedValueType : std::uint16_t // NOLINT(performance-enum-size)
     {
          Copy,
          CopyInteger,
          Add,
          Sub,
          LeftShift,
          RightShift,
          BinaryOr,
          BinaryAnd,
          BinaryXor,
          BitwiseNot,
          Neg,
          Movsx,
          Movzx
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
          using CopyIntegerToRegister = AssignmentValue<AssignedValueType::CopyInteger, std::uint64_t>;
          using AddRegisters =
               AssignmentValue<AssignedValueType::Add, ir::abstract::VirtualRegister, ir::abstract::VirtualRegister>;
          using SubRegisters =
               AssignmentValue<AssignedValueType::Sub, ir::abstract::VirtualRegister, ir::abstract::VirtualRegister>;
          using LeftShiftRegisters = AssignmentValue<AssignedValueType::LeftShift, ir::abstract::VirtualRegister,
                                                     ir::abstract::VirtualRegister>;
          using RightShiftRegisters = AssignmentValue<AssignedValueType::RightShift, ir::abstract::VirtualRegister,
                                                      ir::abstract::VirtualRegister>;
          using BinaryOrRegisters = AssignmentValue<AssignedValueType::BinaryOr, ir::abstract::VirtualRegister,
                                                    ir::abstract::VirtualRegister>;
          using BinaryAndRegisters = AssignmentValue<AssignedValueType::BinaryAnd, ir::abstract::VirtualRegister,
                                                     ir::abstract::VirtualRegister>;
          using BinaryXorRegisters = AssignmentValue<AssignedValueType::BinaryXor, ir::abstract::VirtualRegister,
                                                     ir::abstract::VirtualRegister>;
          using BitwiseNotRegisters = AssignmentValue<AssignedValueType::BitwiseNot, ir::abstract::VirtualRegister>;
          using ArithmeticNegationRegisters = AssignmentValue<AssignedValueType::Neg, ir::abstract::VirtualRegister>;
          using SignExtendToRegister =
               AssignmentValue<AssignedValueType::Movsx, ir::abstract::VirtualRegister, ir::abstract::VirtualRegister>;
          using ZeroExtendToRegister =
               AssignmentValue<AssignedValueType::Movzx, ir::abstract::VirtualRegister, ir::abstract::VirtualRegister>;
     } // namespace values
     namespace materialisations
     {
          using PhysicalRegister = CurrentMaterialisation<MaterialisationType::PhysicalRegisterAssigned, RegisterIndex>;
     }
} // namespace ecpps::abi::encoders::x8664::inline common
