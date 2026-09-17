#pragma once
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include "../Machine/Storage.h"
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
          std::vector<ir::abstract::VirtualInstruction> instructions;
          std::string name;
     };
} // namespace ecpps::codegen
