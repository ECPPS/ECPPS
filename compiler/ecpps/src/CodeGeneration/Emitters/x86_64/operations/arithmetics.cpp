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

static std::vector<std::byte> AsmAdd(Width width, RegisterOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceIndex = std::to_underlying(source.index);

     switch (width)
     {
     case Width::W8: return GenerateAddRegToReg8(targetIndex, sourceIndex);
     case Width::W16: return GenerateAddRegToReg16(targetIndex, sourceIndex);
     case Width::W32: return GenerateAddRegToReg32(targetIndex, sourceIndex);
     case Width::W64: return GenerateAddRegToReg64(targetIndex, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}
static std::vector<std::byte> AsmAdd(Width width, MemoryOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceIndex = std::to_underlying(source.index);

     switch (width)
     {
     case Width::W8: return GenerateAddRegToMem8(targetIndex, targetOffset, sourceIndex);
     case Width::W16: return GenerateAddRegToMem16(targetIndex, targetOffset, sourceIndex);
     case Width::W32: return GenerateAddRegToMem32(targetIndex, targetOffset, sourceIndex);
     case Width::W64: return GenerateAddRegToMem64(targetIndex, targetOffset, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}
static std::vector<std::byte> AsmAdd(Width width, RegisterOperand target, MemoryOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceOffset = static_cast<std::size_t>(source.offset);
     const auto sourceIndex = std::to_underlying(source.relativeTo);

     switch (width)
     {
     case Width::W8: return GenerateAddRegToMem8(targetIndex, sourceOffset, sourceIndex);
     case Width::W16: return GenerateAddRegToMem16(targetIndex, sourceOffset, sourceIndex);
     case Width::W32: return GenerateAddRegToMem32(targetIndex, sourceOffset, sourceIndex);
     case Width::W64: return GenerateAddRegToMem64(targetIndex, sourceOffset, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmAdd(Width width, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = source.value;

     switch (width)
     {
     case Width::W8: return GenerateAddImmToReg8(targetIndex, static_cast<std::uint8_t>(sourceValue));
     case Width::W16: return GenerateAddImmToReg16(targetIndex, static_cast<std::uint16_t>(sourceValue));
     case Width::W32: return GenerateAddImmToReg32(targetIndex, static_cast<std::uint32_t>(sourceValue));
     case Width::W64: return GenerateAddImmToReg64(targetIndex, static_cast<std::uint64_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}
static std::vector<std::byte> AsmAdd(Width width, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = source.value;

     // TODO: 64-bit values split across 2 MOVs or 1 special MOV
     switch (width)
     {
     case Width::W8: return GenerateAddImmToMem8(targetIndex, targetOffset, static_cast<std::uint8_t>(sourceValue));
     case Width::W16: return GenerateAddImmToMem16(targetIndex, targetOffset, static_cast<std::uint16_t>(sourceValue));
     case Width::W32: return GenerateAddImmToMem32(targetIndex, targetOffset, static_cast<std::uint32_t>(sourceValue));
     case Width::W64: return GenerateAddImmToMem64(targetIndex, targetOffset, static_cast<std::uint32_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmAdd(Width width, RegisterOperand target, const Operand& source)
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
static std::vector<std::byte> AsmAdd(Width width, MemoryOperand target, const Operand& source)
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
     const auto instructionWidth = mov.width;

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

static std::vector<std::byte> AsmSub(Width width, RegisterOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceIndex = std::to_underlying(source.index);

     switch (width)
     {
     case Width::W8: return GenerateSubRegToReg8(targetIndex, sourceIndex);
     case Width::W16: return GenerateSubRegToReg16(targetIndex, sourceIndex);
     case Width::W32: return GenerateSubRegToReg32(targetIndex, sourceIndex);
     case Width::W64: return GenerateSubRegToReg64(targetIndex, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}
static std::vector<std::byte> AsmSub(Width width, MemoryOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceIndex = std::to_underlying(source.index);

     switch (width)
     {
     case Width::W8: return GenerateSubRegToMem8(targetIndex, targetOffset, sourceIndex);
     case Width::W16: return GenerateSubRegToMem16(targetIndex, targetOffset, sourceIndex);
     case Width::W32: return GenerateSubRegToMem32(targetIndex, targetOffset, sourceIndex);
     case Width::W64: return GenerateSubRegToMem64(targetIndex, targetOffset, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}
static std::vector<std::byte> AsmSub(Width width, RegisterOperand target, MemoryOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceOffset = static_cast<std::size_t>(source.offset);
     const auto sourceIndex = std::to_underlying(source.relativeTo);

     switch (width)
     {
     case Width::W8: return GenerateSubRegToMem8(targetIndex, sourceOffset, sourceIndex);
     case Width::W16: return GenerateSubRegToMem16(targetIndex, sourceOffset, sourceIndex);
     case Width::W32: return GenerateSubRegToMem32(targetIndex, sourceOffset, sourceIndex);
     case Width::W64: return GenerateSubRegToMem64(targetIndex, sourceOffset, sourceIndex);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmSub(Width width, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = source.value;

     switch (width)
     {
     case Width::W8: return GenerateSubImmToReg8(targetIndex, static_cast<std::uint8_t>(sourceValue));
     case Width::W16: return GenerateSubImmToReg16(targetIndex, static_cast<std::uint16_t>(sourceValue));
     case Width::W32: return GenerateSubImmToReg32(targetIndex, static_cast<std::uint32_t>(sourceValue));
     case Width::W64: return GenerateSubImmToReg64(targetIndex, static_cast<std::uint64_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}
static std::vector<std::byte> AsmSub(Width width, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = source.value;

     // TODO: 64-bit values split across 2 MOVs or 1 special MOV
     switch (width)
     {
     case Width::W8: return GenerateSubImmToMem8(targetIndex, targetOffset, static_cast<std::uint8_t>(sourceValue));
     case Width::W16: return GenerateSubImmToMem16(targetIndex, targetOffset, static_cast<std::uint16_t>(sourceValue));
     case Width::W32: return GenerateSubImmToMem32(targetIndex, targetOffset, static_cast<std::uint32_t>(sourceValue));
     case Width::W64: return GenerateSubImmToMem64(targetIndex, targetOffset, static_cast<std::uint32_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmSub(Width width, RegisterOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmSub(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmSub(width, target, integer);
                                                },
                                                [width, target](const MemoryOperand mem) -> std::vector<std::byte>
                                                {
                                                     return AsmSub(width, target, mem);
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid MOV source");
                                                }},
                       source);
}
static std::vector<std::byte> AsmSub(Width width, MemoryOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmSub(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmSub(width, target, integer);
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid MOV source");
                                                }},
                       source);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitSub(const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::SubInstruction), "Invalid MOV instruction");

     const auto& mov = *std::launder(reinterpret_cast<const abi::encoders::x8664::SubInstruction*>(description.data()));
     const auto instructionWidth = mov.width;

     return std::visit(
          OverloadedVisitor{[instructionWidth, source = mov.source](const RegisterOperand reg) -> std::vector<std::byte>
                            {
                                 return AsmSub(instructionWidth, reg, source);
                            },
                            [instructionWidth, source = mov.source](const MemoryOperand mem) -> std::vector<std::byte>
                            {
                                 return AsmSub(instructionWidth, mem, source);
                            },
                            [](auto&&...) -> std::vector<std::byte>
                            {
                                 throw TracedException("invalid MOV source");
                            }},
          mov.modifiedDestination);
}
