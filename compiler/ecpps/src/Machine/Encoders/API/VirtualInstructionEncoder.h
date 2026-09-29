#pragma once

#include <vector>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Machine.h"
#include "Shared/Config.h"

namespace ecpps::ir
{
     struct FunctionScope;
}

namespace ecpps::abi::api
{
     struct Target;

     struct VirtualInstructionEncoder
     {
          explicit VirtualInstructionEncoder(const ecpps::abi::ISA isa, Target& target) : _target(&target), _isa(isa)
          {
          }
          virtual ~VirtualInstructionEncoder(void) = default;

          [[nodiscard]] virtual std::vector<ir::abstract::Instruction> Encode(
               const std::vector<ir::abstract::VirtualInstruction>& input) = 0;

          [[nodiscard]] constexpr ISA IsaName(void) const noexcept
          {
               return this->_isa;
          }

          [[nodiscard]] virtual std::string Stringify(const ir::abstract::Instruction& instruction) const = 0;

          virtual void Finalise(std::vector<ir::abstract::Instruction>& instructions) = 0;
          virtual void ApplyOptimisations(OptimisationFeatureSets optimisations) = 0;
          void SetFunctionCallTable(std::vector<const ir::FunctionScope*> scopes)
          {
               this->_scopes = std::move(scopes);
          }
          [[nodiscard]] const std::vector<const ir::FunctionScope*>& Scopes(void) const noexcept
          {
               return this->_scopes;
          }

     protected:
          Target* _target; // TODO: non-null pointer
          [[nodiscard]] std::vector<const ir::FunctionScope*>& Scopes(void) noexcept
          {
               return this->_scopes;
          }

     private:
          ISA _isa;
          std::vector<const ir::FunctionScope*> _scopes{};
     };
} // namespace ecpps::abi::api
