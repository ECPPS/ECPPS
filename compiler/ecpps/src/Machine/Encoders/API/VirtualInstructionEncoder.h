#pragma once

#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Machine.h"

namespace ecpps::abi::api
{
     struct Target;

     struct VirtualInstructionEncoder
     {
          explicit VirtualInstructionEncoder(const ecpps::abi::ISA isa, Target& target) : _target(&target), _isa(isa)
          {
          }
          virtual ~VirtualInstructionEncoder(void) = default;

          [[nodiscard]] virtual std::vector<ir::abstract::Instruction> Encode(
               const std::vector<ir::abstract::VirtualInstruction>& input) = 0;

          [[nodiscard]] constexpr ISA IsaName(void) const noexcept
          {
               return this->_isa;
          }

     protected:
          Target* _target; // TODO: non-null pointer

     private:
          ISA _isa;
     };
} // namespace ecpps::abi::api
