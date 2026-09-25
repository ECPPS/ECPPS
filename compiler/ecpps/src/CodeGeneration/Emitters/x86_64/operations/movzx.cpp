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

static std::vector<std::byte> AsmMovzx(Width targetWidth, Width sourceWidth, RegisterOperand target,
                                       RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceIndex = std::to_underlying(source.index);

     return GenerateMovExtendRegToReg(ExtendKind::Zero, targetWidth, sourceWidth, targetIndex, sourceIndex);
}
static std::vector<std::byte> AsmMovzx(Width targetWidth, Width sourceWidth, RegisterOperand target,
                                       MemoryOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceOffset = static_cast<std::size_t>(source.offset);
     const auto sourceIndex = std::to_underlying(source.relativeTo);

     return GenerateMovExtendMemToReg(ExtendKind::Zero, targetWidth, sourceWidth, targetIndex,
                                      MemBase(sourceOffset, sourceIndex));
}

static std::vector<std::byte> AsmMovzx(Width targetWidth, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = source.value;

     switch (targetWidth)
     {
     case Width::W8: return GenerateMovImmToReg8(targetIndex, static_cast<std::uint8_t>(sourceValue));
     case Width::W16: return GenerateMovImmToReg16(targetIndex, static_cast<std::uint16_t>(sourceValue));
     case Width::W32: return GenerateMovImmToReg32(targetIndex, static_cast<std::uint32_t>(sourceValue));
     case Width::W64: return GenerateMovImmToReg64(targetIndex, static_cast<std::uint64_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(targetWidth)));
}
static std::vector<std::byte> AsmMovzx(Width targetWidth, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = source.value;

     switch (targetWidth)
     {
     case Width::W8: return GenerateMovImmToMem8(targetIndex, targetOffset, static_cast<std::uint8_t>(sourceValue));
     case Width::W16: return GenerateMovImmToMem16(targetIndex, targetOffset, static_cast<std::uint16_t>(sourceValue));
     case Width::W32: return GenerateMovImmToMem32(targetIndex, targetOffset, static_cast<std::uint32_t>(sourceValue));
     case Width::W64: return GenerateMovImmToMem64(targetIndex, targetOffset, static_cast<std::uint64_t>(sourceValue));
     }

     throw TracedException(std::format("Invalid instruction width: {}", std::to_underlying(targetWidth)));
}

static std::vector<std::byte> AsmMovzx(Width targetWidth, Width sourceWidth, RegisterOperand target,
                                       const Operand& source)
{
     return std::visit(
          ecpps::OverloadedVisitor{
               [targetWidth, sourceWidth, target](const RegisterOperand reg) -> std::vector<std::byte>
               {
                    return AsmMovzx(targetWidth, sourceWidth, target, reg);
               },
               [targetWidth, target](const IntegerOperand integer) -> std::vector<std::byte>
               {
                    return AsmMovzx(targetWidth, target, integer);
               },
               [targetWidth, sourceWidth, target](const MemoryOperand mem) -> std::vector<std::byte>
               {
                    return AsmMovzx(targetWidth, sourceWidth, target, mem);
               },
               [](const StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid MOVZX source");
               }},
          source);
}
static std::vector<std::byte> AsmMovzx(Width targetWidth, Width sourceWidth, MemoryOperand target,
                                       const Operand& source)
{
     return std::visit(
          ecpps::OverloadedVisitor{
               [targetWidth, sourceWidth, target](const RegisterOperand reg) -> std::vector<std::byte>
               {
                    return AsmMovzx(targetWidth, sourceWidth, target, reg);
               },
               [targetWidth, target](const IntegerOperand integer) -> std::vector<std::byte>
               {
                    return AsmMovzx(targetWidth, target, integer);
               },
               [](const StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid MOVZX source");
               }},
          source);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitMovzx(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::MovExtendInstruction),
                    "Invalid MOVZX instruction");

     const auto& mov =
          *std::launder(reinterpret_cast<const abi::encoders::x8664::MovExtendInstruction*>(description.data()));
     const auto targetWidth = mov.destinationWidth;
     const auto sourceWidth = mov.sourceWidth;

     return std::visit(
          OverloadedVisitor{
               [targetWidth, sourceWidth, source = mov.source](const RegisterOperand reg) -> std::vector<std::byte>
               {
                    return AsmMovzx(targetWidth, sourceWidth, reg, source);
               },
               [targetWidth, sourceWidth, source = mov.source](const MemoryOperand mem) -> std::vector<std::byte>
               {
                    return AsmMovzx(targetWidth, sourceWidth, mem, source);
               },
               [](const StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid MOVZX source");
               }},
          mov.destination);
}
