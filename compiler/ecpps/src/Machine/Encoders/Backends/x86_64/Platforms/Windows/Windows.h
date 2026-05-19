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
     };
} // namespace ecpps::abi::encoders::x8664
