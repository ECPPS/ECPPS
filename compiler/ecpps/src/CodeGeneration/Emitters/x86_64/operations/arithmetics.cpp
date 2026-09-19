
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

static std::vector<std::byte> AsmAdd(std::size_t width, RegisterOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceIndex = std::to_underlying(source.index);

     switch (width)
     {
     case 8: return GenerateAddRegToReg8(targetIndex, sourceIndex);
     case 16: return GenerateAddRegToReg16(targetIndex, sourceIndex);
     case 32: return GenerateAddRegToReg32(targetIndex, sourceIndex);
     case 64: return GenerateAddRegToReg64(targetIndex, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", width));
}
static std::vector<std::byte> AsmAdd(std::size_t width, MemoryOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceIndex = std::to_underlying(source.index);

     switch (width)
     {
     case 8: return GenerateAddRegToMem8(targetIndex, targetOffset, sourceIndex);
     case 16: return GenerateAddRegToMem16(targetIndex, targetOffset, sourceIndex);
     case 32: return GenerateAddRegToMem32(targetIndex, targetOffset, sourceIndex);
     case 64: return GenerateAddRegToMem64(targetIndex, targetOffset, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", width));
}
static std::vector<std::byte> AsmAdd(std::size_t width, RegisterOperand target, MemoryOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceOffset = static_cast<std::size_t>(source.offset);
     const auto sourceIndex = std::to_underlying(source.relativeTo);

     switch (width)
     {
     case 8: return GenerateAddRegToMem8(targetIndex, sourceOffset, sourceIndex);
     case 16: return GenerateAddRegToMem16(targetIndex, sourceOffset, sourceIndex);
     case 32: return GenerateAddRegToMem32(targetIndex, sourceOffset, sourceIndex);
     case 64: return GenerateAddRegToMem64(targetIndex, sourceOffset, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", width));
}

static std::vector<std::byte> AsmAdd(std::size_t width, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = source.value;

     switch (width)
     {
     case 8: return GenerateAddImmToReg8(targetIndex, static_cast<std::uint8_t>(sourceValue));
     case 16: return GenerateAddImmToReg16(targetIndex, static_cast<std::uint16_t>(sourceValue));
     case 32: return GenerateAddImmToReg32(targetIndex, static_cast<std::uint32_t>(sourceValue));
     case 64: return GenerateAddImmToReg64(targetIndex, static_cast<std::uint64_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", width));
}
static std::vector<std::byte> AsmAdd(std::size_t width, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = source.value;

     // TODO: 64-bit values split across 2 MOVs or 1 special MOV
     switch (width)
     {
     case 8: return GenerateAddImmToMem8(targetIndex, targetOffset, static_cast<std::uint8_t>(sourceValue));
     case 16: return GenerateAddImmToMem16(targetIndex, targetOffset, static_cast<std::uint16_t>(sourceValue));
     case 32: return GenerateAddImmToMem32(targetIndex, targetOffset, static_cast<std::uint32_t>(sourceValue));
     case 64: return GenerateAddImmToMem64(targetIndex, targetOffset, static_cast<std::uint32_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", width));
}

static std::vector<std::byte> AsmAdd(std::size_t width, RegisterOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmAdd(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmAdd(width, target, integer);
                                                },
                                                [width, target](const MemoryOperand mem) -> std::vector<std::byte>
                                                {
                                                     return AsmAdd(width, target, mem);
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid MOV source");
                                                }},
                       source);
}
static std::vector<std::byte> AsmAdd(std::size_t width, MemoryOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmAdd(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmAdd(width, target, integer);
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid MOV source");
                                                }},
                       source);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitAdd(const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::AddInstruction), "Invalid MOV instruction");

     const auto& mov = *std::launder(reinterpret_cast<const abi::encoders::x8664::AddInstruction*>(description.data()));
     std::size_t instructionWidth = 32; // TODO: some kind of typesystem

     return std::visit(
          OverloadedVisitor{[instructionWidth, source = mov.source](const RegisterOperand reg) -> std::vector<std::byte>
                            {
                                 return AsmAdd(instructionWidth, reg, source);
                            },
                            [instructionWidth, source = mov.source](const MemoryOperand mem) -> std::vector<std::byte>
                            {
                                 return AsmAdd(instructionWidth, mem, source);
                            },
                            [](auto&&...) -> std::vector<std::byte>
                            {
                                 throw TracedException("invalid MOV source");
                            }},
          mov.modifiedDestination);
}
