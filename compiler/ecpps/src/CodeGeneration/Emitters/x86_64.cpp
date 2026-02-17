// NOLINT(readability-identifier-length)

#include "x86_64.h"
#include <format>
#include <stdexcept>
#include "../../CodeGeneration/PseudoAssembly.h"
#include "../../Parsing/Tokeniser.h"
#include "CodeGeneration/Nodes.h"
#include "Machine/ABI.h"
#include "x86_64/Opcodes.h"

void ecpps::codegen::emitters::X8664Emitter::PatchCalls(std::vector<std::byte>& source,
                                                        std::unordered_map<std::string, std::size_t>& routines)
{
     std::size_t currentOffset = 0;

     constexpr static auto ApplyImportLambda =
         [](Address resolved, [[maybe_unused]] std::unordered_map<std::string, std::vector<std::byte>>& thunkProcedures)
         -> std::vector<std::byte>
     { return x86_64::GenerateIndirectCall2(static_cast<std::int32_t>(resolved.Value())); };

     for (auto& [index, name] : this->_relocationTable)
     {
          const auto offset = currentOffset + index;

          if (ecpps::codegen::g_functionImports.contains(name))
          {
               this->linkerForwardedRelocations.emplace(
                   ByteOffset{offset}, Relocation{.symbolName = name,
                                                  .apply = ApplyImportLambda,
                                                  .applyOutputSize = 6}); // Linker pass handles that, hopefully
               continue;
          }

          std::size_t foundFunction = 0;
          for (const auto& [functionName, functionOffset] : routines)
          {
               if (functionName != name) continue;
               foundFunction = functionOffset;
               break;
          }
          const auto code = x86_64::GenerateIndirectCall(-static_cast<std::int32_t>(offset) +
                                                         static_cast<std::int32_t>(foundFunction));
          source.insert_range(source.begin() + static_cast<std::ptrdiff_t>(offset), code);

          const std::size_t delta = code.size();
          currentOffset += delta;

          for (auto& [_, routineOffset] : routines) // NOLINT(readability-identifier-length)
          {
               if (routineOffset > offset) routineOffset += delta;
          }
     }
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitMov(const MovInstruction& mov)
{
     return mov.isConversion
                ? std::visit(
                      OverloadedVisitor{
                          [&mov, this](const RegisterOperand&)
                          {
                               return std::visit(
                                   OverloadedVisitor{
                                       [](std::monostate) static { return std::vector<std::byte>{}; },
                                       [&mov, this](const RegisterOperand&)
                                       {
                                            return this->EmitSpecificConversion<OperandCombination::RegisterToRegister>(
                                                mov);
                                       },
                                       [&mov, this](const IntegerOperand&)
                                       {
                                            return this
                                                ->EmitSpecificConversion<OperandCombination::ImmediateToRegister>(mov);
                                       },
                                       [&mov, this](const MemoryLocationOperand&)
                                       {
                                            return this->EmitSpecificConversion<OperandCombination::MemoryToRegister>(
                                                mov);
                                       },
                                       [](auto&&) -> std::vector<std::byte>
                                       { throw TracedException(std::logic_error("Invalid mov operation")); }},
                                   mov.source);
                          },
                          [&mov, this](const MemoryLocationOperand&)
                          {
                               return std::visit(
                                   OverloadedVisitor{
                                       [&mov, this](const RegisterOperand&)
                                       {
                                            return this->EmitSpecificConversion<OperandCombination::RegisterToMemory>(
                                                mov);
                                       },
                                       [&mov, this](const IntegerOperand&)
                                       {
                                            return this->EmitSpecificConversion<OperandCombination::ImmediateToMemory>(
                                                mov);
                                       },
                                       [](auto&&) -> std::vector<std::byte>
                                       { throw TracedException(std::logic_error("Invalid mov operation")); }},
                                   mov.source);
                          },
                          [](auto&&) -> std::vector<std::byte>
                          { throw TracedException(std::logic_error("Invalid mov operation")); }},
                      mov.destination)
                : std::visit(
                      OverloadedVisitor{
                          [&mov, this](const RegisterOperand&)
                          {
                               return std::visit(
                                   OverloadedVisitor{
                                       [](std::monostate) { return std::vector<std::byte>{}; },
                                       [&mov, this](const RegisterOperand&)
                                       { return this->EmitSpecificMov<OperandCombination::RegisterToRegister>(mov); },
                                       [&mov, this](const IntegerOperand&)
                                       { return this->EmitSpecificMov<OperandCombination::ImmediateToRegister>(mov); },
                                       [&mov, this](const MemoryLocationOperand&)
                                       { return this->EmitSpecificMov<OperandCombination::MemoryToRegister>(mov); },
                                       [](auto&&) -> std::vector<std::byte>
                                       { throw TracedException(std::logic_error("Invalid mov operation")); }},
                                   mov.source);
                          },
                          [&mov, this](const MemoryLocationOperand&)
                          {
                               return std::visit(
                                   OverloadedVisitor{
                                       [&mov, this](const RegisterOperand&)
                                       { return this->EmitSpecificMov<OperandCombination::RegisterToMemory>(mov); },
                                       [&mov, this](const IntegerOperand&)
                                       { return this->EmitSpecificMov<OperandCombination::ImmediateToMemory>(mov); },
                                       [](auto&&) -> std::vector<std::byte>
                                       { throw TracedException(std::logic_error("Invalid mov operation")); }},
                                   mov.source);
                          },
                          [](auto&&) -> std::vector<std::byte>
                          { throw TracedException(std::logic_error("Invalid mov operation")); }},
                      mov.destination);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitAdd(const AddInstruction& add)
{
     return std::visit(
         OverloadedVisitor{
             [&add, this](const RegisterOperand&)
             {
                  return std::visit(
                      OverloadedVisitor{[&add, this](const RegisterOperand&)
                                        { return this->EmitSpecificAdd<OperandCombination::RegisterToRegister>(add); },
                                        [&add, this](const IntegerOperand&)
                                        { return this->EmitSpecificAdd<OperandCombination::ImmediateToRegister>(add); },
                                        [&add, this](const MemoryLocationOperand&)
                                        { return this->EmitSpecificAdd<OperandCombination::MemoryToRegister>(add); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw ecpps::TracedException(std::logic_error("Invalid add operation")); }},
                      add.from);
             },
             [&add, this](const MemoryLocationOperand&)
             {
                  return std::visit(
                      OverloadedVisitor{[&add, this](const RegisterOperand&)
                                        { return this->EmitSpecificAdd<OperandCombination::RegisterToMemory>(add); },
                                        [&add, this](const IntegerOperand&)
                                        { return this->EmitSpecificAdd<OperandCombination::ImmediateToMemory>(add); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw ecpps::TracedException(std::logic_error("Invalid add operation")); }},
                      add.from);
             },
             [](auto&&) -> std::vector<std::byte>
             { throw ecpps::TracedException(std::logic_error("Invalid add operation")); }},
         add.to);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitSub(const SubInstruction& sub)
{
     return std::visit(
         OverloadedVisitor{
             [&sub, this](const RegisterOperand&)
             {
                  return std::visit(
                      OverloadedVisitor{[&sub, this](const RegisterOperand&)
                                        { return this->EmitSpecificSub<OperandCombination::RegisterToRegister>(sub); },
                                        [&sub, this](const IntegerOperand&)
                                        { return this->EmitSpecificSub<OperandCombination::ImmediateToRegister>(sub); },
                                        [&sub, this](const MemoryLocationOperand&)
                                        { return this->EmitSpecificSub<OperandCombination::MemoryToRegister>(sub); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw TracedException(std::logic_error("Invalid sub operation")); }},
                      sub.from);
             },
             [&sub, this](const MemoryLocationOperand&)
             {
                  return std::visit(
                      OverloadedVisitor{[&sub, this](const RegisterOperand&)
                                        { return this->EmitSpecificSub<OperandCombination::RegisterToMemory>(sub); },
                                        [&sub, this](const IntegerOperand&)
                                        { return this->EmitSpecificSub<OperandCombination::ImmediateToMemory>(sub); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw TracedException(std::logic_error("Invalid sub operation")); }},
                      sub.from);
             },
             [](auto&&) -> std::vector<std::byte>
             { throw ecpps::TracedException(std::logic_error("Invalid sub operation")); }},
         sub.to);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitMul(const MulInstruction& mul)
{
     return std::visit(
         OverloadedVisitor{
             [&mul, this](const RegisterOperand&)
             {
                  return std::visit(
                      OverloadedVisitor{[&mul, this](const RegisterOperand&)
                                        { return this->EmitSpecificMul<OperandCombination::RegisterToRegister>(mul); },
                                        [&mul, this](const IntegerOperand&)
                                        { return this->EmitSpecificMul<OperandCombination::ImmediateToRegister>(mul); },
                                        [&mul, this](const MemoryLocationOperand&)
                                        { return this->EmitSpecificMul<OperandCombination::MemoryToRegister>(mul); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw TracedException(std::logic_error("Invalid mul operation")); }},
                      mul.from);
             },
             [&mul, this](const MemoryLocationOperand&)
             {
                  return std::visit(
                      OverloadedVisitor{[&mul, this](const RegisterOperand&)
                                        { return this->EmitSpecificMul<OperandCombination::RegisterToMemory>(mul); },
                                        [&mul, this](const IntegerOperand&)
                                        { return this->EmitSpecificMul<OperandCombination::ImmediateToMemory>(mul); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw TracedException(std::logic_error("Invalid mul operation")); }},
                      mul.from);
             },
             [](auto&&) -> std::vector<std::byte>
             { throw TracedException(std::logic_error("Invalid mul operation")); }},
         mul.to);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitDiv(const DivInstruction& div)
{
     return std::visit(
         OverloadedVisitor{
             [&div, this](const RegisterOperand&)
             {
                  return std::visit(
                      OverloadedVisitor{[&div, this](const RegisterOperand&)
                                        { return this->EmitSpecificDiv<OperandCombination::RegisterToRegister>(div); },
                                        [&div, this](const IntegerOperand&)
                                        { return this->EmitSpecificDiv<OperandCombination::ImmediateToRegister>(div); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw ecpps::TracedException(std::logic_error("Invalid mul operation")); }},
                      div.from);
             },
             [&div, this](const MemoryLocationOperand&)
             {
                  return std::visit(
                      OverloadedVisitor{[&div, this](const RegisterOperand&)
                                        { return this->EmitSpecificDiv<OperandCombination::RegisterToMemory>(div); },
                                        [&div, this](const IntegerOperand&)
                                        { return this->EmitSpecificDiv<OperandCombination::ImmediateToMemory>(div); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw ecpps::TracedException(std::logic_error("Invalid mul operation")); }},
                      div.from);
             },
             [](auto&&) -> std::vector<std::byte>
             { throw ecpps::TracedException(std::logic_error("Invalid mul operation")); }},
         div.to);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitLea(const TakeAddressInstruction& lea)
{
     return std::visit(
         OverloadedVisitor{[&lea, this](const RegisterOperand&)
                           { return this->EmitSpecificLea<OperandCombination::MemoryToRegister>(lea); },
                           [](auto&&) -> std::vector<std::byte>
                           { throw ecpps::TracedException(std::logic_error("Invalid address-of operation")); }},
         lea.to);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitCall(const CallInstruction& call)
{
     this->_relocationTable.emplace(this->_currentInstructionBase, call.functionName);
     return {};
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitReturn(void) { return x86_64::GenerateRet(); }

std::size_t ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(const RegisterOperand& register_)
{
     const auto registerName = register_.Index()->physical->friendlyName;
     const static std::unordered_map<std::string, std::size_t> GeneralRegisterMap{
         {"rax", x86_64::Rax}, {"rcx", x86_64::Rcx}, {"rdx", x86_64::Rdx}, {"rbx", x86_64::Rbx},
         {"rsp", x86_64::Rsp}, {"rbp", x86_64::Rbp}, {"rsi", x86_64::Rsi}, {"rdi", x86_64::Rdi},

         {"r8", x86_64::R8},   {"r9", x86_64::R9},   {"r10", x86_64::R10}, {"r11", x86_64::R11},
         {"r12", x86_64::R12}, {"r13", x86_64::R13}, {"r14", x86_64::R14}, {"r15", x86_64::R15},
     };
     if (GeneralRegisterMap.contains(registerName)) return GeneralRegisterMap.at(registerName);

     return ~0ULL;
}

//
// Mov
//

template <>
struct ecpps::codegen::emitters::EmitSpecificMovImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MovInstruction& mov)
     {
          const auto& source = std::get<IntegerOperand>(mov.source);
          const auto& destination = std::get<MemoryLocationOperand>(mov.destination);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();

          switch (mov.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateMovImmToMem8(destinationRegister, destinationDisplacement,
                                                   static_cast<std::uint8_t>(sourceImmediate));
          case ecpps::abi::wordSize:
               return x86_64::GenerateMovImmToMem16(destinationRegister, destinationDisplacement,
                                                    static_cast<std::uint16_t>(sourceImmediate));
          case ecpps::abi::dwordSize:
               return x86_64::GenerateMovImmToMem32(destinationRegister, destinationDisplacement,
                                                    static_cast<std::uint32_t>(sourceImmediate));
          case ecpps::abi::qwordSize:
               return x86_64::GenerateMovImmToMem64(destinationRegister, destinationDisplacement,
                                                    static_cast<std::uint32_t>(sourceImmediate));
          default: throw TracedException(std::logic_error("Invalid mov operation"));
          }
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMovImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MovInstruction& mov)
     {
          const auto& source = std::get<IntegerOperand>(mov.source);
          const auto& destination = std::get<RegisterOperand>(mov.destination);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (mov.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateMovImmToReg8(destinationRegister, static_cast<std::uint8_t>(sourceImmediate));
          case ecpps::abi::wordSize:
               return x86_64::GenerateMovImmToReg16(destinationRegister, static_cast<std::uint16_t>(sourceImmediate));
          case ecpps::abi::dwordSize:
               return x86_64::GenerateMovImmToReg32(destinationRegister, static_cast<std::uint32_t>(sourceImmediate));
          case ecpps::abi::qwordSize:
               return x86_64::GenerateMovImmToReg64(destinationRegister, static_cast<std::uint64_t>(sourceImmediate));
          default: throw TracedException(std::logic_error("Invalid mov operation"));
          }
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMovImpl<ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MovInstruction& mov)
     {
          const auto& source = std::get<RegisterOperand>(mov.source);
          const auto& destination = std::get<MemoryLocationOperand>(mov.destination);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();

          switch (mov.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateMovRegToMem8(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::wordSize:
               return x86_64::GenerateMovRegToMem16(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::dwordSize:
               return x86_64::GenerateMovRegToMem32(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::qwordSize:
               return x86_64::GenerateMovRegToMem64(destinationRegister, destinationDisplacement, sourceRegister);
          default: throw TracedException(std::logic_error("Invalid mov operation"));
          }
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMovImpl<ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MovInstruction& mov)
     {
          const auto& source = std::get<RegisterOperand>(mov.source);
          const auto& destination = std::get<RegisterOperand>(mov.destination);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (mov.width)
          {
          case ecpps::abi::byteSize: return x86_64::GenerateMovRegToReg8(destinationRegister, sourceRegister);
          case ecpps::abi::wordSize: return x86_64::GenerateMovRegToReg16(destinationRegister, sourceRegister);
          case ecpps::abi::dwordSize: return x86_64::GenerateMovRegToReg32(destinationRegister, sourceRegister);
          case ecpps::abi::qwordSize: return x86_64::GenerateMovRegToReg64(destinationRegister, sourceRegister);
          }

          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

//
// convert
//

template <>
struct ecpps::codegen::emitters::EmitSpecificConversionImpl<
    ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self,
                                              [[maybe_unused]] const MovInstruction& mov)
     {
          throw TracedException(std::logic_error("not implemented"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificConversionImpl<
    ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self,
                                              [[maybe_unused]] const MovInstruction& mov)
     {
          throw TracedException(std::logic_error("not implemented"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificConversionImpl<
    ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self,
                                              [[maybe_unused]] const MovInstruction& mov)
     {
          throw TracedException(std::logic_error("not implemented"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificConversionImpl<
    ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self,
                                              [[maybe_unused]] const MovInstruction& mov)
     {
          const auto& source = std::get<RegisterOperand>(mov.source);
          const auto& destination = std::get<RegisterOperand>(mov.destination);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          using ecpps::abi::byteSize;
          using ecpps::abi::dwordSize;
          using ecpps::abi::qwordSize;
          using ecpps::abi::wordSize;

          const auto fromSize = source.Size();
          const auto toSize = destination.Size();

          runtime_assert(fromSize <= ecpps::abi::qwordSize && toSize <= ecpps::abi::qwordSize, "Invalid conversion");

          if (fromSize == toSize) [[unlikely]]
          {
               MovInstruction newMov{mov};
               newMov.isConversion = false;
               return self->EmitMov(newMov);
          }

          switch (static_cast<int>(toSize < fromSize))
          {
          case 1:
          {
               switch (toSize)
               {
               case byteSize: return x86_64::GenerateMovRegToReg8(destinationRegister, sourceRegister);
               case wordSize: return x86_64::GenerateMovRegToReg16(destinationRegister, sourceRegister);
               case dwordSize: return x86_64::GenerateMovRegToReg32(destinationRegister, sourceRegister);
               }
          }
          break;
          case 0:
          {
               switch (toSize)
               {
               case wordSize:
                    return fromSize == byteSize
                               ? x86_64::GenerateMovZeroExtendReg8ToReg16(destinationRegister, sourceRegister)
                               : throw TracedException(std::logic_error(
                                     std::format("Cannot move-extend {} bits to {} bits", fromSize, toSize)));
               case dwordSize:
                    return fromSize == byteSize
                               ? x86_64::GenerateMovZeroExtendReg8ToReg32(destinationRegister, sourceRegister)
                           : fromSize == wordSize
                               ? x86_64::GenerateMovZeroExtendReg16ToReg32(destinationRegister, sourceRegister)
                               : throw TracedException(std::logic_error(
                                     std::format("Cannot move-extend {} bits to {} bits", fromSize, toSize)));
               case qwordSize:
                    return fromSize == byteSize
                               ? x86_64::GenerateMovZeroExtendReg8ToReg64(destinationRegister, sourceRegister)
                           : fromSize == wordSize
                               ? x86_64::GenerateMovZeroExtendReg16ToReg64(destinationRegister, sourceRegister)
                           : fromSize == dwordSize
                               ? x86_64::GenerateMovZeroExtendReg32ToReg64(destinationRegister, sourceRegister)
                               : throw TracedException(std::logic_error(
                                     std::format("Cannot move-extend {} bits to {} bits", fromSize, toSize)));
               }
          }
          break;
          }

          throw TracedException("Not implemented");
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificConversionImpl<
    ecpps::codegen::emitters::OperandCombination::MemoryToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MovInstruction& mov)
     {
          using ecpps::abi::byteSize;
          using ecpps::abi::dwordSize;
          using ecpps::abi::qwordSize;
          using ecpps::abi::wordSize;

          const auto& from = std::get<MemoryLocationOperand>(mov.source);
          const auto& destinationOperand = std::get<RegisterOperand>(mov.destination);

          const auto fromSize = from.Size();
          const auto toSize = destinationOperand.Size();

          runtime_assert(fromSize <= ecpps::abi::qwordSize && toSize <= ecpps::abi::qwordSize, "Invalid conversion");

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(from.Register());
          const auto sourceRegisterOffset = from.Displacement();
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destinationOperand);

          if (fromSize == toSize) [[unlikely]]
               return self->EmitMov(mov);

          switch (static_cast<int>(toSize < fromSize))
          {
          case 1:
          {
               switch (toSize)
               {
               case byteSize:
                    return x86_64::GenerateMovMemToReg8(destinationRegister, sourceRegisterOffset, sourceRegister);
               case wordSize:
                    return x86_64::GenerateMovMemToReg16(destinationRegister, sourceRegisterOffset, sourceRegister);
               case dwordSize:
                    return x86_64::GenerateMovMemToReg32(destinationRegister, sourceRegisterOffset, sourceRegister);
               }
          }
          break;
          case 0:
          {
               switch (toSize)
               {
               case wordSize:
                    return fromSize == byteSize
                               ? x86_64::GenerateMovZeroExtendMem8ToReg16(destinationRegister, sourceRegisterOffset,
                                                                          sourceRegister)
                               : throw TracedException(std::logic_error(
                                     std::format("Cannot move-extend {} bits to {} bits", fromSize, toSize)));
               case dwordSize:
                    return fromSize == byteSize ? x86_64::GenerateMovZeroExtendMem8ToReg32(
                                                      destinationRegister, sourceRegisterOffset, sourceRegister)
                           : fromSize == wordSize
                               ? x86_64::GenerateMovZeroExtendMem16ToReg32(destinationRegister, sourceRegisterOffset,
                                                                           sourceRegister)
                               : throw TracedException(std::logic_error(
                                     std::format("Cannot move-extend {} bits to {} bits", fromSize, toSize)));
               case qwordSize:
                    return fromSize == byteSize ? x86_64::GenerateMovZeroExtendMem8ToReg64(
                                                      destinationRegister, sourceRegisterOffset, sourceRegister)
                           : fromSize == wordSize ? x86_64::GenerateMovZeroExtendMem16ToReg64(
                                                        destinationRegister, sourceRegisterOffset, sourceRegister)
                           : fromSize == dwordSize
                               ? x86_64::GenerateMovZeroExtendMem32ToReg64(destinationRegister, sourceRegisterOffset,
                                                                           sourceRegister)
                               : throw TracedException(std::logic_error(
                                     std::format("Cannot move-extend {} bits to {} bits", fromSize, toSize)));
               }
          }
          break;
          }

          throw TracedException("Not implemented");
     }
};

//
// Add
//

template <>
struct ecpps::codegen::emitters::EmitSpecificAddImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const AddInstruction& add)
     {
          const auto& source = std::get<IntegerOperand>(add.from);
          const auto& destination = std::get<MemoryLocationOperand>(add.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();

          switch (add.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateAddImmToMem8(destinationRegister, destinationDisplacement,
                                                   static_cast<std::uint8_t>(sourceImmediate));
          case ecpps::abi::wordSize:
               return x86_64::GenerateAddImmToMem16(destinationRegister, destinationDisplacement,
                                                    static_cast<std::uint16_t>(sourceImmediate));
          case ecpps::abi::dwordSize:
               return x86_64::GenerateAddImmToMem32(destinationRegister, destinationDisplacement,
                                                    static_cast<std::uint32_t>(sourceImmediate));
          case ecpps::abi::qwordSize:
               return x86_64::GenerateAddImmToMem64(destinationRegister, destinationDisplacement,
                                                    static_cast<std::uint64_t>(sourceImmediate));
          default: throw TracedException(std::logic_error("Invalid mov operation"));
          }
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMovImpl<ecpps::codegen::emitters::OperandCombination::MemoryToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MovInstruction& mov)
     {
          const auto& source = std::get<MemoryLocationOperand>(mov.source);
          const auto& destination = std::get<RegisterOperand>(mov.destination);

          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);
          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source.Register());
          const auto sourceDisplacement = source.Displacement();

          switch (mov.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateMovMemToReg8(destinationRegister, sourceDisplacement, sourceRegister);
          case ecpps::abi::wordSize:
               return x86_64::GenerateMovMemToReg16(destinationRegister, sourceDisplacement, sourceRegister);
          case ecpps::abi::dwordSize:
               return x86_64::GenerateMovMemToReg32(destinationRegister, sourceDisplacement, sourceRegister);
          case ecpps::abi::qwordSize:
               return x86_64::GenerateMovMemToReg64(destinationRegister, sourceDisplacement, sourceRegister);
          default: throw TracedException(std::logic_error("Invalid mov operation"));
          }
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificAddImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const AddInstruction& add)
     {
          const auto& source = std::get<IntegerOperand>(add.from);
          const auto& destination = std::get<RegisterOperand>(add.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (add.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateAddImmToReg8(destinationRegister, static_cast<std::uint8_t>(sourceImmediate));
          case ecpps::abi::wordSize:
               return x86_64::GenerateAddImmToReg16(destinationRegister, static_cast<std::uint16_t>(sourceImmediate));
          case ecpps::abi::dwordSize:
               return x86_64::GenerateAddImmToReg32(destinationRegister, static_cast<std::uint32_t>(sourceImmediate));
          case ecpps::abi::qwordSize:
               return x86_64::GenerateAddImmToReg64(destinationRegister, static_cast<std::uint64_t>(sourceImmediate));
          default: throw TracedException(std::logic_error("Invalid add operation"));
          }
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificAddImpl<ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const AddInstruction& add)
     {
          const auto& source = std::get<RegisterOperand>(add.from);
          const auto& destination = std::get<MemoryLocationOperand>(add.to);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();

          switch (add.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateAddRegToMem8(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::wordSize:
               return x86_64::GenerateAddRegToMem16(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::dwordSize:
               return x86_64::GenerateAddRegToMem32(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::qwordSize:
               return x86_64::GenerateAddRegToMem64(destinationRegister, destinationDisplacement, sourceRegister);
          default: throw TracedException(std::logic_error("Invalid add operation"));
          }
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificAddImpl<ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const AddInstruction& add)
     {
          const auto& source = std::get<RegisterOperand>(add.from);
          const auto& destination = std::get<RegisterOperand>(add.to);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (add.width)
          {
          case ecpps::abi::byteSize: return x86_64::GenerateAddRegToReg8(destinationRegister, sourceRegister);
          case ecpps::abi::wordSize: return x86_64::GenerateAddRegToReg16(destinationRegister, sourceRegister);
          case ecpps::abi::dwordSize: return x86_64::GenerateAddRegToReg32(destinationRegister, sourceRegister);
          case ecpps::abi::qwordSize: return x86_64::GenerateAddRegToReg64(destinationRegister, sourceRegister);
          default: throw TracedException(std::logic_error("Invalid add operation"));
          }
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificAddImpl<ecpps::codegen::emitters::OperandCombination::MemoryToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const AddInstruction& add)
     {
          const auto& source = std::get<MemoryLocationOperand>(add.from);
          const auto& destination = std::get<RegisterOperand>(add.to);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source.Register());
          const auto sourceOffset = source.Displacement();
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (add.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateAddMemToReg8(destinationRegister, sourceOffset, sourceRegister);
          case ecpps::abi::wordSize:
               return x86_64::GenerateAddMemToReg16(destinationRegister, sourceOffset, sourceRegister);
          case ecpps::abi::dwordSize:
               return x86_64::GenerateAddMemToReg32(destinationRegister, sourceOffset, sourceRegister);
          case ecpps::abi::qwordSize:
               return x86_64::GenerateAddMemToReg64(destinationRegister, sourceOffset, sourceRegister);
          default: throw TracedException(std::logic_error("Invalid add operation"));
          }
     }
};

//
// Sub
//

template <>
struct ecpps::codegen::emitters::EmitSpecificSubImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const SubInstruction& sub)
     {
          const auto& source = std::get<IntegerOperand>(sub.from);
          const auto& destination = std::get<MemoryLocationOperand>(sub.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();

          switch (sub.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateSubImmToMem8(destinationRegister, destinationDisplacement,
                                                   static_cast<std::uint8_t>(sourceImmediate));
          case ecpps::abi::wordSize:
               return x86_64::GenerateSubImmToMem16(destinationRegister, destinationDisplacement,
                                                    static_cast<std::uint16_t>(sourceImmediate));
          case ecpps::abi::dwordSize:
               return x86_64::GenerateSubImmToMem32(destinationRegister, destinationDisplacement,
                                                    static_cast<std::uint32_t>(sourceImmediate));
          case ecpps::abi::qwordSize:
               return x86_64::GenerateSubImmToMem64(destinationRegister, destinationDisplacement,
                                                    static_cast<std::uint64_t>(sourceImmediate));
          default: throw TracedException(std::logic_error("Invalid sub operation"));
          }
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificSubImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const SubInstruction& sub)
     {
          const auto& source = std::get<IntegerOperand>(sub.from);
          const auto& destination = std::get<RegisterOperand>(sub.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (sub.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateSubImmToReg8(destinationRegister, static_cast<std::uint8_t>(sourceImmediate));
          case ecpps::abi::wordSize:
               return x86_64::GenerateSubImmToReg16(destinationRegister, static_cast<std::uint16_t>(sourceImmediate));
          case ecpps::abi::dwordSize:
               return x86_64::GenerateSubImmToReg32(destinationRegister, static_cast<std::uint32_t>(sourceImmediate));
          case ecpps::abi::qwordSize:
               return x86_64::GenerateSubImmToReg64(destinationRegister, static_cast<std::uint64_t>(sourceImmediate));
          }

          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificSubImpl<ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const SubInstruction& sub)
     {
          const auto& source = std::get<RegisterOperand>(sub.from);
          const auto& destination = std::get<MemoryLocationOperand>(sub.to);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();

          switch (sub.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateSubRegToMem8(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::wordSize:
               return x86_64::GenerateSubRegToMem16(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::dwordSize:
               return x86_64::GenerateSubRegToMem32(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::qwordSize:
               return x86_64::GenerateSubRegToMem64(destinationRegister, destinationDisplacement, sourceRegister);
          }

          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificSubImpl<ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const SubInstruction& sub)
     {
          const auto& source = std::get<RegisterOperand>(sub.from);
          const auto& destination = std::get<RegisterOperand>(sub.to);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (sub.width)
          {
          case ecpps::abi::byteSize: return x86_64::GenerateSubRegToReg8(destinationRegister, sourceRegister);
          case ecpps::abi::wordSize: return x86_64::GenerateSubRegToReg16(destinationRegister, sourceRegister);
          case ecpps::abi::dwordSize: return x86_64::GenerateSubRegToReg32(destinationRegister, sourceRegister);
          case ecpps::abi::qwordSize: return x86_64::GenerateSubRegToReg64(destinationRegister, sourceRegister);
          }

          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificSubImpl<ecpps::codegen::emitters::OperandCombination::MemoryToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const SubInstruction& sub)
     {
          const auto& source = std::get<MemoryLocationOperand>(sub.from);
          const auto& destination = std::get<RegisterOperand>(sub.to);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source.Register());
          const auto sourceRegisterOffset = source.Displacement();
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (sub.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateSubMemToReg8(destinationRegister, sourceRegisterOffset, sourceRegister);
          case ecpps::abi::wordSize:
               return x86_64::GenerateSubMemToReg16(destinationRegister, sourceRegisterOffset, sourceRegister);
          case ecpps::abi::dwordSize:
               return x86_64::GenerateSubMemToReg32(destinationRegister, sourceRegisterOffset, sourceRegister);
          case ecpps::abi::qwordSize:
               return x86_64::GenerateSubMemToReg64(destinationRegister, sourceRegisterOffset, sourceRegister);
          }

          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

//
// Mul
//

template <>
struct ecpps::codegen::emitters::EmitSpecificMulImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MulInstruction& mul)
     {
          const auto& source = std::get<IntegerOperand>(mul.from);
          const auto& destination = std::get<MemoryLocationOperand>(mul.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();

          switch (mul.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateSignedMulImmToMem8(destinationRegister, destinationDisplacement,
                                                         static_cast<std::uint8_t>(sourceImmediate));
          case ecpps::abi::wordSize:
               return x86_64::GenerateSignedMulImmToMem16(destinationRegister, destinationDisplacement,
                                                          static_cast<std::uint16_t>(sourceImmediate));
          case ecpps::abi::dwordSize:
               return x86_64::GenerateSignedMulImmToMem32(destinationRegister, destinationDisplacement,
                                                          static_cast<std::uint32_t>(sourceImmediate));
          case ecpps::abi::qwordSize:
               return x86_64::GenerateSignedMulImmToMem64(destinationRegister, destinationDisplacement,
                                                          static_cast<std::uint64_t>(sourceImmediate));
          }

          throw ecpps::TracedException(std::logic_error("Invalid mul operation"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMulImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MulInstruction& mul)
     {
          const auto& source = std::get<IntegerOperand>(mul.from);
          const auto& destination = std::get<RegisterOperand>(mul.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (mul.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateSignedMulImmToReg8(destinationRegister,
                                                         static_cast<std::uint8_t>(sourceImmediate));
          case ecpps::abi::wordSize:
               return x86_64::GenerateSignedMulImmToReg16(destinationRegister,
                                                          static_cast<std::uint16_t>(sourceImmediate));
          case ecpps::abi::dwordSize:
               return x86_64::GenerateSignedMulImmToReg32(destinationRegister,
                                                          static_cast<std::uint32_t>(sourceImmediate));
          case ecpps::abi::qwordSize:
               return x86_64::GenerateSignedMulImmToReg64(destinationRegister,
                                                          static_cast<std::uint64_t>(sourceImmediate));
          }

          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMulImpl<ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MulInstruction& mul)
     {
          const auto& source = std::get<RegisterOperand>(mul.from);
          const auto& destination = std::get<MemoryLocationOperand>(mul.to);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();

          switch (mul.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateSignedMulRegToMem8(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::wordSize:
               return x86_64::GenerateSignedMulRegToMem16(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::dwordSize:
               return x86_64::GenerateSignedMulRegToMem32(destinationRegister, destinationDisplacement, sourceRegister);
          case ecpps::abi::qwordSize:
               return x86_64::GenerateSignedMulRegToMem64(destinationRegister, destinationDisplacement, sourceRegister);
          }

          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMulImpl<ecpps::codegen::emitters::OperandCombination::MemoryToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MulInstruction& mul)
     {
          const auto& source = std::get<MemoryLocationOperand>(mul.from);
          const auto& destination = std::get<RegisterOperand>(mul.to);

          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);
          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source.Register());
          const auto sourceDisplacement = source.Displacement();

          switch (mul.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateSignedMulMemToReg8(destinationRegister, sourceDisplacement, sourceRegister);
          case ecpps::abi::wordSize:
               return x86_64::GenerateSignedMulMemToReg16(destinationRegister, sourceDisplacement, sourceRegister);
          case ecpps::abi::dwordSize:
               return x86_64::GenerateSignedMulMemToReg32(destinationRegister, sourceDisplacement, sourceRegister);
          case ecpps::abi::qwordSize:
               return x86_64::GenerateSignedMulMemToReg64(destinationRegister, sourceDisplacement, sourceRegister);
          }

          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMulImpl<ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const MulInstruction& mul)
     {
          const auto& source = std::get<RegisterOperand>(mul.from);
          const auto& destination = std::get<RegisterOperand>(mul.to);

          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          switch (mul.width)
          {
          case ecpps::abi::byteSize: return x86_64::GenerateSignedMulRegToReg8(destinationRegister, sourceRegister);
          case ecpps::abi::wordSize: return x86_64::GenerateSignedMulRegToReg16(destinationRegister, sourceRegister);
          case ecpps::abi::dwordSize: return x86_64::GenerateSignedMulRegToReg32(destinationRegister, sourceRegister);
          case ecpps::abi::qwordSize: return x86_64::GenerateSignedMulRegToReg64(destinationRegister, sourceRegister);
          }

          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

//
// Div
//

template <>
struct ecpps::codegen::emitters::EmitSpecificDivImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const DivInstruction& div)
     {
          const auto& source = std::get<IntegerOperand>(div.from);
          const auto& destination = std::get<MemoryLocationOperand>(div.to);

#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
          const auto sourceImmediate = source.Value();
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();
#ifdef __clang__
#pragma GCC diagnostic pop
#endif

          throw ecpps::TracedException(std::logic_error("Invalid mul operation"));
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificDivImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const DivInstruction& div)
     {
          const auto& source = std::get<IntegerOperand>(div.from);
          const auto& destination = std::get<RegisterOperand>(div.to);

          const auto sourceImmediate = source.Value();
          const auto destReg = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);

          std::vector<std::byte> code{};

          // mov dividend immediate -> RAX/AX/AL
          switch (div.width)
          {
          case ecpps::abi::byteSize:
          {
               auto movBytes = x86_64::GenerateMovImmToReg8(0, static_cast<std::uint8_t>(sourceImmediate));
               code.insert(code.end(), movBytes.begin(), movBytes.end());
               auto movRdxBytes = x86_64::GenerateXorReg64(2, 2);
               code.insert(code.end(), movRdxBytes.begin(), movRdxBytes.end());
               break;
          }
          case ecpps::abi::wordSize:
          {
               auto movBytes = x86_64::GenerateMovImmToReg16(0, static_cast<std::uint16_t>(sourceImmediate));
               code.insert(code.end(), movBytes.begin(), movBytes.end());
               auto movDxBytes = x86_64::GenerateXorReg16(2, 2);
               code.insert(code.end(), movDxBytes.begin(), movDxBytes.end());
               break;
          }
          case ecpps::abi::dwordSize:
          {
               auto movBytes = x86_64::GenerateMovImmToReg32(0, static_cast<std::uint32_t>(sourceImmediate));
               code.insert(code.end(), movBytes.begin(), movBytes.end());
               auto movEdxBytes = x86_64::GenerateXorReg32(2, 2);
               code.insert(code.end(), movEdxBytes.begin(), movEdxBytes.end());
               break;
          }
          case ecpps::abi::qwordSize:
          {
               auto movBytes = x86_64::GenerateMovImmToReg64(0, static_cast<std::uint64_t>(sourceImmediate));
               code.insert(code.end(), movBytes.begin(), movBytes.end());
               auto movRdxBytes = x86_64::GenerateXorReg64(2, 2);
               code.insert(code.end(), movRdxBytes.begin(), movRdxBytes.end());
               break;
          }
          default: throw ecpps::TracedException(std::logic_error("Invalid div width"));
          }

          // emit div instruction with divisor in register (destReg)
          std::vector<std::byte> divBytes{};
          switch (div.width)
          {
          case ecpps::abi::byteSize: divBytes = x86_64::GenerateSignedDiv8(destReg); break;
          case ecpps::abi::wordSize: divBytes = x86_64::GenerateSignedDiv16(destReg); break;
          case ecpps::abi::dwordSize: divBytes = x86_64::GenerateSignedDiv32(destReg); break;
          case ecpps::abi::qwordSize: divBytes = x86_64::GenerateSignedDiv64(destReg); break;
          }
          code.insert(code.end(), divBytes.begin(), divBytes.end());

          return code;
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificDivImpl<ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const DivInstruction& div)
     {
          // NOLINTBEGIN(clang-analyzer-deadcode.DeadStores)
          const auto& source = std::get<RegisterOperand>(div.from);
          const auto& destination = std::get<MemoryLocationOperand>(div.to);
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister =
              ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination.Register());
          const auto destinationDisplacement = destination.Displacement();
#ifdef __clang__
#pragma GCC diagnostic pop
#endif
          throw TracedException(std::logic_error("Invalid mov operation"));
          // NOLINTEND(clang-analyzer-deadcode.DeadStores)
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificDivImpl<ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self, const DivInstruction& div)
     {
          const auto& source = std::get<RegisterOperand>(div.from);
          const auto& destination = std::get<RegisterOperand>(div.to);
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source);
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);
#ifdef __clang__
#pragma GCC diagnostic pop
#endif
          throw TracedException(std::logic_error("Invalid mov operation"));
     }
};

// lea

template <>
struct ecpps::codegen::emitters::EmitSpecificLeaImpl<ecpps::codegen::emitters::OperandCombination::MemoryToRegister>
{
     static std::vector<std::byte> operator()([[maybe_unused]] X8664Emitter* self,
                                              [[maybe_unused]] const TakeAddressInstruction& lea)
     {

          constexpr static auto ApplyStringRelocationLambda = [](std::uint8_t register_, std::size_t stringTableOffset)
          { return x86_64::GenerateLeaToReg(x86_64::Rip, stringTableOffset, register_); };

          const auto& source = lea.from;
          const auto& destination = std::get<RegisterOperand>(lea.to);

          const auto sourceRegisterOffset = source.Displacement();
          const auto destinationRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(destination);
          if (source.Register().Index() == abi::ABI::Current().StringRegister())
          {
               self->_stringRelocation.push_back(self->_currentInstructionBase + 3); // instruction length: 3 + DWORD
               return x86_64::GenerateLeaToReg(x86_64::Rip, sourceRegisterOffset - self->_currentInstructionBase - 7,
                                               destinationRegister);
          }
          const auto sourceRegister = ecpps::codegen::emitters::X8664Emitter::RegisterToIndex(source.Register());

          return x86_64::GenerateLeaToReg(sourceRegister, sourceRegisterOffset, destinationRegister);
     }
};
