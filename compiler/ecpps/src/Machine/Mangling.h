#pragma once
#include <string>
#include <vector>
#include "../TypeSystem/TypeBase.h"
#include "ABI.h"

namespace ecpps::abi
{
     struct Mangling
     {
          explicit Mangling(void) = delete;

          [[nodiscard]] static std::string MangleType(typeSystem::NonowningTypePointer type);

          // TODO: namespaces and classes
          [[nodiscard]] static std::string MangleName(Linkage linkage, const std::string& name,
                                                      CallingConventionName callingConvention,
                                                      typeSystem::NonowningTypePointer returnType,
                                                      const std::vector<typeSystem::NonowningTypePointer>& parameters);
     };
} // namespace ecpps::abi
