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

          [[nodiscard]] static std::string MangleType(const typeSystem::TypePointer& type);

          // TODO: namespaces and classes
          [[nodiscard]] static std::string MangleName(Linkage linkage, const std::string& name,
                                                      CallingConventionName callingConvetion,
                                                      const typeSystem::TypePointer& returnType,
                                                      const std::vector<typeSystem::TypePointer>& parameters);
     };
} // namespace ecpps::abi