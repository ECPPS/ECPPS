#pragma once

#include <memory>
#include <vector>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Platform.h"
#include "Machine/Encoders/API/SDK.h"
#include "Machine/Encoders/API/VirtualInstructionEncoder.h"

namespace ecpps::abi::api
{
     struct Target
     {
          std::unique_ptr<VirtualInstructionEncoder> encoder; // TODO: small unique pointer
          std::unique_ptr<PlatformBase> platform;             // TODO: small unique pointer
          std::unique_ptr<SDKBase> sdk;                       // TODO: small unique pointer
          std::vector<std::unique_ptr<VirtualInstructionEncoder>> extensions;

          std::unique_ptr<ir::abstract::VirtualRegisterMap> registerMap;
          void EnsureVRM(void)
          {
               if (registerMap == nullptr) return;
               registerMap = std::make_unique<ir::abstract::VirtualRegisterMap>();
          }
     };
} // namespace ecpps::abi::api
