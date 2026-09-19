#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/VirtualInstructionEncoder.h"
#include "Machine/Machine.h"
namespace ecpps::abi::encoders::x8664
{
     enum struct OperandType : std::uint8_t
     {
          Register,
          Memory,
          Integer
     };

     struct X8664InstructionName
     {
          explicit X8664InstructionName(void) = delete;

          constexpr static std::size_t Mov = 0; // b = a
          constexpr static std::size_t Add = 1; // c = a + b
          constexpr static std::size_t Ret = 2; // return [a]
     };

     inline namespace instructionSetData
     {
          enum struct RegisterIndex : std::uint8_t
          {
               Rax,
               Rcx,
               Rdx,
               Rbx,
               Rsp,
               Rbp,
               Rsi,
               Rdi,
               R8,
               R9,
               R10,
               R11,
               R12,
               R13,
               R14,
               R15,
               Rip
          };

          struct RegisterOperand
          {
               RegisterIndex index{};
          };
          struct MemoryOperand
          {
               RegisterIndex relativeTo{};
               std::uint32_t offset{};
          };
          struct IntegerOperand
          {
               std::uint64_t value{};
          };
          using Operand = std::variant<RegisterOperand, MemoryOperand, IntegerOperand>;

          struct AddInstruction // modifiedDestination += source
          {
               Operand modifiedDestination{};
               Operand source{};
          };
          struct MovInstruction // destination = source
          {
               Operand destination{};
               Operand source{};
          };
          struct RetInstruction // return value (if any)
          {
               std::optional<Operand> value{};
          };

          [[nodiscard]] std::string ToString(const Operand& operand);
     } // namespace instructionSetData

     struct X8664VirtualInstructionEncoder final : api::VirtualInstructionEncoder
     {
          explicit X8664VirtualInstructionEncoder(api::Target& target) : VirtualInstructionEncoder(ISA::x86_64, target)
          {
          }

          [[nodiscard]] std::vector<ir::abstract::Instruction> Encode(
               const std::vector<ir::abstract::VirtualInstruction>& input) final;
          [[nodiscard]] std::string Stringify(const ir::abstract::Instruction& instruction) const final;

     private:
          std::vector<ir::abstract::Instruction> EncodeSingle(const ir::abstract::VirtualInstruction&);
          [[nodiscard]] ir::abstract::VirtualRegisterMap& GetVRM(void) noexcept;

          std::vector<ecpps::ir::abstract::Instruction> EnsureMaterialisation(
               ir::abstract::VirtualRegister virtualRegister);

          template <ir::abstract::VirtualInstructionType TType>
          std::vector<ir::abstract::Instruction> EncoderImplementation(
               const std::vector<ir::abstract::VirtualRegister>& registerArray);

          template <ir::abstract::VirtualInstructionType TType>
          std::vector<ir::abstract::Instruction> MaterialisationImplementation(std::span<const std::byte> data);
     };
} // namespace ecpps::abi::encoders::x8664
