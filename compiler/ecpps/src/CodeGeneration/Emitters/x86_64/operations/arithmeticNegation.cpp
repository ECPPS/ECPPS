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

static std::vector<std::byte> AsmBitwiseNeg(Width width, RegisterOperand operand)
{
     const auto operandIndex = std::to_underlying(operand.index);

     return GenerateUnaryReg(UnaryOp::Neg, width, operandIndex);
}

static std::vector<std::byte> AsmBitwiseNeg(Width width, MemoryOperand operand)
{
     const auto baseIndex = std::to_underlying(operand.relativeTo);
     const auto offset = static_cast<std::int32_t>(operand.offset);

     return GenerateUnaryMem(UnaryOp::Neg, width, MemBase(baseIndex, offset));
}

static std::vector<std::byte> AsmBitwiseNeg(Width width, const Operand& operand)
{
     return std::visit(
          ecpps::OverloadedVisitor{
               [width](const RegisterOperand reg) -> std::vector<std::byte>
               {
                    return AsmBitwiseNeg(width, reg);
               },
               [width](const MemoryOperand mem) -> std::vector<std::byte>
               {
                    return AsmBitwiseNeg(width, mem);
               },
               [](const StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](const IntegerOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("invalid NOT operand: x86-64 NOT does not accept an immediate operand");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid NOT operand");
               }},
          operand);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitArithmeticNegation(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::ArithmeticNegationInstruction),
                    "Invalid binary complement instruction");

     const auto& neg = *std::launder(
          reinterpret_cast<const abi::encoders::x8664::ArithmeticNegationInstruction*>(description.data()));

     return AsmBitwiseNeg(neg.width, neg.modifiedOperand);
}
