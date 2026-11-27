#pragma once

#include "CodeGeneration/AbstractNodes.h"
#include "Optimiser/selector.h"

namespace ecpps::optimisations
{
     struct DeadCodePass
     {
          [[nodiscard]] PassInput Execute(PassInput input) const;
          constexpr static PassType Type(void) noexcept
          {
               return PassType::Mutating;
          }
     };

     void OptimiseVirtualInstructions(std::vector<ir::abstract::VirtualInstruction>& instructions,
                                      const ir::abstract::VirtualRegisterMap& registers, bool removeRedundantMoves,
                                      bool eliminateDeadCode);
} // namespace ecpps::optimisations
