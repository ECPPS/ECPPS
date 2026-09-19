#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "CodeGeneration/AbstractNodes.h"

namespace ecpps::codegen
{

     enum struct InstructionAlignment : std::uint_fast8_t
     {
          None,
          Aligned,
          Unaligned
     };

     struct Routine
     {
          std::vector<ir::abstract::VirtualInstruction> virtualInstructions;
          std::string name;

          std::vector<ir::abstract::Instruction> physicalInstructions;
     };
} // namespace ecpps::codegen
