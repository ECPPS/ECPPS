#include "CodeEmitter.h"
#include <stdexcept>
#include "../Parsing/Tokeniser.h"
#include "../Shared/Diagnostics.h"
#include "Emitters/x86_64.h"
#include "Nodes.h"

std::vector<std::byte> ecpps::codegen::CodeEmitter::EmitRoutine(const Routine& routine, std::size_t displacement)
{
     std::vector<std::byte> generated{};
     generated.reserve(routine.instructions.size() * 2);
     // TODO: Check preconditions

     for (const auto& instruction : routine.instructions)
     {
          this->_currentInstructionBase = generated.size() + displacement;
          generated.append_range(this->EmitInstruction(instruction));
     }

     // TODO: Check postconditions
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

std::vector<std::byte> ecpps::codegen::CodeEmitter::EmitInstruction(const Instruction& instruction)
{
     return std::visit(
         OverloadedVisitor{
             [this](const MovInstruction& mov) { return this->EmitMov(mov); }, [this](const AddInstruction& add)
             { return this->EmitAdd(add); }, [this](const SubInstruction& sub) { return this->EmitSub(sub); },
             [this](const MulInstruction& mul) { return this->EmitMul(mul); }, [this](const DivInstruction& div)
             { return this->EmitDiv(div); }, [this](const CallInstruction& call) { return this->EmitCall(call); },
             [this](const TakeAddressInstruction& lea) { return this->EmitLea(lea); }, [this](const ReturnInstruction&)
             { return this->EmitReturn(); }, [](auto&&) -> std::vector<std::byte> { throw nullptr; }},
         instruction);
}
