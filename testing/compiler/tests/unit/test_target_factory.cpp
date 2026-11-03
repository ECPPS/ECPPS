#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>
#include "Machine/Encoders/InstructionEncoder.h"

TEST_CASE("CreateTarget creates the configured supported backend")
{
     ecpps::abi::encoding::CompilationContext context{};

     SECTION("Linux kernel 7.x")
     {
          context.isa = ecpps::abi::ISA::x86_64;
          context.platform = ecpps::abi::encoding::Platform::Linux;
          context.sdk = ecpps::abi::encoding::SDK::LinuxKernel7x;
     }

     SECTION("Linux kernel 7.14")
     {
          context.isa = ecpps::abi::ISA::x86_64;
          context.platform = ecpps::abi::encoding::Platform::Linux;
          context.sdk = ecpps::abi::encoding::SDK::LinuxKernel7014;
     }

     SECTION("Windows SDK 10")
     {
          context.isa = ecpps::abi::ISA::x86_64;
          context.platform = ecpps::abi::encoding::Platform::Windows;
          context.sdk = ecpps::abi::encoding::SDK::WindowsSDK10;
     }

     const auto target = ecpps::abi::CreateTarget(context);

     REQUIRE(target != nullptr);
     REQUIRE(target->encoder != nullptr);
     REQUIRE(target->platform != nullptr);
     REQUIRE(target->sdk != nullptr);
     REQUIRE(target->registerMap != nullptr);
}

TEST_CASE("CreateTarget rejects unsupported target combinations")
{
     ecpps::abi::encoding::CompilationContext context{};
     context.isa = ecpps::abi::ISA::x86_64;
     context.platform = ecpps::abi::encoding::Platform::Linux;
     context.sdk = ecpps::abi::encoding::SDK::Unknown;

     REQUIRE(ecpps::abi::CreateTarget(context) == nullptr);

     context.sdk = ecpps::abi::encoding::SDK::LinuxKernel7x;
     context.platform = ecpps::abi::encoding::Platform::None;

     REQUIRE(ecpps::abi::CreateTarget(context) == nullptr);

     context.isa = ecpps::abi::ISA::ARM64;
     context.platform = ecpps::abi::encoding::Platform::Linux;

     REQUIRE(ecpps::abi::CreateTarget(context) == nullptr);
}
