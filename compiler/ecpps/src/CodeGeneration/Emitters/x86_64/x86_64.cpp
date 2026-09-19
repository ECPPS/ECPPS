// NOLINT(readability-identifier-length)

#include "x86_64.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitInstruction(
     const ir::abstract::Instruction& instruction)
{
     switch (instruction.opcode)
     {
     case abi::encoders::x8664::X8664InstructionName::Mov: return this->EmitMov(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Add: return this->EmitAdd(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Ret: return this->EmitRet(instruction.description);
     default: throw TracedException("not implemented");
     }
}
