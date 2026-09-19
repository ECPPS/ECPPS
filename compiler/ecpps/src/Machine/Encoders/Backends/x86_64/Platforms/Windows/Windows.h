#pragma once

#include "Machine/Encoders/API/Platform.h"
#include "Machine/Encoders/API/SDK.h"

namespace ecpps::abi::encoders::x8664
{
     struct WindowsPlatform : api::PlatformBase
     {
          explicit WindowsPlatform(api::SDKBase* currentSdk) : PlatformBase(currentSdk)
          {
          }
          [[nodiscard]] std::size_t StackAlignment(void) const noexcept final
          {
               return 16;
          }
          [[nodiscard]] std::size_t InitialStackReserve(void) const noexcept final
          {
               return 32;
          }
     };
} // namespace ecpps::abi::encoders::x8664
