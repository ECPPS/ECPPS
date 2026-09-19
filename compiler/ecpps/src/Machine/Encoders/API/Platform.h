#pragma once

#include "Machine/Encoders/API/SDK.h"
namespace ecpps::abi::api
{
     struct PlatformBase
     {
          explicit PlatformBase(SDKBase* currentSdk) : _currentSdk(currentSdk)
          {
          }
          virtual ~PlatformBase(void) = default;

          [[nodiscard]] virtual std::uint8_t IntegerReturnRegisterIndex(void) const noexcept
          {
               return 0;
          }
          [[nodiscard]] virtual std::size_t StackAlignment(void) const noexcept
          {
               return 0;
          }
          [[nodiscard]] virtual std::size_t InitialStackReserve(void) const noexcept
          {
               return 0;
          }

     protected:
          SDKBase* _currentSdk; // TODO: non-null pointer
     };
} // namespace ecpps::abi::api
