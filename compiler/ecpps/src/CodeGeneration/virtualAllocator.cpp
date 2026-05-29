#include <unordered_map>
#include "CodeGeneration/Nodes.h"
#include "PseudoAssembly.h"
#include "Shared/Diagnostics.h"

using ecpps::codegen::ParsingContext;

struct ecpps::codegen::ParsingContext::VirtualRegisterAllocator
{
     struct Register
     {
          const ir::SingleAssignRegisterNode* ssa;
          std::size_t width;
     };
     std::vector<Register> map{};
     std::size_t nextFreeIndex{};
};

ParsingContext::ParsingContext(std::vector<Instruction>& instructions, ecpps::abi::ABI& abi)
    : instructions(&instructions), abi(&abi), allocator(new VirtualRegisterAllocator())
{
}

ecpps::codegen::VirtualRegisterOperand ParsingContext::GetVirtualFromUnbounded(
    const ir::SingleAssignRegisterNode& reg) const
{
     std::size_t index{};
     for (const auto mapRegister : allocator->map)
     {
          if (mapRegister.ssa == &reg) return VirtualRegisterOperand{index, mapRegister.width};
          index++;
     }
     throw TracedException("Invalid register access");
}
void ParsingContext::RegisterUnboundedToVirtual(const ir::SingleAssignRegisterNode& reg, std::size_t width)
{
     for (auto& mapRegister : allocator->map)
     {
          if (mapRegister.ssa != nullptr && mapRegister.ssa->UseCount() > 0) continue;
          mapRegister.ssa = &reg;
          break;
     }
     allocator->map.emplace_back(&reg, width);
}
