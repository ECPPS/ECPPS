#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include "Machine/Machine.h"

namespace ecpps::abi::encoding
{
     enum struct Platform : std::uint16_t // NOLINT(performance-enum-size)
     {
          None, // freestanding
          Windows,
          Linux
     };
     enum struct SDK : std::uint16_t // NOLINT(performance-enum-size)
     {
          Unknown = 0,
          // Windows SDK
          WindowsSDK10 = 1,

          // Linux kernel
          LinuxKernel7x = 7000,
          LinuxKernel7014 = 7014,
     };
     enum struct MicroArch : std::uint16_t // NOLINT(performance-enum-size)
     {
          Unknown,
          Haswell,
          Skylake
     };
     using Extensions = ArchitectureExtensionFeatures;

     struct alignas(16) CompilationId
     {
          std::uint64_t base{};
          std::uint64_t extensions{};

          [[nodiscard]] constexpr bool operator==(const CompilationId other) const noexcept
          {
               return other.base == this->base && other.extensions == this->extensions;
          }
     };

     struct CompilationContext
     {
          ISA isa{};
          Platform platform = Platform::None;
          SDK sdk = SDK::Unknown;
          MicroArch cpu = MicroArch::Unknown;
          Extensions extensions = Extensions::None;

          [[nodiscard]] CompilationId MakeId(void) const noexcept
          {
               CompilationId id{};
               id.base = static_cast<std::uint64_t>(isa);
               id.base |= static_cast<std::uint64_t>(platform) << 16;
               id.base |= static_cast<std::uint64_t>(sdk) << 32;
               id.base |= static_cast<std::uint64_t>(cpu) << 48;
               id.extensions = std::to_underlying(extensions);
               return id;
          }
     };
} // namespace ecpps::abi::encoding

template <> struct std::hash<ecpps::abi::encoding::CompilationId>
{
     constexpr static std::size_t operator()(const ecpps::abi::encoding::CompilationId& id) noexcept
     {
          return std::hash<std::size_t>{}(id.base) ^ std::hash<std::size_t>{}(id.extensions);
     }
};
