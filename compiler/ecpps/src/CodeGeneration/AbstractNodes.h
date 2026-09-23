#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Shared/Diagnostics.h"

namespace ecpps::ir::abstract
{
     enum struct DescriptionType // NOLINT(performance-enum-size)
     {
     };
     enum struct OperandType : std::uint8_t
     {
          Input = 0b01,
          Output = 0b10,
          Both = 0b11
     };
     enum struct OperandCompatibility : std::uint8_t
     {
          Register = 0b0100,
          Memory = 0b1000,
          Both = 0b1100
     };
     struct Operand
     {
          explicit constexpr Operand(OperandType type, OperandCompatibility compatibility)
              : _value(std::to_underlying(type) | (std::to_underlying(compatibility)))
          {
          }
          explicit constexpr Operand(std::uint8_t initial) : _value(initial)
          {
          }
          explicit constexpr Operand() : _value(0)
          {
          }
          [[nodiscard]] constexpr OperandType Type(void) const noexcept
          {
               return static_cast<OperandType>(this->_value & 0b11);
          }
          [[nodiscard]] constexpr OperandCompatibility Compatibility(void) const noexcept
          {
               return static_cast<OperandCompatibility>((this->_value) & 0b1100);
          }

     private:
          std::uint8_t _value;
     };

     using DynamicBytecode = std::vector<std::byte>;
     using EncodedOpcode = std::uint64_t;

     struct Instruction
     {
          EncodedOpcode opcode;
          DynamicBytecode description;
     };
     enum struct VirtualInstructionType : std::uint32_t // NOLINT(performance-enum-size)
     {
          Copy,
          CopyInteger,
          Add,
          Return,
          Sub,
          LeftShift,
          RightShift,
          BinaryOr,
          BinaryAnd,
          BinaryXor,
          BitwiseNot,
          ArithmeticNegate,
          Reinterpret,              // same width, different sign
          ZeroExtension,            // widening (unsigned)
          SignExtension,            // widening (signed)
          SignExtendAndReinterpret, // widening (unsigned <- signed)
          ZeroExtendAndReinterpret, // widening (signed <- unsigned)
          Truncate                  // narrowing (signed/unsigned)
     };
     constexpr std::string_view ToString(const VirtualInstructionType type)
     {
          switch (type)
          {
          case VirtualInstructionType::Copy: return "copy";
          case VirtualInstructionType::CopyInteger: return "copy-int";
          case VirtualInstructionType::Add: return "add";
          case VirtualInstructionType::Sub: return "sub";
          case VirtualInstructionType::Return: return "return";
          case VirtualInstructionType::LeftShift: return "left-shift";
          case VirtualInstructionType::RightShift: return "right-shift";
          case VirtualInstructionType::BinaryOr: return "bin-or";
          case VirtualInstructionType::BinaryAnd: return "bin-and";
          case VirtualInstructionType::BinaryXor: return "bin-xor";
          case VirtualInstructionType::BitwiseNot: return "compl";
          case VirtualInstructionType::Reinterpret: return "reinterpret";
          case VirtualInstructionType::ZeroExtension: return "zero-extension";
          case VirtualInstructionType::SignExtension: return "sign-extension";
          case VirtualInstructionType::SignExtendAndReinterpret: return "sign-extend-and-reinterpret";
          case VirtualInstructionType::ZeroExtendAndReinterpret: return "zero-extend-and-reinterpret";
          case VirtualInstructionType::Truncate: return "truncate";

          case VirtualInstructionType::ArithmeticNegate: return "negate";
          }
          throw TracedException("control flow");
     }

     struct VirtualRegister
     {
          std::size_t index{};

          [[nodiscard]] constexpr bool operator==(const VirtualRegister other) const noexcept
          {
               return other.index == this->index;
          }
     };
     struct VirtualInstruction
     {
          VirtualInstructionType type{};
          std::vector<VirtualRegister> operands{};
     };
     enum struct StateType : std::uint8_t
     {
          Unknown,
          Allocation,
          Impossible,
     };
     enum struct AllocationClass : std::uint8_t
     {
          Locked = 0,
          HotTemporary,
          HotAllocation,
          Temporary,
          Allocation,
          ColdAllocation,

          Invalid = std::numeric_limits<std::uint8_t>::max()
     };

     struct State
     {
          StateType type = StateType::Unknown;
          DynamicBytecode data;
     };
     struct RegisterData
     {
          std::size_t useCount{};
          std::optional<State> materialised;
          State currentValue{};
          std::size_t size{};
          std::size_t alignment{};
          AllocationClass allocationClass = AllocationClass::Temporary;
     };

     template <typename TPossibleRegister>
     concept VirtualRegisterUsable =
          std::same_as<TPossibleRegister, VirtualRegister> || std::unsigned_integral<TPossibleRegister>;

     struct VirtualRegisterMap
     {
          void ReferenceRegister(VirtualRegisterUsable auto reg)
          {
               DataFromRegister(reg).useCount++;
          }
          std::size_t DereferenceRegister(VirtualRegisterUsable auto reg)
          {
               return --DataFromRegister(reg).useCount;
          }
          void Materialise(VirtualRegisterUsable auto reg, State&& bytecode)
          {
               DataFromRegister(reg).materialised = std::move(bytecode);
          }
          void Materialise(VirtualRegisterUsable auto reg, const State& bytecode)
          {
               DataFromRegister(reg).materialised = bytecode;
          }
          void ClearMaterialisation(VirtualRegisterUsable auto reg)
          {
               DataFromRegister(reg).materialised = std::nullopt;
          }
          bool IsMaterialised(VirtualRegisterUsable auto reg) const
          {
               return DataFromRegister(reg).materialised.has_value();
          }
          const std::optional<State>& GetMaterialisation(VirtualRegisterUsable auto reg) const
          {
               return DataFromRegister(reg).materialised;
          }

          void UpdateValue(VirtualRegisterUsable auto reg, State&& bytecode)
          {
               DataFromRegister(reg).currentValue = std::move(bytecode);
          }
          void UpdateValue(VirtualRegisterUsable auto reg, const State& bytecode)
          {
               DataFromRegister(reg).currentValue = bytecode;
          }
          const State& GetValue(VirtualRegisterUsable auto reg) const
          {
               return DataFromRegister(reg).currentValue;
          }

          void Describe(VirtualRegisterUsable auto reg, const std::size_t size, const std::size_t alignment,
                        const AllocationClass allocationClass)
          {
               auto& data = DataFromRegister(reg);
               data.size = size;
               data.alignment = alignment;
               data.allocationClass = allocationClass;
          }
          std::size_t GetSize(VirtualRegisterUsable auto reg) const
          {
               return DataFromRegister(reg).size;
          }
          std::size_t GetAlignment(VirtualRegisterUsable auto reg) const
          {
               return DataFromRegister(reg).alignment;
          }
          AllocationClass GetAllocationClass(VirtualRegisterUsable auto reg) const
          {
               return DataFromRegister(reg).allocationClass;
          }

     private:
          std::unordered_map<std::size_t, RegisterData> _map;

          static constexpr std::size_t IndexFromRegister(VirtualRegisterUsable auto reg)
          {
               if constexpr (std::integral<decltype(reg)>) return reg;
               else
                    return reg.index;
          }
          RegisterData& DataFromRegister(VirtualRegisterUsable auto reg)
          {
               return this->_map[IndexFromRegister(reg)];
          }
          const RegisterData& DataFromRegister(VirtualRegisterUsable auto reg) const
          {
               return this->_map.at(IndexFromRegister(reg));
          }
     };
} // namespace ecpps::ir::abstract
