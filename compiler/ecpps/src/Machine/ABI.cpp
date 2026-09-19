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
