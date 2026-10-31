#pragma once
#include <string>
#include <vector>
#include "../TypeSystem/TypeBase.h"
#include "TypeSystem/ClassInfo.h"

namespace ecpps::abi
{
     enum struct MemberRefQualifier : std::uint_fast8_t
     {
          None,
          LValue,
          RValue
     };
     struct MemberFunctionInfo
     {
          bool isStatic{};
          bool isConst{};
          bool isVolatile{};
          MemberRefQualifier refQualifier{};
     };
     enum struct Linkage : std::uint_fast8_t
     {
          NoLinkage,
          Internal,
          External,
          Module,
          CLinkage
     };

     enum struct CallingConventionName : std::uint_fast16_t // NOLINT(performance-enum-size)
     {
          Microsoftx64
     };

     struct Mangling
     {
          explicit Mangling(void) = delete;

          [[nodiscard]] static std::string MangleType(typeSystem::NonowningTypePointer type);
          [[nodiscard]] static std::string MangleClassPath(const typeSystem::ClassInfo& info);

          // TODO: namespaces and classes
          [[nodiscard]] static std::string MangleName(Linkage linkage, const std::string& name,
                                                      CallingConventionName callingConvention,
                                                      typeSystem::NonowningTypePointer returnType,
                                                      const std::vector<typeSystem::NonowningTypePointer>& parameters,
                                                      const std::vector<std::string>& namespacePath);
          [[nodiscard]] static std::string MangleName(Linkage linkage, const std::string& name,
                                                      CallingConventionName callingConvention,
                                                      typeSystem::NonowningTypePointer returnType,
                                                      const std::vector<typeSystem::NonowningTypePointer>& parameters,
                                                      const std::vector<typeSystem::ScopeSegment>& scope);
          [[nodiscard]] static std::string MangleMemberName(
               Linkage linkage, const std::string& name, CallingConventionName callingConvention,
               typeSystem::NonowningTypePointer returnType,
               const std::vector<typeSystem::NonowningTypePointer>& parameters,
               const std::vector<typeSystem::ScopeSegment>& scope, const MemberFunctionInfo& member);
     };
} // namespace ecpps::abi
