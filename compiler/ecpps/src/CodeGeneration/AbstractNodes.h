#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ecpps::ir::abstract
{
     enum struct DescriptionType
     {
          Copy, // copies A = B
          Add   // A = B + C
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

     enum struct DescribedOperandType : std::uint8_t
     {
          Unused, // reserved
          Input,
          Output
     };
     struct DescribedOperand
     {
          DescribedOperandType type{};
     };

     struct InstructionDescription
     {
          DescriptionType type{};
          std::vector<DescribedOperand> operands{};
     };

     std::string ToString(const InstructionDescription& description);
} // namespace ecpps::ir::abstract
