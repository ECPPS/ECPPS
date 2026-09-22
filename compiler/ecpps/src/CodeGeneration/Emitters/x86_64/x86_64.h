#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "../../CodeEmitter.h"
#include "../../Nodes.h"
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
          {
               this->_stringRelocationSize = abi::dwordSize / typeSystem::CharWidth;
          }

          [[nodiscard]] std::vector<std::byte> EmitInstruction(const ir::abstract::Instruction& instruction) final;

     private:
          [[nodiscard]] std::vector<std::byte> EmitMov(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitAdd(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitSub(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitRet(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitPop(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitPush(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitLeftShift(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitRightShift(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitXchg(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitBinaryOr(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitBinaryAnd(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitBinaryXor(const ir::abstract::DynamicBytecode& description);
          [[nodiscard]] std::vector<std::byte> EmitBitwiseNot(const ir::abstract::DynamicBytecode& description);
     };
} // namespace ecpps::codegen::emitters
