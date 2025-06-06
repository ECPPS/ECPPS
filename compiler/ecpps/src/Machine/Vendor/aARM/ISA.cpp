#include "../Shared/ISA.h"
#include <platformlib/identify.h>

namespace abi = ecpps::abi;

template <> abi::ISA abi::Platform::CurrentISA<abi::Architecture::ARM>(void)
{
     const auto bitness = platformlib::PlatformIdentity::Identify().bitness;
     return bitness == platformlib::ArchitectureBitness::ARM32 ? ISA::ARM32 : ISA::ARM64;
}
