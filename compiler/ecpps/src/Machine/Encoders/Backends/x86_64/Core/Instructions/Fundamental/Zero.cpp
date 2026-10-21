#include <cstddef>
#include <format>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/MemoryOperations.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::ZeroMemory>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     runtime_assert(
          registerArray.size() == 2,
          std::format("Invalid register array! Specified: {}, expected: [target, size]", registerArray.size()));

     std::vector<ir::abstract::Instruction> out{};
     const std::size_t size = registerArray[1].index;
     if (size == 0) return out;

     this->EmitMemoryZero(out, MemoryLocation{.base = this->EnsureStackSlot(registerArray[0])}, size);
     return out;
}

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyFromData>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     runtime_assert(registerArray.size() == 4,
                    std::format("Invalid register array! Specified: {}, expected: [target, table, offset, size]",
                                registerArray.size()));

     std::vector<ir::abstract::Instruction> out{};
     const std::size_t size = registerArray[3].index;
     if (size == 0) return out;

     const StringAddressOperand string{
          .tableIndex = static_cast<decltype(StringAddressOperand::tableIndex)>(registerArray[1].index),
          .offset = static_cast<decltype(StringAddressOperand::offset)>(registerArray[2].index)};

     ScratchRegister pointer{this->_registerAllocator};
     out.push_back(BuildLea(Width::W64, pointer.AsOperand(), string));

     this->EmitMemoryCopy(
          out, MemoryLocation{.base = this->EnsureStackSlot(registerArray[0])},
          MemoryLocation{.base = MemoryOperand{.relativeTo = pointer.Register(), .offset = 0}, .disposable = true},
          size);
     return out;
}
