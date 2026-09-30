#include <format>
#include <ranges>
#include <utility>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "RuntimeAssert.h"

std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     PrepareForCall(void)
{
     std::vector<ir::abstract::Instruction> built{};
     const auto& platform = *this->_target->platform;

     for (const auto& cell : this->_registerAllocator.Snapshot())
     {
          if (!cell.owner.has_value()) continue;
          if (platform.IsCalleeSaved(std::to_underlying(cell.reg))) continue;

          const auto uses = this->_remainingUses.find(cell.owner->index);
          if (uses == this->_remainingUses.end() || uses->second == 0)
          {
               this->_registerAllocator.Free(*cell.owner);
               this->GetVRM().ClearMaterialisation(*cell.owner);
               continue;
          }
          built.append_range(this->Evict(*cell.owner));
     }
     return built;
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::ReleaseArgumentRegisters(void)
{
     const auto& platform = *this->_target->platform;
     for (const auto i : std::views::iota(0uz, platform.IntegerParameterRegisterCount()))
          this->Unlock(static_cast<RegisterIndex>(platform.IntegerParameterRegisterIndex(i)));
}

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Call>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     runtime_assert(registerArray.size() == 1,
                    std::format("Invalid register array! Specified: {}, expected: [index]", registerArray.size()));

     auto built = this->PrepareForCall();
     built.push_back(BuildCall(registerArray[0].index));
     this->ReleaseArgumentRegisters();
     return built;
}
