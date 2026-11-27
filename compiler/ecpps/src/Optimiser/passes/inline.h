#pragma once

#include "Optimiser/selector.h"
namespace ecpps::optimisations
{
     struct InlinePass
     {
          explicit InlinePass(BumpAllocator& allocator) noexcept : _allocator(&allocator)
          {
          }

          [[nodiscard]] PassInput Execute(PassInput input) const;
          constexpr static PassType Type(void) noexcept
          {
               return PassType::Mutating;
          }

     private:
          BumpAllocator* _allocator;
     };
} // namespace ecpps::optimisations
