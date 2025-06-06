#pragma once
#include <cstdint>

namespace ecpps::platformlib
{
     enum struct ArchitectureBitness
     {
          x86_64,
          x86_32,
          ARM32,
          ARM64
     };

     enum struct OperatingSystem
     {
          Windows,
          WindowsOnWindows,
          Linux,
     };

     struct PlatformIdentity
     {
          ArchitectureBitness bitness;
          explicit PlatformIdentity(const ArchitectureBitness bitness) : bitness(bitness) {}

          /// <summary>
          /// Calling this from other platform than x86 is undefined
          /// </summary>
          /// <returns>Current platform identity (x86)</returns>
          static PlatformIdentity Identify(void);
     };
} // namespace ecpps::platformlib