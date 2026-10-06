#include <cstddef>
#include <utility>
#include <variant>
#include <vector>
#include "../x86_64.h"
#include "CodeGeneration/Emitters/x86_64/Opcodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "RuntimeAssert.h"

using namespace ecpps::abi::encoders::x8664;
using namespace ecpps::codegen::x86_64;

namespace
{
     [[nodiscard]] std::size_t RegisterNumber(const RegisterOperand& reg)
     {
          return static_cast<std::size_t>(std::to_underlying(reg.index));
     }
     [[nodiscard]] Memory ToMemory(const MemoryOperand& mem)
     {
          return MemBase(static_cast<std::size_t>(std::to_underlying(mem.relativeTo)), mem.offset);
     }
} // namespace

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitCmp(const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(CmpInstruction), "Invalid CMP instruction");
     const auto& cmp = *std::launder(reinterpret_cast<const CmpInstruction*>(description.data()));

     runtime_assert(!std::holds_alternative<StackOperand>(cmp.left) && !std::holds_alternative<StackOperand>(cmp.right),
                    "CMP: unresolved stack operand");
     runtime_assert(!std::holds_alternative<StringAddressOperand>(cmp.left) &&
                         !std::holds_alternative<StringAddressOperand>(cmp.right),
                    "CMP: string address operands are not comparable");

     if (const auto* immediate = std::get_if<IntegerOperand>(&cmp.right))
     {
          runtime_assert(immediate->value <= 0x7FFFFFFFu, "CMP: immediate does not fit in a sign-extended imm32");
          const auto value = static_cast<std::int64_t>(immediate->value);

          if (const auto* left = std::get_if<RegisterOperand>(&cmp.left))
               return GenerateAluImmReg(AluOp::Cmp, cmp.width, RegisterNumber(*left), value);
          if (const auto* left = std::get_if<MemoryOperand>(&cmp.left))
               return GenerateAluImmMem(AluOp::Cmp, cmp.width, ToMemory(*left), value);
          throw TracedException("CMP: left operand cannot be an immediate");
     }

     if (const auto* right = std::get_if<RegisterOperand>(&cmp.right))
     {
          if (const auto* left = std::get_if<RegisterOperand>(&cmp.left))
               return GenerateAluRegReg(AluOp::Cmp, cmp.width, RegisterNumber(*left), RegisterNumber(*right));
          if (const auto* left = std::get_if<MemoryOperand>(&cmp.left))
               return GenerateAluRegMem(AluOp::Cmp, cmp.width, ToMemory(*left), RegisterNumber(*right)); // cmp [m], r
          throw TracedException("CMP: left operand cannot be an immediate");
     }

     if (const auto* right = std::get_if<MemoryOperand>(&cmp.right))
     {
          if (const auto* left = std::get_if<RegisterOperand>(&cmp.left))
               return GenerateAluMemReg(AluOp::Cmp, cmp.width, RegisterNumber(*left), ToMemory(*right)); // cmp r, [m]
          throw TracedException("CMP: memory-to-memory comparison is not encodable");
     }

     throw TracedException("CMP: unsupported operand combination");
}
