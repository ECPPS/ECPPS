#pragma once
#include <cstdint>
#include <utility>

namespace ecpps::abi
{
     enum struct Architecture : std::uint_fast8_t
     {
          x86,
          ARM
     };

     enum struct ISA : std::uint16_t // NOLINT(performance-enum-size)
     {
          x86_32,
          x86_64,
          ARM32,
          ARM64,
     };

     enum struct Endianness : bool
     {
          Little,
          Big
     };

     enum struct ArchitectureExtensionFeatures : std::uint64_t // NOLINT(performance-enum-size)
     {
          None = 0,

          // x86
          SSE = 1 << 0,
          SSE2 = 1 << 1,
          SSE3 = 1 << 2,
          SSSE3 = 1 << 3,
          SSE4_1 = 1 << 4,
          SSE4_2 = 1 << 5,
          AVX = 1 << 6,
          AVX2 = 1 << 7,
          FMA = 1 << 8,
          AVX512F = 1 << 9,
          AVX512BW = 1 << 10,
          AVX512DQ = 1 << 11,
          AVX512VL = 1 << 12,
          BMI1 = 1 << 13,
          BMI2 = 1 << 14,
          LZCNT = 1 << 15,
          POPCNT = 1 << 16,
          // ARM
          NEON = 1 << 0,
          ASIMD = 1 << 1,
          AES = 1 << 2,
          SHA1 = 1 << 3,
          SHA2 = 1 << 4,
          SVE = 1 << 5,
          SVE2 = 1 << 6,
          FP16 = 1 << 7,
          DOTPROD = 1 << 8
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
