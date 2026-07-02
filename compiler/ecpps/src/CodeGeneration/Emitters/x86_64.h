#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "../CodeEmitter.h"
#include "../Nodes.h"
#include "Machine/Storage.h"
#include "TypeSystem/TypeBase.h"

namespace ecpps::codegen::emitters
{
     enum struct OperandCombination : std::uint8_t
     {
          RegisterToRegister,
          RegisterToMemory,
          MemoryToRegister,
          ImmediateToRegister,
          ImmediateToMemory,
     };

     class X8664Emitter final : public CodeEmitter
     {
     public:
          explicit X8664Emitter(void) : CodeEmitter("x86_64")
          { this->_stringRelocationSize = abi::dwordSize / typeSystem::CharWidth; }

          void PatchCalls(std::vector<std::byte>& source,
                          std::unordered_map<std::string, std::size_t>& routines) override;

          [[nodiscard]] std::vector<std::byte> EmitInstruction(const ir::abstract::Instruction& instruction) final;

     private:
     };
} // namespace ecpps::codegen::emitters
