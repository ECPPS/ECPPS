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

static std::vector<std::byte> AsmCopy(Width width, RegisterOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceIndex = std::to_underlying(source.index);

     switch (width)
     {
     case Width::W8: return GenerateMovRegToReg8(targetIndex, sourceIndex);
     case Width::W16: return GenerateMovRegToReg16(targetIndex, sourceIndex);
     case Width::W32: return GenerateMovRegToReg32(targetIndex, sourceIndex);
     case Width::W64: return GenerateMovRegToReg64(targetIndex, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}
static std::vector<std::byte> AsmCopy(Width width, MemoryOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceIndex = std::to_underlying(source.index);

     switch (width)
     {
     case Width::W8: return GenerateMovRegToMem8(targetIndex, targetOffset, sourceIndex);
     case Width::W16: return GenerateMovRegToMem16(targetIndex, targetOffset, sourceIndex);
     case Width::W32: return GenerateMovRegToMem32(targetIndex, targetOffset, sourceIndex);
     case Width::W64: return GenerateMovRegToMem64(targetIndex, targetOffset, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}
static std::vector<std::byte> AsmCopy(Width width, RegisterOperand target, MemoryOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceOffset = static_cast<std::size_t>(source.offset);
     const auto sourceIndex = std::to_underlying(source.relativeTo);

     switch (width)
     {
     case Width::W8: return GenerateMovMemToReg8(targetIndex, sourceOffset, sourceIndex);
     case Width::W16: return GenerateMovMemToReg16(targetIndex, sourceOffset, sourceIndex);
     case Width::W32: return GenerateMovMemToReg32(targetIndex, sourceOffset, sourceIndex);
     case Width::W64: return GenerateMovMemToReg64(targetIndex, sourceOffset, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmCopy(Width width, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = source.value;

     switch (width)
     {
     case Width::W8: return GenerateMovImmToReg8(targetIndex, static_cast<std::uint8_t>(sourceValue));
     case Width::W16: return GenerateMovImmToReg16(targetIndex, static_cast<std::uint16_t>(sourceValue));
     case Width::W32: return GenerateMovImmToReg32(targetIndex, static_cast<std::uint32_t>(sourceValue));
     case Width::W64: return GenerateMovImmToReg64(targetIndex, static_cast<std::uint64_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}
static std::vector<std::byte> AsmCopy(Width width, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = source.value;

     switch (width)
     {
     case Width::W8: return GenerateMovImmToMem8(targetIndex, targetOffset, static_cast<std::uint8_t>(sourceValue));
     case Width::W16: return GenerateMovImmToMem16(targetIndex, targetOffset, static_cast<std::uint16_t>(sourceValue));
     case Width::W32: return GenerateMovImmToMem32(targetIndex, targetOffset, static_cast<std::uint32_t>(sourceValue));
     case Width::W64: return GenerateMovImmToMem64(targetIndex, targetOffset, static_cast<std::uint64_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmCopy(Width width, RegisterOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmCopy(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmCopy(width, target, integer);
                                                },
                                                [width, target](const MemoryOperand mem) -> std::vector<std::byte>
                                                {
                                                     return AsmCopy(width, target, mem);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid MOV source");
                                                }},
                       source);
}
static std::vector<std::byte> AsmCopy(Width width, MemoryOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmCopy(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmCopy(width, target, integer);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid MOV source");
                                                }},
                       source);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitMov(const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::MovInstruction), "Invalid MOV instruction");

     const auto& mov = *std::launder(reinterpret_cast<const abi::encoders::x8664::MovInstruction*>(description.data()));
     const auto instructionWidth = mov.width;

     return std::visit(
          OverloadedVisitor{[instructionWidth, source = mov.source](const RegisterOperand reg) -> std::vector<std::byte>
                            {
                                 return AsmCopy(instructionWidth, reg, source);
                            },
                            [instructionWidth, source = mov.source](const MemoryOperand mem) -> std::vector<std::byte>
                            {
                                 return AsmCopy(instructionWidth, mem, source);
                            },
                            [](const StackOperand&) -> std::vector<std::byte>
                            {
                                 throw TracedException("unresolved StackOperand: Finalise must run before emission");
                            },
                            [](auto&&...) -> std::vector<std::byte>
                            {
                                 throw TracedException("invalid MOV source");
                            }},
          mov.destination);
}
