#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <string_view>
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

namespace std
{
     template <> struct formatter<ecpps::abi::encoding::Platform>
     {
          constexpr auto parse(std::format_parse_context& context) // NOLINT
          {
               return context.begin();
          }

          auto format(const ecpps::abi::encoding::Platform value, std::format_context& context) const // NOLINT
          {
               using enum ecpps::abi::encoding::Platform;

               const std::string_view name = [&]
               {
                    switch (value)
                    {
                    case None: return "none";
                    case Windows: return "windows";
                    case Linux: return "linux";
                    }

                    return "<unknown>";
               }();

               return std::format_to(context.out(), "{}", name);
          }
     };

     template <> struct formatter<ecpps::abi::encoding::SDK>
     {
          constexpr auto parse(std::format_parse_context& context) // NOLINT
          {
               return context.begin();
          }

          auto format(const ecpps::abi::encoding::SDK value, std::format_context& context) const // NOLINT
          {
               using enum ecpps::abi::encoding::SDK;

               const std::string_view name = [&]
               {
                    switch (value)
                    {
                    case Unknown: return "unknown";
                    case WindowsSDK10: return "windows-sdk-10";
                    case LinuxKernel7x: return "linux-kernel-7.x";
                    case LinuxKernel7014: return "linux-kernel-7.14";
                    }

                    return "<unknown>";
               }();

               return std::format_to(context.out(), "{}", name);
          }
     };

     template <> struct formatter<ecpps::abi::encoding::MicroArch>
     {
          constexpr auto parse(std::format_parse_context& context) // NOLINT
          {
               return context.begin();
          }

          auto format(const ecpps::abi::encoding::MicroArch value, std::format_context& context) const // NOLINT
          {
               using enum ecpps::abi::encoding::MicroArch;

               const std::string_view name = [&]
               {
                    switch (value)
                    {
                    case Unknown: return "generic";
                    case Haswell: return "haswell";
                    case Skylake: return "skylake";
                    }

                    return "<unknown>";
               }();

               return std::format_to(context.out(), "{}", name);
          }
     };

     template <> struct formatter<ecpps::abi::ISA>
     {
          constexpr auto parse(std::format_parse_context& context) // NOLINT
          {
               return context.begin();
          }

          auto format(const ecpps::abi::ISA value, std::format_context& context) const // NOLINT
          {
               using enum ecpps::abi::ISA;

               const std::string_view name = [&]
               {
                    switch (value)
                    {
                    case x86_32: return "x86";
                    case x86_64: return "x86-64";
                    case ARM32: return "arm";
                    case ARM64: return "aarch64";
                    }

                    return "<unknown>";
               }();

               return std::format_to(context.out(), "{}", name);
          }
     };

     template <> struct formatter<ecpps::abi::ArchitectureExtensionFeatures>
     {
          constexpr auto parse(std::format_parse_context& context) // NOLINT
          {
               return context.begin();
          }

          auto format(const ecpps::abi::ArchitectureExtensionFeatures value, // NOLINT
                      std::format_context& context) const
          {
               using ecpps::abi::ArchitectureExtensionFeatures;

               if (value == ArchitectureExtensionFeatures::None) return std::format_to(context.out(), "none");

               bool first = true;

               const auto append = [&](const ArchitectureExtensionFeatures feature, std::string_view name)
               {
                    if (!(value & feature)) return;

                    if (!first) std::format_to(context.out(), ",");
                    std::format_to(context.out(), "{}", name);
                    first = false;
               };

               append(ArchitectureExtensionFeatures::SSE, "sse");
               append(ArchitectureExtensionFeatures::SSE2, "sse2");
               append(ArchitectureExtensionFeatures::SSE3, "sse3");
               append(ArchitectureExtensionFeatures::SSSE3, "ssse3");
               append(ArchitectureExtensionFeatures::SSE4_1, "sse4.1");
               append(ArchitectureExtensionFeatures::SSE4_2, "sse4.2");
               append(ArchitectureExtensionFeatures::AVX, "avx");
               append(ArchitectureExtensionFeatures::AVX2, "avx2");
               append(ArchitectureExtensionFeatures::FMA, "fma");
               append(ArchitectureExtensionFeatures::AVX512F, "avx512f");
               append(ArchitectureExtensionFeatures::AVX512BW, "avx512bw");
               append(ArchitectureExtensionFeatures::AVX512DQ, "avx512dq");
               append(ArchitectureExtensionFeatures::AVX512VL, "avx512vl");
               append(ArchitectureExtensionFeatures::BMI1, "bmi1");
               append(ArchitectureExtensionFeatures::BMI2, "bmi2");
               append(ArchitectureExtensionFeatures::LZCNT, "lzcnt");
               append(ArchitectureExtensionFeatures::POPCNT, "popcnt");

               append(ArchitectureExtensionFeatures::NEON, "neon");
               append(ArchitectureExtensionFeatures::ASIMD, "asimd");
               append(ArchitectureExtensionFeatures::AES, "aes");
               append(ArchitectureExtensionFeatures::SHA1, "sha1");
               append(ArchitectureExtensionFeatures::SHA2, "sha2");
               append(ArchitectureExtensionFeatures::SVE, "sve");
               append(ArchitectureExtensionFeatures::SVE2, "sve2");
               append(ArchitectureExtensionFeatures::FP16, "fp16");
               append(ArchitectureExtensionFeatures::DOTPROD, "dotprod");

               return context.out();
          }
     };
} // namespace std
