#include "../Shared/ISA.h"
#include <platformlib/identify.h>

namespace abi = ecpps::abi;

template <> abi::ISA abi::Platform::CurrentISA<abi::Architecture::x86>(void)
{
     const auto bitness = platformlib::PlatformIdentity::Identify().bitness;
     return bitness == platformlib::ArchitectureBitness::x86_32 ? ISA::x86_32 : ISA::x86_64;
}
