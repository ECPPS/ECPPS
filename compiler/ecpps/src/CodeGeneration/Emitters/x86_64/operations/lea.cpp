#include <cstddef>
#include <format>
#include <utility>
#include <variant>
#include <vector>
#include "../x86_64.h"
#include "CodeGeneration/Emitters/x86_64/Opcodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "Parsing/Tokeniser.h"
#include "Shared/Diagnostics.h"

using namespace ecpps::abi::encoders::x8664;
using namespace ecpps::codegen::x86_64;

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitLea(const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::LeaInstruction), "Invalid LEA instruction");

     const auto& lea = *std::launder(reinterpret_cast<const abi::encoders::x8664::LeaInstruction*>(description.data()));
     const auto instructionWidth = lea.width;

     return std::visit(
          OverloadedVisitor{
               [this, instructionWidth,
                destination = lea.destination](const StringAddressOperand string) -> std::vector<std::byte>
               {
                    this->AddStringPatch(static_cast<std::uint32_t>(this->_currentInstructionBase),
                                         InstructionPatchType::LeaFrom,
                                         StringIndex{.indexInTable = string.tableIndex, .offset = string.offset});
                    return GenerateLea(instructionWidth, std::to_underlying(destination.index), MemBase(Rip));
               },
               [instructionWidth, destination = lea.destination](const MemoryOperand mem) -> std::vector<std::byte>
               {
                    return GenerateLea(instructionWidth, std::to_underlying(destination.index),
                                       MemBase(std::to_underlying(mem.relativeTo), mem.offset));
               },
               [](const StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid MOV source");
               }},
          lea.address);
}
