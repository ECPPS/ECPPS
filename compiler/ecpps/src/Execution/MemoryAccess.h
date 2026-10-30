#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include "RuntimeAssert.h"

namespace ecpps::ir
{
     enum struct MemoryOrdering : std::uint_fast8_t // I do not have atomics but this is shiny fun & fluffy
     {
          Relaxed,
          Acquire,
          Release,
          AcquireRelease,
          Sequenced
     };
     [[nodiscard]] std::string ToString(MemoryOrdering order);
     struct MemoryAccess
     {
          bool isVolatile{};
          std::optional<MemoryOrdering> atomicOrdering{}; // nullopt: not atomic
          std::uint32_t alignment{};                      // BYTES! must ^2
          std::uint32_t addressSpace{};                   // trust me I'm doing somethnig

          [[nodiscard]] std::string ToString(void) const
          {
               std::string text{};
               if (this->isVolatile) text += " volatile";
               if (this->atomicOrdering.has_value())
                    text += std::format(" atomic({})", ::ecpps::ir::ToString(*this->atomicOrdering));
               text += std::format(" align {}", this->alignment);
               if (this->addressSpace != 0) text += std::format(" as{}", this->addressSpace);
               return text;
          }
          void Verify(void) const noexcept
          {
               runtime_assert(this->alignment != 0 && (this->alignment & (this->alignment - 1)) == 0,
                              "Access alignment must be a power of two");
               // TODO: atomic guarantees stuff
          }
     };
} // namespace ecpps::ir
