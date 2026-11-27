#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>
#include "Execution/NodeBase.h"
#include "Shared/Config.h"

namespace ecpps::optimisations
{
     enum struct PassType : std::uint8_t
     {
          Mutating,
          Reconstructive
     };
     using PassInput = std::vector<ir::NodePointer>;
     template <typename T>
     concept PassRun = requires(const T& o) {
          { o.Execute(std::declval<PassInput>()) } -> std::same_as<PassInput>;
          { std::remove_cvref_t<T>::Type() } -> std::same_as<PassType>;
     };
     struct Pass
     {
          explicit Pass(PassRun auto&& run)
              : _run(
                     [self = std::forward<decltype(run)>(run)](PassInput input) -> PassInput
                     {
                          return self.Execute(std::move(input));
                     }),
                _type(std::remove_cvref_t<decltype(run)>::Type())
          {
          }

          [[nodiscard]] constexpr PassType Type(void) const noexcept
          {
               return this->_type;
          }
          [[nodiscard]] PassInput Run(PassInput input) const
          {
               return this->_run(std::move(input));
          }

     private:
          std::move_only_function<PassInput(PassInput) const> _run;
          PassType _type;
     };

     std::vector<Pass> SelectPasses(OptimisationFeatureSets features, BumpAllocator& allocator);
} // namespace ecpps::optimisations
