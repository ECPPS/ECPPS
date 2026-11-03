#include "InstructionEncoder.h"
#include <memory>
#include "Backends/x86_64/Platforms/Linux/Linux.h"
#include "Backends/x86_64/Platforms/Linux/SDK/7/SDK7.0.14.h"
#include "Backends/x86_64/Platforms/Linux/SDK/7/SDK7x.h"
#include "Backends/x86_64/Platforms/Windows/SDK/10/SDK10.h"
#include "Backends/x86_64/Platforms/Windows/Windows.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "Machine/Encoders/Context.h"
#include "Machine/Machine.h"

std::unique_ptr<ecpps::abi::api::Target> ecpps::abi::CreateTarget(
     const ecpps::abi::encoding::CompilationContext& context)
{
     if (context.isa != ecpps::abi::ISA::x86_64) return nullptr;

     switch (context.platform)
     {
     case ecpps::abi::encoding::Platform::Windows:
          if (context.sdk != ecpps::abi::encoding::SDK::WindowsSDK10) return nullptr;
          break;
     case ecpps::abi::encoding::Platform::Linux:
          switch (context.sdk)
          {
          case ecpps::abi::encoding::SDK::LinuxKernel7x:
          case ecpps::abi::encoding::SDK::LinuxKernel7014: break;
          default: return nullptr;
          }
          break;
     default: return nullptr;
     }

     auto target = std::make_unique<ecpps::abi::api::Target>();

     switch (context.platform)
     {
     case ecpps::abi::encoding::Platform::Windows:
          target->sdk = std::make_unique<ecpps::abi::encoders::x8664::WindowsSDK10>();
          target->platform = std::make_unique<ecpps::abi::encoders::x8664::WindowsPlatform>(target->sdk.get());
          break;
     case ecpps::abi::encoding::Platform::Linux:
          if (context.sdk == ecpps::abi::encoding::SDK::LinuxKernel7x)
               target->sdk = std::make_unique<ecpps::abi::encoders::x8664::LinuxSDK7x>();
          else
               target->sdk = std::make_unique<ecpps::abi::encoders::x8664::LinuxSDK7014>();
          target->platform = std::make_unique<ecpps::abi::encoders::x8664::LinuxPlatform>(target->sdk.get());
          break;
     default: return nullptr;
     }

     target->encoder = std::make_unique<ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder>(*target);
     target->EnsureVRM();
     return target;
}
