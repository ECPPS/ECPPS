#pragma once

#include "Machine/Encoders/API/SDK.h"
namespace ecpps::abi::api
{
     struct PlatformBase
     {
          explicit PlatformBase(SDKBase* currentSdk) : _currentSdk(currentSdk) {}
          virtual ~PlatformBase(void) = default;

     protected:
          SDKBase* _currentSdk; // TODO: non-null pointer
     };
} // namespace ecpps::abi::api
