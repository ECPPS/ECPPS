#include <identify.h>
#include <platformlib.h>

#ifdef _WIN32
#include <Windows.h>
#elifdef __linux__

#endif

#include <format>

ecpps::platformlib::PlatformIdentity ecpps::platformlib::PlatformIdentity::Identify(void)
{
     ecpps::platformlib::ArchitectureBitness bitness{};
#ifdef _WIN32
     USHORT processMachine{};
     USHORT nativeMachine{};

     if (!IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
     {
          const auto code = GetLastError();
          throw ecpps::platformlib::NativeException(std::format("::IsWow64Process2() failed: {}", code));
     }
     if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN)
     {
          switch (nativeMachine)
          {
          case IMAGE_FILE_MACHINE_ARM64: bitness = ecpps::platformlib::ArchitectureBitness::ARM64; break;
          case IMAGE_FILE_MACHINE_ARM: bitness = ecpps::platformlib::ArchitectureBitness::ARM32; break;
          case IMAGE_FILE_MACHINE_AMD64:
          case IMAGE_FILE_MACHINE_IA64: bitness = ecpps::platformlib::ArchitectureBitness::x86_64; break;
          case IMAGE_FILE_MACHINE_I386: bitness = ecpps::platformlib::ArchitectureBitness::x86_32; break;
          default:
               throw ecpps::platformlib::NativeException(
                   std::format("Unknown native architecture: 0x{:X}", nativeMachine));
          }
     }
     else
     {
          switch (nativeMachine)
          {
          case IMAGE_FILE_MACHINE_ARM64: bitness = ecpps::platformlib::ArchitectureBitness::ARM64; break;
          case IMAGE_FILE_MACHINE_AMD64: bitness = ecpps::platformlib::ArchitectureBitness::x86_64; break;
          default:
               throw ecpps::platformlib::NativeException(
                   std::format("Unsupported WOW64 native arch: 0x{:X}", nativeMachine));
          }
     }
#else
#if defined(__x86_64__)
     bitness = ecpps::platformlib::ArchitectureBitness::x64;
#elif defined(__i386__)
     bitness = ecpps::platformlib::ArchitectureBitness::x32;
#elif defined(__aarch64__)
     bitness = ecpps::platformlib::ArchitectureBitness::ARM64;
#elif defined(__arm__)
     bitness = ecpps::platformlib::ArchitectureBitness::ARM32;
#else
#error Unknown architecture
#endif
#endif

     return ecpps::platformlib::PlatformIdentity{bitness};
}