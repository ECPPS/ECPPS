#include "CodeEmitter.h"
#include <stdexcept>
#include "../Shared/Diagnostics.h"
#include "Emitters/x86_64/x86_64.h"
#include "Nodes.h"

ecpps::codegen::CodeEmitter::~CodeEmitter(void) = default;

std::vector<std::byte> ecpps::codegen::CodeEmitter::EmitRoutine(const Routine& routine, std::size_t displacement)
{
     std::vector<std::byte> generated{};
     generated.reserve(routine.physicalInstructions.size() * 2);
     for (const auto& instruction : routine.physicalInstructions)
     {
          this->_currentInstructionBase = generated.size() + displacement;
          generated.append_range(this->EmitInstruction(instruction));
     }
     return generated;
}

std::unique_ptr<ecpps::codegen::CodeEmitter> ecpps::codegen::CodeEmitter::New(abi::ISA isa)
{
     switch (isa)
     {
     case abi::ISA::x86_64: return std::make_unique<emitters::X8664Emitter>();
     default: throw TracedException(std::runtime_error("Emitter for the selected ISA does not exist"));
     }
}
