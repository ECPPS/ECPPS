#include "ABI.h"
#include <RuntimeAssert.h>
#include <algorithm>
#include <cstddef>
#include <vector>
#include "../Shared/Diagnostics.h"
#include "Machine.h"
#include "Machine/Machine.h"
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

[[nodiscard]] ecpps::abi::ClassLayoutResult ecpps::abi::ABI::LayoutClass(
     bool isUnion, std::span<const MemberLayoutInput> members) const
{
     const auto alignUp = [](std::size_t value, std::size_t alignment) -> std::size_t
     {
          alignment = std::max(1uz, alignment);
          return ((value + alignment - 1uz) / alignment) * alignment;
     };

     std::size_t alignment = 1uz;
     std::size_t size = 0uz;
     std::vector<std::size_t> offsets;
     offsets.reserve(members.size());

     if (isUnion)
     {
          for (const auto& member : members)
          {
               alignment = std::max(alignment, member.alignment);
               size = std::max(size, member.size);
               offsets.push_back(0uz);
          }

          size = alignUp(std::max(1uz, size), alignment);
     }
     else
     {
          for (const auto& member : members)
          {
               alignment = std::max(alignment, member.alignment);
               size = alignUp(size, member.alignment);
               offsets.push_back(size);
               size += member.size;
          }

          size = alignUp(std::max(1uz, size), alignment);
     }

     return ClassLayoutResult{
          .size = size,
          .alignment = alignment,
          .offsets = std::move(offsets),
     };
}
