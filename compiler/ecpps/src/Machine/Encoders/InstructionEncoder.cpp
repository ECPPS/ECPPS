#include "InstructionEncoder.h"
#include <format>
#include <memory>
#include <unordered_map>
#include <utility>
#include "Machine/Encoders/API/Target.h"
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
     case ecpps::abi::ISA::x86_64: break;
     default: throw ecpps::TracedException(std::format("Invalid target.isa: {}", std::to_underlying(context.isa)));
     }

     runtime_assert(target->encoder != nullptr, "Encoder was null");
     runtime_assert(target->platform != nullptr, "Platform was null");
     runtime_assert(target->sdk != nullptr, "SDK was null");
     return target;
}

static void RegisterSpecificSDK(ecpps::abi::encoding::CompilationContext& context, ecpps::abi::encoding::SDK sdk)
{
     context.sdk = sdk;
     ecpps::abi::BackendRegistry::Register(context, FromDescription(context));

     // TODO: Micro architecture variants
     // TODO: Extension variants
}

static void RegisterFreestandingTargets(ecpps::abi::encoding::CompilationContext& context)
{
     context.platform = ecpps::abi::encoding::Platform::None;

     RegisterSpecificSDK(context, ecpps::abi::encoding::SDK::Unknown);
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

     RegisterFreestandingTargets(context);
     RegisterWindowsTargets(context);
     RegisterLinuxTargets(context);
}

static void RegisterARM64(void)
{
     ecpps::abi::encoding::CompilationContext context{};
     context.isa = ecpps::abi::ISA::ARM64;

     RegisterFreestandingTargets(context);
     RegisterWindowsTargets(context);
     RegisterLinuxTargets(context);
}

void ecpps::abi::BackendRegistry::RegisterTargets(void)
{
     RegisterX8664();
     RegisterARM64();
}
