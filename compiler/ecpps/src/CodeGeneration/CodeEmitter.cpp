#include "CodeEmitter.h"
#include <variant>
#include "../Parsing/Tokeniser.h"
#include "Emitters/x86_64.h"

std::vector<std::byte> ecpps::codegen::CodeEmitter::EmitRoutine(const Routine& routine)
{
     std::vector<std::byte> generated{};
     generated.reserve(routine.instructions.size() * 2);
     // TODO: Check preconditions

     for (const auto& instruction : routine.instructions) generated.append_range(this->EmitInstruction(instruction));

     // TODO: Check postconditions
     return generated;
}

std::unique_ptr<ecpps::codegen::CodeEmitter> ecpps::codegen::CodeEmitter::New(abi::ISA isa)
{
     switch (isa)
     {
     case abi::ISA::x86_64: return std::make_unique<emitters::X8664Emitter>();
     }

     return nullptr;
}

std::vector<std::byte> ecpps::codegen::CodeEmitter::EmitInstruction(const Instruction& instruction)
{
     return std::visit(OverloadedVisitor{[this](const MovInstruction& mov) { return this->EmitMov(mov); },
                                         [this](const ReturnInstruction&) { return this->EmitReturn(); },
                                         [](auto&&) -> std::vector<std::byte> { throw nullptr; }},
                       instruction);
}
