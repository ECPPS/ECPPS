#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
          explicit constexpr Operand(std::uint8_t initial) : _value(initial) {}
          explicit constexpr Operand() : _value(0) {}
          [[nodiscard]] constexpr OperandType Type(void) const noexcept
          { return static_cast<OperandType>(this->_value & 0b11); }
          [[nodiscard]] constexpr OperandCompatibility Compatibility(void) const noexcept
          { return static_cast<OperandCompatibility>((this->_value) & 0b1100); }

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
          Copy,   // copies A = B
          Add,    // A = B + C
          Return, // returns A
     };
     struct VirtualRegister
     {
          std::size_t index{};

          [[nodiscard]] constexpr bool operator==(const VirtualRegister other) const noexcept
          { return other.index == this->index; }
     };
     struct VirtualInstruction
     {
          VirtualInstructionType type{};
          std::vector<VirtualRegister> operands{}; // OPTIMISE: small vector
     };
     enum struct StateType : std::uint8_t
     {
          Unknown,
          Allocation,
          Impossible,
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
     };

     template <typename TPossibleRegister>
     concept VirtualRegisterUsable =
         std::same_as<TPossibleRegister, VirtualRegister> || std::unsigned_integral<TPossibleRegister>;

     struct VirtualRegisterMap
     {
          void ReferenceRegister(VirtualRegisterUsable auto reg) { DataFromRegister(reg).useCount++; }
          std::size_t DereferenceRegister(VirtualRegisterUsable auto reg) { return --DataFromRegister(reg).useCount; }
          void Materialise(VirtualRegisterUsable auto reg, State&& bytecode)
          { DataFromRegister(reg).materialised = std::move(bytecode); }
          void Materialise(VirtualRegisterUsable auto reg, const State& bytecode)
          { DataFromRegister(reg).materialised = bytecode; }
          bool IsMaterialised(VirtualRegisterUsable auto reg) const
          { return DataFromRegister(reg).materialised.has_value(); }
          const std::optional<State>& GetMaterialisation(VirtualRegisterUsable auto reg) const
          { return DataFromRegister(reg).materialised; }

          void UpdateValue(VirtualRegisterUsable auto reg, State&& bytecode)
          { DataFromRegister(reg).currentValue = std::move(bytecode); }
          void UpdateValue(VirtualRegisterUsable auto reg, const State& bytecode)
          { DataFromRegister(reg).currentValue = bytecode; }
          const State& GetValue(VirtualRegisterUsable auto reg) const { return DataFromRegister(reg).currentValue; }

     private:
          std::unordered_map<std::size_t, RegisterData> _map;

          static constexpr std::size_t IndexFromRegister(VirtualRegisterUsable auto reg)
          {
               if constexpr (std::integral<decltype(reg)>) return reg;
               else
                    return reg.index;
          }
          RegisterData& DataFromRegister(VirtualRegisterUsable auto reg) { return this->_map[IndexFromRegister(reg)]; }
          const RegisterData& DataFromRegister(VirtualRegisterUsable auto reg) const
          { return this->_map.at(IndexFromRegister(reg)); }
     };
} // namespace ecpps::ir::abstract
