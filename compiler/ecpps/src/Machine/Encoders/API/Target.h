#pragma once

#include <memory>
#include <vector>
#include "Machine/Encoders/API/Platform.h"
#include "Machine/Encoders/API/VirtualInstructionEncoder.h"

namespace ecpps::abi::api
{
     struct Target
     {
          std::unique_ptr<VirtualInstructionEncoder> encoder; // TODO: small unique pointer
          std::unique_ptr<PlatformBase> platform;             // TODO: small unique pointer
          std::unique_ptr<VirtualInstructionEncoder> sdk;     // TODO: small unique pointer
          std::vector<std::unique_ptr<VirtualInstructionEncoder>> extensions;
     };
} // namespace ecpps::abi::api
