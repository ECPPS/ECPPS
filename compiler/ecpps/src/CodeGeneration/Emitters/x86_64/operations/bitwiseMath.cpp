#include <cstddef>
#include <cstdint>
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

static std::vector<std::byte> AsmBinaryOr(Width width, RegisterOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceIndex = std::to_underlying(source.index);

     return GenerateAluRegReg(AluOp::Or, width, targetIndex, sourceIndex);
}
static std::vector<std::byte> AsmBinaryOr(Width width, MemoryOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceIndex = std::to_underlying(source.index);

     return GenerateAluRegMem(AluOp::Or, width, MemBase(targetIndex, static_cast<std::int32_t>(targetOffset)),
                              sourceIndex);
}
static std::vector<std::byte> AsmBinaryOr(Width width, RegisterOperand target, MemoryOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceOffset = static_cast<std::size_t>(source.offset);
     const auto sourceIndex = std::to_underlying(source.relativeTo);

     return GenerateAluMemReg(AluOp::Or, width, targetIndex,
                              MemBase(sourceIndex, static_cast<std::int32_t>(sourceOffset)));
}

static std::vector<std::byte> AsmBinaryOr(Width width, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = source.value;

     return GenerateAluImmReg(AluOp::Or, width, targetIndex, static_cast<std::int64_t>(sourceValue));
}
static std::vector<std::byte> AsmBinaryOr(Width width, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = source.value;

     return GenerateAluImmMem(AluOp::Or, width, MemBase(targetIndex, static_cast<std::int32_t>(targetOffset)),
                              static_cast<std::int64_t>(sourceValue));
}

static std::vector<std::byte> AsmBinaryOr(Width width, RegisterOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryOr(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryOr(width, target, integer);
                                                },
                                                [width, target](const MemoryOperand mem) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryOr(width, target, mem);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid ADD source");
                                                }},
                       source);
}
static std::vector<std::byte> AsmBinaryOr(Width width, MemoryOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryOr(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryOr(width, target, integer);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid ADD source");
                                                }},
                       source);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitBinaryOr(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::BinaryOrInstruction), "Invalid OR instruction");

     const auto& bin =
          *std::launder(reinterpret_cast<const abi::encoders::x8664::BinaryOrInstruction*>(description.data()));
     const auto instructionWidth = bin.width;

     return std::visit(
          OverloadedVisitor{[instructionWidth, source = bin.source](const RegisterOperand reg) -> std::vector<std::byte>
                            {
                                 return AsmBinaryOr(instructionWidth, reg, source);
                            },
                            [instructionWidth, source = bin.source](const MemoryOperand mem) -> std::vector<std::byte>
                            {
                                 return AsmBinaryOr(instructionWidth, mem, source);
                            },
                            [](const StackOperand&) -> std::vector<std::byte>
                            {
                                 throw TracedException("unresolved StackOperand: Finalise must run before emission");
                            },
                            [](auto&&...) -> std::vector<std::byte>
                            {
                                 throw TracedException("invalid ADD source");
                            }},
          bin.modifiedDestination);
}

static std::vector<std::byte> AsmBinaryAnd(Width width, RegisterOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceIndex = std::to_underlying(source.index);

     return GenerateAluRegReg(AluOp::And, width, targetIndex, sourceIndex);
}
static std::vector<std::byte> AsmBinaryAnd(Width width, MemoryOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceIndex = std::to_underlying(source.index);

     return GenerateAluRegMem(AluOp::And, width, MemBase(targetIndex, static_cast<std::int32_t>(targetOffset)),
                              sourceIndex);
}
static std::vector<std::byte> AsmBinaryAnd(Width width, RegisterOperand target, MemoryOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceOffset = static_cast<std::size_t>(source.offset);
     const auto sourceIndex = std::to_underlying(source.relativeTo);

     return GenerateAluMemReg(AluOp::And, width, targetIndex,
                              MemBase(sourceIndex, static_cast<std::int32_t>(sourceOffset)));
}

static std::vector<std::byte> AsmBinaryAnd(Width width, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = source.value;

     return GenerateAluImmReg(AluOp::And, width, targetIndex, static_cast<std::int64_t>(sourceValue));
}
static std::vector<std::byte> AsmBinaryAnd(Width width, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = source.value;

     return GenerateAluImmMem(AluOp::And, width, MemBase(targetIndex, static_cast<std::int32_t>(targetOffset)),
                              static_cast<std::int64_t>(sourceValue));
}

static std::vector<std::byte> AsmBinaryAnd(Width width, RegisterOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryAnd(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryAnd(width, target, integer);
                                                },
                                                [width, target](const MemoryOperand mem) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryAnd(width, target, mem);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid ADD source");
                                                }},
                       source);
}
static std::vector<std::byte> AsmBinaryAnd(Width width, MemoryOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryAnd(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryAnd(width, target, integer);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid OR source");
                                                }},
                       source);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitBinaryAnd(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::BinaryAndInstruction),
                    "Invalid AND instruction");

     const auto& bin =
          *std::launder(reinterpret_cast<const abi::encoders::x8664::BinaryAndInstruction*>(description.data()));
     const auto instructionWidth = bin.width;

     return std::visit(
          OverloadedVisitor{[instructionWidth, source = bin.source](const RegisterOperand reg) -> std::vector<std::byte>
                            {
                                 return AsmBinaryAnd(instructionWidth, reg, source);
                            },
                            [instructionWidth, source = bin.source](const MemoryOperand mem) -> std::vector<std::byte>
                            {
                                 return AsmBinaryAnd(instructionWidth, mem, source);
                            },
                            [](const StackOperand&) -> std::vector<std::byte>
                            {
                                 throw TracedException("unresolved StackOperand: Finalise must run before emission");
                            },
                            [](auto&&...) -> std::vector<std::byte>
                            {
                                 throw TracedException("invalid ADD source");
                            }},
          bin.modifiedDestination);
}

static std::vector<std::byte> AsmBinaryXor(Width width, RegisterOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceIndex = std::to_underlying(source.index);

     return GenerateAluRegReg(AluOp::Xor, width, targetIndex, sourceIndex);
}
static std::vector<std::byte> AsmBinaryXor(Width width, MemoryOperand target, RegisterOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceIndex = std::to_underlying(source.index);

     return GenerateAluRegMem(AluOp::Xor, width, MemBase(targetIndex, static_cast<std::int32_t>(targetOffset)),
                              sourceIndex);
}
static std::vector<std::byte> AsmBinaryXor(Width width, RegisterOperand target, MemoryOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceOffset = static_cast<std::size_t>(source.offset);
     const auto sourceIndex = std::to_underlying(source.relativeTo);

     return GenerateAluMemReg(AluOp::Xor, width, targetIndex,
                              MemBase(sourceIndex, static_cast<std::int32_t>(sourceOffset)));
}

static std::vector<std::byte> AsmBinaryXor(Width width, RegisterOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.index);
     const auto sourceValue = source.value;

     return GenerateAluImmReg(AluOp::Xor, width, targetIndex, static_cast<std::int64_t>(sourceValue));
}
static std::vector<std::byte> AsmBinaryXor(Width width, MemoryOperand target, IntegerOperand source)
{
     const auto targetIndex = std::to_underlying(target.relativeTo);
     const auto targetOffset = static_cast<std::size_t>(target.offset);
     const auto sourceValue = source.value;

     return GenerateAluImmMem(AluOp::Xor, width, MemBase(targetIndex, static_cast<std::int32_t>(targetOffset)),
                              static_cast<std::int64_t>(sourceValue));
}

static std::vector<std::byte> AsmBinaryXor(Width width, RegisterOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryXor(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryXor(width, target, integer);
                                                },
                                                [width, target](const MemoryOperand mem) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryXor(width, target, mem);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid ADD source");
                                                }},
                       source);
}
static std::vector<std::byte> AsmBinaryXor(Width width, MemoryOperand target, const Operand& source)
{
     return std::visit(ecpps::OverloadedVisitor{[width, target](const RegisterOperand reg) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryXor(width, target, reg);
                                                },
                                                [width, target](const IntegerOperand integer) -> std::vector<std::byte>
                                                {
                                                     return AsmBinaryXor(width, target, integer);
                                                },
                                                [](const StackOperand&) -> std::vector<std::byte>
                                                {
                                                     throw TracedException(
                                                          "unresolved StackOperand: Finalise must run before emission");
                                                },
                                                [](auto&&...) -> std::vector<std::byte>
                                                {
                                                     throw TracedException("invalid ADD source");
                                                }},
                       source);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitBinaryXor(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::BinaryXorInstruction),
                    "Invalid XOR instruction");

     const auto& bin =
          *std::launder(reinterpret_cast<const abi::encoders::x8664::BinaryXorInstruction*>(description.data()));
     const auto instructionWidth = bin.width;

     return std::visit(
          OverloadedVisitor{[instructionWidth, source = bin.source](const RegisterOperand reg) -> std::vector<std::byte>
                            {
                                 return AsmBinaryXor(instructionWidth, reg, source);
                            },
                            [instructionWidth, source = bin.source](const MemoryOperand mem) -> std::vector<std::byte>
                            {
                                 return AsmBinaryXor(instructionWidth, mem, source);
                            },
                            [](const StackOperand&) -> std::vector<std::byte>
                            {
                                 throw TracedException("unresolved StackOperand: Finalise must run before emission");
                            },
                            [](auto&&...) -> std::vector<std::byte>
                            {
                                 throw TracedException("invalid ADD source");
                            }},
          bin.modifiedDestination);
}
