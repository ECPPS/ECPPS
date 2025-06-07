#include "x86_64.h"
#include <stdexcept>
#include "../../Machine/ABI.h"
#include "../../Parsing/Tokeniser.h"
#include "x86_64/Opcodes.h"

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitMov(const MovInstruction& mov)
{
     return std::visit(
         OverloadedVisitor{
             [&mov, this](const RegisterOperand& registerDestination)
             {
                  return std::visit(
                      OverloadedVisitor{[&mov, this](const RegisterOperand& registerSource)
                                        { return this->EmitSpecificMov<OperandCombination::RegisterToRegister>(mov); },
                                        [&mov, this](const IntegerOperand& integerSource)
                                        { return this->EmitSpecificMov<OperandCombination::ImmediateToRegister>(mov); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw std::logic_error("Invalid mov operation"); }},
                      mov.source);
             },
             [&mov, this](const MemoryLocationOperand& memoryDestination)
             {
                  return std::visit(
                      OverloadedVisitor{[&mov, this](const RegisterOperand& registerSource)
                                        { return this->EmitSpecificMov<OperandCombination::RegisterToMemory>(mov); },
                                        [&mov, this](const IntegerOperand& integerSource)
                                        { return this->EmitSpecificMov<OperandCombination::ImmediateToMemory>(mov); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw std::logic_error("Invalid mov operation"); }},
                      mov.source);
             },
             [](auto&&) -> std::vector<std::byte> { throw std::logic_error("Invalid mov operation"); }},
         mov.destination);
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

template <>
struct ecpps::codegen::emitters::EmitSpecificMovImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const MovInstruction& mov)
     {
          const IntegerOperand& source = std::get<IntegerOperand>(mov.source);
          const MemoryLocationOperand& destination = std::get<MemoryLocationOperand>(mov.destination);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = self->RegisterToIndex(destination.Register());
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
                                                    static_cast<std::uint64_t>(sourceImmediate));
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMovImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const MovInstruction& mov)
     {
          const IntegerOperand& source = std::get<IntegerOperand>(mov.source);
          const RegisterOperand& destination = std::get<RegisterOperand>(mov.destination);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = self->RegisterToIndex(destination);

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
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMovImpl<ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const MovInstruction& mov)
     {
          const RegisterOperand& source = std::get<RegisterOperand>(mov.source);
          const MemoryLocationOperand& destination = std::get<MemoryLocationOperand>(mov.destination);

          const auto sourceRegister = self->RegisterToIndex(source);
          const auto destinationRegister = self->RegisterToIndex(destination.Register());
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
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMovImpl<ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const MovInstruction& mov)
     {
          const RegisterOperand& source = std::get<RegisterOperand>(mov.source);
          const RegisterOperand& destination = std::get<RegisterOperand>(mov.destination);

          const auto sourceRegister = self->RegisterToIndex(source);
          const auto destinationRegister = self->RegisterToIndex(destination);

          switch (mov.width)
          {
          case ecpps::abi::byteSize: return x86_64::GenerateMovRegToReg8(destinationRegister, sourceRegister);
          case ecpps::abi::wordSize: return x86_64::GenerateMovRegToReg16(destinationRegister, sourceRegister);
          case ecpps::abi::dwordSize: return x86_64::GenerateMovRegToReg32(destinationRegister, sourceRegister);
          case ecpps::abi::qwordSize: return x86_64::GenerateMovRegToReg64(destinationRegister, sourceRegister);
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};