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

[[nodiscard]] constexpr std::size_t AlignUp(const std::size_t value, const std::size_t alignment) noexcept
{
     return (value + alignment - 1) / alignment * alignment;
}

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
     // TODO: Consult ABI. This is missing many meowy features
     ClassLayoutResult result{};
     result.alignment = 1;
     result.offsets.reserve(members.size());

     std::size_t offset = 0;
     for (const auto& member : members)
     {
          runtime_assert(member.alignment != 0, "A complete type cannot have zero alignment");
          result.alignment = std::max(result.alignment, member.alignment);

          if (isUnion)
          {
               result.offsets.push_back(0);
               offset = std::max(offset, member.size);
               continue;
          }
          offset = AlignUp(offset, member.alignment);
          result.offsets.push_back(offset);
          offset += member.size;
     }

     result.size = members.empty() ? 1 : AlignUp(offset, result.alignment);
     return result;
}
// TODO: Okay tired of writing TODOs but basically everything below needs to consult ABI.
ecpps::abi::SpecialMemberAbi ecpps::abi::ABI::SpecialMembers(void) const noexcept
{
     return SpecialMemberAbi{.constructorsReturnThis = true};
}
ecpps::abi::CallingConventionName ecpps::abi::ABI::ImplicitMemberCallingConvention(void) const noexcept
{
     return CallingConventionName{};
}
ecpps::abi::Linkage ecpps::abi::ABI::ImplicitMemberLinkage(void) const noexcept
{
     return Linkage::Internal;
}

std::string ecpps::abi::ABI::MangleMemberName(Linkage linkage, const std::string& name,
                                              const CallingConventionName callingConvention,
                                              typeSystem::NonowningTypePointer returnType,
                                              const std::vector<typeSystem::NonowningTypePointer>& parameters,
                                              const std::vector<typeSystem::ScopeSegment>& scope,
                                              const MemberFunctionInfo& member)
{
     return Mangling::MangleMemberName(linkage, name, callingConvention, returnType, parameters, scope, member);
}

std::string ecpps::abi::ABI::MangleSpecialMember(const typeSystem::ClassInfo& info, const SpecialMemberKind kind,
                                                 const std::vector<typeSystem::NonowningTypePointer>& parameters) const
{
     return Mangling::MangleMemberName(
          this->ImplicitMemberLinkage(), kind == SpecialMemberKind::Constructor ? ".ctor" : ".dtor",
          this->ImplicitMemberCallingConvention(), nullptr, parameters, info.QualifiedScope(), MemberFunctionInfo{});
}
