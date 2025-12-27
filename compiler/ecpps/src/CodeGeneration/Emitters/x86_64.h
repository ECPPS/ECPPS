#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "../CodeEmitter.h"
#include "../Nodes.h"

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

     class X8664Emitter;

     template <OperandCombination> struct EmitSpecificMovImpl
     {
          static std::vector<std::byte> operator()(X8664Emitter* self, const MovInstruction& mov);
     };

     template <OperandCombination> struct EmitSpecificConversionImpl
     {
          static std::vector<std::byte> operator()(X8664Emitter* self, const MovInstruction& mov);
     };

     template <OperandCombination> struct EmitSpecificAddImpl
     {
          static std::vector<std::byte> operator()(X8664Emitter* self, const AddInstruction& sub);
     };

     template <OperandCombination> struct EmitSpecificSubImpl
     {
          static std::vector<std::byte> operator()(X8664Emitter* self, const SubInstruction& sub);
     };

     template <OperandCombination> struct EmitSpecificMulImpl
     {
          static std::vector<std::byte> operator()(X8664Emitter* self, const MulInstruction& mul);
     };

     template <OperandCombination> struct EmitSpecificDivImpl
     {
          static std::vector<std::byte> operator()(X8664Emitter* self, const DivInstruction& div);
     };

     class X8664Emitter final : public CodeEmitter
     {
     public:
          explicit X8664Emitter(void) : CodeEmitter("x86_64") {}

          virtual void PatchCalls(std::vector<std::byte>& source,
                                  std::unordered_map<std::string, std::size_t>& routines) override;

     private:
          // general overrides
          [[nodiscard]] std::vector<std::byte> EmitMov(const MovInstruction& mov) override;
          [[nodiscard]] std::vector<std::byte> EmitAdd(const AddInstruction& add) override;
          [[nodiscard]] std::vector<std::byte> EmitSub(const SubInstruction& sub) override;
          [[nodiscard]] std::vector<std::byte> EmitMul(const MulInstruction& mul) override;
          [[nodiscard]] std::vector<std::byte> EmitDiv(const DivInstruction& div) override;
          [[nodiscard]] std::vector<std::byte> EmitCall(const CallInstruction& call) override;
          [[nodiscard]] std::vector<std::byte> EmitReturn(void) override;

          // x86_64 specific helpers
          template <OperandCombination TOperandCombination>
          [[nodiscard]] std::vector<std::byte> EmitSpecificMov(const MovInstruction& mov)
          {
               return EmitSpecificMovImpl<TOperandCombination>{}(this, mov);
          }
          template <OperandCombination TOperandCombination>
          [[nodiscard]] std::vector<std::byte> EmitSpecificConversion(const MovInstruction& mov)
          {
               return EmitSpecificConversionImpl<TOperandCombination>{}(this, mov);
          }
          template <OperandCombination TOperandCombination>
          [[nodiscard]] std::vector<std::byte> EmitSpecificAdd(const AddInstruction& add)
          {
               return EmitSpecificAddImpl<TOperandCombination>{}(this, add);
          }
          template <OperandCombination TOperandCombination>
          [[nodiscard]] std::vector<std::byte> EmitSpecificSub(const SubInstruction& sub)
          {
               return EmitSpecificSubImpl<TOperandCombination>{}(this, sub);
          }
          template <OperandCombination TOperandCombination>
          [[nodiscard]] std::vector<std::byte> EmitSpecificMul(const MulInstruction& mul)
          {
               return EmitSpecificMulImpl<TOperandCombination>{}(this, mul);
          }
          template <OperandCombination TOperandCombination>
          [[nodiscard]] std::vector<std::byte> EmitSpecificDiv(const DivInstruction& div)
          {
               return EmitSpecificDivImpl<TOperandCombination>{}(this, div);
          }

          // x86 operand conventions
          static std::size_t RegisterToIndex(const RegisterOperand& register_);

          template <OperandCombination> friend struct EmitSpecificMovImpl;
          template <OperandCombination> friend struct EmitSpecificConversionImpl;
          template <OperandCombination> friend struct EmitSpecificAddImpl;
          template <OperandCombination> friend struct EmitSpecificSubImpl;
          template <OperandCombination> friend struct EmitSpecificMulImpl;
          template <OperandCombination> friend struct EmitSpecificDivImpl;
     };
} // namespace ecpps::codegen::emitters
