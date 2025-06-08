#pragma once
#include <cstddef>
#include <vector>
#include "../CodeEmitter.h"
#include "../Nodes.h"

namespace ecpps::codegen::emitters
{
     enum struct OperandCombination
     {
          RegisterToRegister,
          RegisterToMemory,
          MemoryToRegister,
          ImmediateToRegister,
          ImmediateToMemory,
     };

     class X8664Emitter;

     template <OperandCombination> struct EmitSpecificMovImpl
     {
          static std::vector<std::byte> Go(X8664Emitter* self, const MovInstruction& mov);
     };

     template <OperandCombination> struct EmitSpecificAddImpl
     {
          static std::vector<std::byte> Go(X8664Emitter* self, const AddInstruction& sub);
     };

     template <OperandCombination> struct EmitSpecificSubImpl
     {
          static std::vector<std::byte> Go(X8664Emitter* self, const SubInstruction& add);
     };

     class X8664Emitter final : public CodeEmitter
     {
     public:
          explicit X8664Emitter(void) : CodeEmitter("x86_64") {}

     private:
          // general overrides
          [[nodiscard]] std::vector<std::byte> EmitMov(const MovInstruction& mov) override;
          [[nodiscard]] std::vector<std::byte> EmitAdd(const AddInstruction& add) override;
          [[nodiscard]] std::vector<std::byte> EmitSub(const SubInstruction& sub) override;
          [[nodiscard]] std::vector<std::byte> EmitReturn(void) override;

          // x86_64 specific helpers
          template <OperandCombination TOperandCombination>
          [[nodiscard]] std::vector<std::byte> EmitSpecificMov(const MovInstruction& mov)
          {
               return EmitSpecificMovImpl<TOperandCombination>::Go(this, mov);
          }                      
          template <OperandCombination TOperandCombination>
          [[nodiscard]] std::vector<std::byte> EmitSpecificAdd(const AddInstruction& add)
          {
               return EmitSpecificAddImpl<TOperandCombination>::Go(this, add);
          }                          
          template <OperandCombination TOperandCombination>
          [[nodiscard]] std::vector<std::byte> EmitSpecificSub(const SubInstruction& sub)
          {
               return EmitSpecificSubImpl<TOperandCombination>::Go(this, sub);
          }

          // x86 operand conventions
          std::size_t RegisterToIndex(const RegisterOperand& register_);

          template <OperandCombination> friend struct EmitSpecificMovImpl;
          template <OperandCombination> friend struct EmitSpecificAddImpl;
          template <OperandCombination> friend struct EmitSpecificSubImpl;
     };
} // namespace ecpps::codegen::emitters