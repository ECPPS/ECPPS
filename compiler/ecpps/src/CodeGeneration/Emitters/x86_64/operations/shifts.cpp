#include <cstddef>
#include <cstdint>
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

static std::vector<std::byte> AsmLeftShift(Width width, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = static_cast<std::uint8_t>(source.value);

     switch (width)
     {
     case Width::W8: return GenerateSalReg8(targetIndex, sourceValue);
     case Width::W16: return GenerateSalReg16(targetIndex, sourceValue);
     case Width::W32: return GenerateSalReg32(targetIndex, sourceValue);
     case Width::W64: return GenerateSalReg64(targetIndex, sourceValue);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmLeftShift(Width width, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = static_cast<std::uint8_t>(source.value);

     switch (width)
     {
     case Width::W8: return GenerateSalMem8(targetIndex, targetOffset, sourceValue);
     case Width::W16: return GenerateSalMem16(targetIndex, targetOffset, sourceValue);
     case Width::W32: return GenerateSalMem32(targetIndex, targetOffset, sourceValue);
     case Width::W64: return GenerateSalMem64(targetIndex, targetOffset, sourceValue);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmLeftShift(Width width, RegisterOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmLeftShift(width, target, integer);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid SHL source");
                                                }},
                       source);
}

static std::vector<std::byte> AsmLeftShift(Width width, MemoryOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmLeftShift(width, target, integer);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid SHL source");
                                                }},
                       source);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitLeftShift(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::ShiftInstruction),
                    "Invalid LEFT-SHIFT instruction");

     const auto& shiftInstr =
          *std::launder(reinterpret_cast<const abi::encoders::x8664::ShiftInstruction*>(description.data()));
     const auto instructionWidth = shiftInstr.width;

     return std::visit(
          OverloadedVisitor{
               [instructionWidth, source = shiftInstr.source](const RegisterOperand reg) -> std::vector<std::byte>
               {
                    return AsmLeftShift(instructionWidth, reg, source);
               },
               [instructionWidth, source = shiftInstr.source](const MemoryOperand mem) -> std::vector<std::byte>
               {
                    return AsmLeftShift(instructionWidth, mem, source);
               },
               [](const StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid SHL destination");
               }},
          shiftInstr.modifiedDestination);
}

static std::vector<std::byte> AsmRightShift(Width width, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = static_cast<std::uint8_t>(source.value);

     switch (width)
     {
     case Width::W8: return GenerateSarReg8(targetIndex, sourceValue);
     case Width::W16: return GenerateSarReg16(targetIndex, sourceValue);
     case Width::W32: return GenerateSarReg32(targetIndex, sourceValue);
     case Width::W64: return GenerateSarReg64(targetIndex, sourceValue);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmRightShift(Width width, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = static_cast<std::uint8_t>(source.value);

     switch (width)
     {
     case Width::W8: return GenerateSarMem8(targetIndex, targetOffset, sourceValue);
     case Width::W16: return GenerateSarMem16(targetIndex, targetOffset, sourceValue);
     case Width::W32: return GenerateSarMem32(targetIndex, targetOffset, sourceValue);
     case Width::W64: return GenerateSarMem64(targetIndex, targetOffset, sourceValue);
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(width)));
}

static std::vector<std::byte> AsmRightShift(Width width, RegisterOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmRightShift(width, target, integer);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid SAR source");
                                                }},
                       source);
}

static std::vector<std::byte> AsmRightShift(Width width, MemoryOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmRightShift(width, target, integer);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid SAR source");
                                                }},
                       source);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitRightShift(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::ShiftInstruction),
                    "Invalid RIGHT-SHIFT instruction");

     const auto& shiftInstr =
          *std::launder(reinterpret_cast<const abi::encoders::x8664::ShiftInstruction*>(description.data()));
     const auto instructionWidth = shiftInstr.width;

     return std::visit(
          OverloadedVisitor{
               [instructionWidth, source = shiftInstr.source](const RegisterOperand reg) -> std::vector<std::byte>
               {
                    return AsmRightShift(instructionWidth, reg, source);
               },
               [instructionWidth, source = shiftInstr.source](const MemoryOperand mem) -> std::vector<std::byte>
               {
                    return AsmRightShift(instructionWidth, mem, source);
               },
               [](const StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid SAR destination");
               }},
          shiftInstr.modifiedDestination);
}
