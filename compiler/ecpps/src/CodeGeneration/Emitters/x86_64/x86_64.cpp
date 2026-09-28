// NOLINT(readability-identifier-length)

#include "x86_64.h"
#include <cstddef>
#include <print>
#include <utility>
#include "Execution/Context.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitInstruction(
     const ir::abstract::Instruction& instruction)
{
     switch (instruction.opcode)
     {
     case abi::encoders::x8664::X8664InstructionName::Mov: return this->EmitMov(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Add: return this->EmitAdd(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Ret: return this->EmitRet(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Sub: return this->EmitSub(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Push: return this->EmitPush(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::LeftShift: return this->EmitLeftShift(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::RightShift: return this->EmitRightShift(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Xchg: return this->EmitXchg(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Pop: return this->EmitPop(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::BinaryOr: return this->EmitBinaryOr(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::BinaryAnd: return this->EmitBinaryAnd(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::BinaryXor: return this->EmitBinaryXor(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::BitwiseNot: return this->EmitBitwiseNot(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Neg: return this->EmitArithmeticNegation(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::SignExtend: return this->EmitMovsx(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::ZeroExtend: return this->EmitMovzx(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Call: return this->EmitCall(instruction.description);
     default: throw TracedException("x86-64 does not implement this opcode yet");
     }
}

void ecpps::codegen::emitters::X8664Emitter::PatchCalls(std::vector<std::byte>& instructions, const Routine& routine)
{
     for (const auto offset : this->_callPatches)
     {
          if (instructions.size() <= offset) continue; // ???
          runtime_assert(instructions[offset] == std::byte{0xe8}, "Invalid offset");
          std::int32_t index{};
          std::memcpy(&index, instructions.data() + offset + 1uz, sizeof(std::int32_t));

          const ir::FunctionScope* function = routine.scopes[static_cast<std::size_t>(index)];
          const std::int32_t displacement = static_cast<std::int32_t>(function->emittedOffset) -
                                            static_cast<std::int32_t>(offset + routine.emittedOffset) - 5;
          std::memcpy(instructions.data() + offset + 1uz, &displacement, sizeof(std::int32_t));
     }
}
