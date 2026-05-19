#include "ABI.h"
#include <RuntimeAssert.h>
#include <climits>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <vector>
#include "../CodeGeneration/Nodes.h"
#include "../Shared/Diagnostics.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "Machine.h"
#include "Machine/Machine.h"
#include "Machine/Storage.h"
#include "Mangling.h"
#include "Vendor/Shared/ISA.h"

using ecpps::abi::ABI;

extern template ecpps::abi::ISA ecpps::abi::Platform::CurrentISA<ecpps::abi::Platform::CurrentVendor()>(void);

#ifdef __clang__
__attribute__((no_sanitize("address")))
#endif
ABI ABI::_current{Platform::CurrentISA<Platform::CurrentVendor()>()};

ecpps::abi::ABI::ABI(ISA isa) : _isa(isa)
{
}

ABI& ecpps::abi::ABI::Current(void)
{
     return ABI::_current;
}

template <std::size_t TTo, std::size_t TFrom>
std::size_t ecpps::abi::ABI::ConvertEndian(std::size_t value) const noexcept
{
     if constexpr (TFrom == 1)
     {
          return value & ((std::size_t{1} << CHAR_BIT) - 1);
     }

     std::size_t result = 0;

     switch (this->_endianness)
     {
     case ecpps::abi::Endianness::Big:
          for (std::size_t i = 0; i < TFrom && i < TTo; i++)
          {
               const std::size_t byte = (value >> (i * 8)) & 0xFF;
               result |= byte << ((TTo - 1 - i) * 8);
          }
          break;
     case ecpps::abi::Endianness::Little:
     {
          for (std::size_t i = 0; i < TFrom && i < TTo; i++)
          {
               const std::size_t byte = (value >> (i * 8)) & 0xFF;
               result |= byte << (i * 8);
          }
     }
     break;
     }

     return result;
}

std::string ecpps::abi::ABI::MangleName(Linkage linkage, const std::string& name,
                                        const CallingConventionName callingConvention,
                                        typeSystem::NonowningTypePointer returnType,
                                        const std::vector<typeSystem::NonowningTypePointer>& parameters,
                                        const std::vector<std::string>& namespacePath)
{
     if (name == "main") return "main";

     return Mangling::MangleName(linkage, name, callingConvention, returnType, parameters, namespacePath);
}

ecpps::abi::CallingConventionName ecpps::abi::ABI::DefaultCallingConventionName(void) const
{
     switch (this->_isa)
     {
     case abi::ISA::x86_64: return abi::CallingConventionName::Microsoftx64;
     default: throw TracedException("ISA not handled yet");
     }
}
