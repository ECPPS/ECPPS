#pragma once
#include <cstdint>
#include <utility>

namespace ecpps::abi
{
     enum struct Architecture : std::uint_fast8_t
     {
          x86,
          ARM,
     };

     enum struct ISA : std::uint16_t // NOLINT(performance-enum-size)
     {
          x86_32,
          x86_64,
          ARM32,
          ARM64,
     };

     enum struct ArchitectureExtensionFeatures : std::uint64_t // NOLINT(performance-enum-size)
     {
          None = 0,

          // x86
          SSE = 1u << 0u,
          SSE2 = 1u << 1u,
          SSE3 = 1u << 2u,
          SSSE3 = 1u << 3u,
          SSE4_1 = 1u << 4u,
          SSE4_2 = 1u << 5u,
          AVX = 1u << 6u,
          AVX2 = 1u << 7u,
          FMA = 1u << 8u,
          AVX512F = 1u << 9u,
          AVX512BW = 1u << 10u,
          AVX512DQ = 1u << 11u,
          AVX512VL = 1u << 12u,
          BMI1 = 1u << 13u,
          BMI2 = 1u << 14u,
          LZCNT = 1u << 15u,
          POPCNT = 1u << 16u,
          // ARM
          NEON = 1u << 0u,
          ASIMD = 1u << 1u,
          AES = 1u << 2u,
          SHA1 = 1u << 3u,
          SHA2 = 1u << 4u,
          SVE = 1u << 5u,
          SVE2 = 1u << 6u,
          FP16 = 1u << 7u,
          DOTPROD = 1u << 8u,
     };
     constexpr bool operator&(const ArchitectureExtensionFeatures features, const ArchitectureExtensionFeatures other)
     {
          return (std::to_underlying(features) & std::to_underlying(other)) != 0;
     }
     constexpr ArchitectureExtensionFeatures operator|(const ArchitectureExtensionFeatures features,
                                                       const ArchitectureExtensionFeatures other)
     {
          return static_cast<ArchitectureExtensionFeatures>(std::to_underlying(features) | std::to_underlying(other));
     }

     constexpr ArchitectureExtensionFeatures& operator|=(ArchitectureExtensionFeatures& features,
                                                         const ArchitectureExtensionFeatures other)
     {
          return features = features | other;
     }

     struct CPUFeatures
     {
          ISA isa;
          ArchitectureExtensionFeatures features;

          explicit CPUFeatures(const ISA isa, const ArchitectureExtensionFeatures features)
              : isa(isa), features(features)
          {
          }
     };
} // namespace ecpps::abi
