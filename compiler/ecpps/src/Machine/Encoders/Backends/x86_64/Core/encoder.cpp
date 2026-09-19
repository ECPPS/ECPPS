#include "encoder.h"
#include <cstddef>
#include <format>
#include <new>
#include <span>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "Parsing/Tokeniser.h"
#include "RuntimeAssert.h"
#include "Shared/Diagnostics.h"

extern template std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Copy>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray);

extern template std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Copy>(
          std::span<const std::byte> data);

std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::Encode(
     const std::vector<ir::abstract::VirtualInstruction>& input)
{
     std::vector<ecpps::ir::abstract::Instruction> instructions{};

     for (const auto& instruction : input) instructions.append_range(EncodeSingle(instruction));

     return instructions;
}
std::string ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::Stringify(
     const ecpps::ir::abstract::Instruction& instruction) const
{
     auto opcode = instruction.opcode;
     switch (opcode)
     {
     case X8664InstructionName::Mov:
     {
          runtime_assert(instruction.description.size() == sizeof(MovInstruction), "invalid MOV");
          const auto* mov = std::launder(reinterpret_cast<const MovInstruction*>(instruction.description.data()));
          return std::format("MOV {}, {}", ToString(mov->destination), ToString(mov->source));
     }
     case X8664InstructionName::Add:
     {
          runtime_assert(instruction.description.size() == sizeof(AddInstruction), "invalid ADD");
          const auto* add = std::launder(reinterpret_cast<const AddInstruction*>(instruction.description.data()));
          return std::format("ADD {}, {}", ToString(add->modifiedDestination), ToString(add->source));
     }
     }

     return "__unknown";
}

[[nodiscard]] ecpps::ir::abstract::VirtualRegisterMap& ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     GetVRM(void) noexcept
{
     return *this->_target->registerMap;
}
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::EncodeSingle(
     const ir::abstract::VirtualInstruction& instruction)
{
     std::vector<ecpps::ir::abstract::Instruction> instructions{};

     switch (instruction.type)
     {
     case ecpps::ir::abstract::VirtualInstructionType::Copy:
          instructions.append_range(
               EncoderImplementation<ir::abstract::VirtualInstructionType::Copy>(instruction.operands));
          break;
     default: throw TracedException("Invalid instruction"); // TODO: Diagnostics
     }

     return instructions;
}

std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EnsureMaterialisation(ecpps::ir::abstract::VirtualRegister virtualRegister)
{
     if (this->GetVRM().IsMaterialised(virtualRegister)) return {};

     const auto& value = this->GetVRM().GetValue(virtualRegister);
     runtime_assert(value.type != ir::abstract::StateType::Unknown,
                    "Cannot materialise a  register with unknown value"); // TODO: Diagnostics
     runtime_assert(value.type != ir::abstract::StateType::Impossible,
                    "Cannot materialise a  register with impossible state"); // TODO: Diagnostics

     const auto& valueBase = *std::launder(reinterpret_cast<const AssignedValueBase*>(value.data.data()));
     switch (valueBase.type)
     {
     case ecpps::abi::encoders::x8664::AssignedValueType::Copy:
          return MaterialisationImplementation<ir::abstract::VirtualInstructionType::Copy>(
               std::span<const std::byte>{value.data});
     }

     throw TracedException("Invalid opcode");
}

[[nodiscard]] static std::string FormatRegister(ecpps::abi::encoders::x8664::RegisterIndex index)
{
     std::string formattedRegister{};
     switch (index)
     {
     case ecpps::abi::encoders::x8664::RegisterIndex::Rax: formattedRegister = "rax"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rbx: formattedRegister = "rbx"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rcx: formattedRegister = "rcx"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rdx: formattedRegister = "rdx"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rsp: formattedRegister = "rsp"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rbp: formattedRegister = "rbp"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rsi: formattedRegister = "rsi"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rdi: formattedRegister = "rdi"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R8: formattedRegister = "r8"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R9: formattedRegister = "r8"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R10: formattedRegister = "r10"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R11: formattedRegister = "r11"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R12: formattedRegister = "r12"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R13: formattedRegister = "r13"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R14: formattedRegister = "r14"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R15: formattedRegister = "r15"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rip: formattedRegister = "rip"; break;
     }
     return formattedRegister;
}

[[nodiscard]] std::string ecpps::abi::encoders::x8664::ToString(const Operand& operand)
{
     return std::visit(OverloadedVisitor{[](const RegisterOperand& reg)
                                         {
                                              return FormatRegister(reg.index);
                                         },
                                         [](const MemoryOperand& mem)
                                         {
                                              return std::format("[{} + {}]", FormatRegister(mem.relativeTo),
                                                                 mem.offset);
                                         },
                                         [](const IntegerOperand& integer)
                                         {
                                              return std::format("{}", integer.value);
                                         }},
                       operand);
}
