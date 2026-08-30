#include "InstructionEncoder.h"
#include <format>
#include <memory>
#include <unordered_map>
#include <utility>
#include "Backends/x86_64/Platforms/Linux/Linux.h"
#include "Backends/x86_64/Platforms/Linux/SDK/7/SDK7.0.14.h"
#include "Backends/x86_64/Platforms/Linux/SDK/7/SDK7x.h"
#include "Backends/x86_64/Platforms/Windows/SDK/10/SDK10.h"
#include "Backends/x86_64/Platforms/Windows/Windows.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "Machine/Encoders/Context.h"
#include "Machine/Machine.h"
#include "RuntimeAssert.h"
#include "Shared/Diagnostics.h"

std::unordered_map<ecpps::abi::encoding::CompilationId, std::unique_ptr<ecpps::abi::api::Target>>
     ecpps::abi::BackendRegistry::_backends{};

static std::unique_ptr<ecpps::abi::api::Target> FromDescription(ecpps::abi::encoding::CompilationContext context)
{
     auto target = std::make_unique<ecpps::abi::api::Target>();
     switch (context.isa)
     {
     case ecpps::abi::ISA::x86_64:
     {
          target->encoder = std::make_unique<ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder>(*target);

          switch (context.platform)
          {
          case ecpps::abi::encoding::Platform::Windows:
          {
               switch (context.sdk)
               {
               case ecpps::abi::encoding::SDK::Unknown: break;
               case ecpps::abi::encoding::SDK::WindowsSDK10:
                    target->sdk = std::make_unique<ecpps::abi::encoders::x8664::WindowsSDK10>();
                    break;
               default:
                    throw ecpps::TracedException(
                         std::format("Invalid target.sdk: {}", std::to_underlying(context.sdk)));
               }
               target->platform = std::make_unique<ecpps::abi::encoders::x8664::WindowsPlatform>(target->sdk.get());
          }
          break;
          case ecpps::abi::encoding::Platform::Linux:
          {
               switch (context.sdk)
               {
               case ecpps::abi::encoding::SDK::Unknown: break;
               case ecpps::abi::encoding::SDK::LinuxKernel7x:
                    target->sdk = std::make_unique<ecpps::abi::encoders::x8664::LinuxSDK7x>();
                    break;
               case ecpps::abi::encoding::SDK::LinuxKernel7014:
                    target->sdk = std::make_unique<ecpps::abi::encoders::x8664::LinuxSDK7014>();
                    break;
               default:
                    throw ecpps::TracedException(
                         std::format("Invalid target.sdk: {}", std::to_underlying(context.sdk)));
               }
               target->platform = std::make_unique<ecpps::abi::encoders::x8664::LinuxPlatform>(target->sdk.get());
          }
          break;
          default:
               throw ecpps::TracedException(
                    std::format("Invalid target.platform: {}", std::to_underlying(context.platform)));
          }
     }
     break;
     default: throw ecpps::TracedException(std::format("Invalid target.isa: {}", std::to_underlying(context.isa)));
     }
     runtime_assert(target->encoder != nullptr, "Encoder was null");
     runtime_assert(target->platform != nullptr, "Platform was null");
     return target;
}

static void RegisterSpecificSDK(ecpps::abi::encoding::CompilationContext& context, ecpps::abi::encoding::SDK sdk)
{
     context.sdk = sdk;
     ecpps::abi::BackendRegistry::Register(context, FromDescription(context));

     // TODO: Micro architecture variants
     // TODO: Extension variants
}

static void RegisterWindowsTargets(ecpps::abi::encoding::CompilationContext& context)
{
     context.platform = ecpps::abi::encoding::Platform::Windows;

     RegisterSpecificSDK(context, ecpps::abi::encoding::SDK::Unknown);
     RegisterSpecificSDK(context, ecpps::abi::encoding::SDK::WindowsSDK10);
}

static void RegisterLinuxTargets(ecpps::abi::encoding::CompilationContext& context)
{
     context.platform = ecpps::abi::encoding::Platform::Linux;

     // Unknown

     RegisterSpecificSDK(context, ecpps::abi::encoding::SDK::Unknown);
     RegisterSpecificSDK(context, ecpps::abi::encoding::SDK::LinuxKernel7x);
     RegisterSpecificSDK(context, ecpps::abi::encoding::SDK::LinuxKernel7014);
}

static void RegisterX8664(void)
{
     ecpps::abi::encoding::CompilationContext context{};
     context.isa = ecpps::abi::ISA::x86_64;

     RegisterWindowsTargets(context);
     RegisterLinuxTargets(context);
}

// static void RegisterARM64(void)
// {
//      ecpps::abi::encoding::CompilationContext context{};
//      context.isa = ecpps::abi::ISA::ARM64;

//      RegisterWindowsTargets(context);
//      RegisterLinuxTargets(context);
// }

void ecpps::abi::BackendRegistry::RegisterTargets(void)
{
     RegisterX8664();
     // RegisterARM64();
}
