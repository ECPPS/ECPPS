#pragma once
#include "../../Machine.h"

namespace ecpps::abi
{
     class Platform
     {
     public:
          template <Architecture TArchitecture> static ISA CurrentISA(void);

          static consteval Architecture CurrentVendor(void)
          {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
               return Architecture::x86;
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(__arm__) || defined(__aarch64__)
               return Architecture::ARM;
#else
               static_assert(false, "Unsupported architecture");
#endif
          }
     };
} // namespace ecpps::abi