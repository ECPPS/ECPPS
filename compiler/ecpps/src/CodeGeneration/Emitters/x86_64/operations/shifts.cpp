#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <variant>
#include <vector>
#include "../x86_64.h"
#include "CodeGeneration/Emitters/x86_64/Opcodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "Parsing/Tokeniser.h"
#include "RuntimeAssert.h"
#include "Shared/Diagnostics.h"

using namespace ecpps::abi::encoders::x8664;
using namespace ecpps::codegen::x86_64;

static std::vector<std::byte> AsmShift(const ShiftOp op, const Width width, const RegisterOperand target,
                                       const Operand& source)
{
     const auto reg = std::to_underlying(target.index);

     return std::visit(
          ecpps::OverloadedVisitor{
               [&](const IntegerOperand imm) -> std::vector<std::byte>
               {
                    return GenerateShiftRegImm(op, width, reg, static_cast<std::uint8_t>(imm.value));
               },
               [&]([[maybe_unused]] const RegisterOperand count) -> std::vector<std::byte>
               {
                    runtime_assert(count.index == RegisterIndex::Rcx,
                                   "invalid shift count register [expected CL, got a different register]");
                    return GenerateShiftRegCl(op, width, reg);
               },
               [](const StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid shift source");
               }},
          source);
}

static std::vector<std::byte> AsmShift(const ShiftOp op, const Width width, const MemoryOperand target,
                                       const Operand& source)
{
     const Memory mem = MemBase(std::to_underlying(target.relativeTo), static_cast<std::int32_t>(target.offset));

     return std::visit(
          ecpps::OverloadedVisitor{
               [&](const IntegerOperand imm) -> std::vector<std::byte>
               {
                    return GenerateShiftMemImm(op, width, mem, static_cast<std::uint8_t>(imm.value));
               },
               [&]([[maybe_unused]] const RegisterOperand count) -> std::vector<std::byte>
               {
                    runtime_assert(count.index == RegisterIndex::Rcx,
                                   "invalid shift count register [expected CL, got a different register]");
                    return GenerateShiftMemCl(op, width, mem);
               },
               [](const StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid shift source");
               }},
          source);
}

static std::vector<std::byte> EmitShift(const ShiftOp op, const ecpps::ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(ShiftInstruction), "Invalid SHIFT instruction");

     const auto& shiftInstr = *std::launder(reinterpret_cast<const ShiftInstruction*>(description.data()));
     const auto width = shiftInstr.width;
     const auto& source = shiftInstr.source;

     return std::visit(ecpps::OverloadedVisitor{[&](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmShift(op, width, reg, source);
                                                },
                                                [&](const MemoryOperand mem) -> std::vector<std::byte>
                                                {
                                                     return AsmShift(op, width, mem, source);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid shift destination");
                                                }},
                       shiftInstr.modifiedDestination);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitLeftShift(
     const ir::abstract::DynamicBytecode& description)
{
     return EmitShift(ShiftOp::Shl, description);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitRightShift(
     const ir::abstract::DynamicBytecode& description)
{
     return EmitShift(ShiftOp::Shr, description);
}
