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

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitAdd(const AddInstruction& add)
{
     return std::visit(
         OverloadedVisitor{
             [&add, this](const RegisterOperand& registerDestination)
             {
                  return std::visit(
                      OverloadedVisitor{[&add, this](const RegisterOperand& registerSource)
                                        { return this->EmitSpecificAdd<OperandCombination::RegisterToRegister>(add); },
                                        [&add, this](const IntegerOperand& integerSource)
                                        { return this->EmitSpecificAdd<OperandCombination::ImmediateToRegister>(add); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw std::logic_error("Invalid add operation"); }},
                      add.from);
             },
             [&add, this](const MemoryLocationOperand& memoryDestination)
             {
                  return std::visit(
                      OverloadedVisitor{[&add, this](const RegisterOperand& registerSource)
                                        { return this->EmitSpecificAdd<OperandCombination::RegisterToMemory>(add); },
                                        [&add, this](const IntegerOperand& integerSource)
                                        { return this->EmitSpecificAdd<OperandCombination::ImmediateToMemory>(add); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw std::logic_error("Invalid add operation"); }},
                      add.from);
             },
             [](auto&&) -> std::vector<std::byte> { throw std::logic_error("Invalid add operation"); }},
         add.to);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitSub(const SubInstruction& sub)
{
     return std::visit(
         OverloadedVisitor{
             [&sub, this](const RegisterOperand& registerDestination)
             {
                  return std::visit(
                      OverloadedVisitor{[&sub, this](const RegisterOperand& registerSource)
                                        { return this->EmitSpecificSub<OperandCombination::RegisterToRegister>(sub); },
                                        [&sub, this](const IntegerOperand& integerSource)
                                        { return this->EmitSpecificSub<OperandCombination::ImmediateToRegister>(sub); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw std::logic_error("Invalid sub operation"); }},
                      sub.from);
             },
             [&sub, this](const MemoryLocationOperand& memoryDestination)
             {
                  return std::visit(
                      OverloadedVisitor{[&sub, this](const RegisterOperand& registerSource)
                                        { return this->EmitSpecificSub<OperandCombination::RegisterToMemory>(sub); },
                                        [&sub, this](const IntegerOperand& integerSource)
                                        { return this->EmitSpecificSub<OperandCombination::ImmediateToMemory>(sub); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw std::logic_error("Invalid sub operation"); }},
                      sub.from);
             },
             [](auto&&) -> std::vector<std::byte> { throw std::logic_error("Invalid sub operation"); }},
         sub.to);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitMul(const MulInstruction& mul)
{
     return std::visit(
         OverloadedVisitor{
             [&mul, this](const RegisterOperand& registerDestination)
             {
                  return std::visit(
                      OverloadedVisitor{[&mul, this](const RegisterOperand& registerSource)
                                        { return this->EmitSpecificMul<OperandCombination::RegisterToRegister>(mul); },
                                        [&mul, this](const IntegerOperand& integerSource)
                                        { return this->EmitSpecificMul<OperandCombination::ImmediateToRegister>(mul); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw std::logic_error("Invalid mul operation"); }},
                      mul.from);
             },
             [&mul, this](const MemoryLocationOperand& memoryDestination)
             {
                  return std::visit(
                      OverloadedVisitor{[&mul, this](const RegisterOperand& registerSource)
                                        { return this->EmitSpecificMul<OperandCombination::RegisterToMemory>(mul); },
                                        [&mul, this](const IntegerOperand& integerSource)
                                        { return this->EmitSpecificMul<OperandCombination::ImmediateToMemory>(mul); },
                                        [](auto&&) -> std::vector<std::byte>
                                        { throw std::logic_error("Invalid mul operation"); }},
                      mul.from);
             },
             [](auto&&) -> std::vector<std::byte> { throw std::logic_error("Invalid mul operation"); }},
         mul.to);
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitDiv(const DivInstruction& div)
{
     return std::vector<std::byte>();
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

//
// Add
//

template <>
struct ecpps::codegen::emitters::EmitSpecificAddImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const AddInstruction& add)
     {
          const IntegerOperand& source = std::get<IntegerOperand>(add.from);
          const MemoryLocationOperand& destination = std::get<MemoryLocationOperand>(add.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = self->RegisterToIndex(destination.Register());
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
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificAddImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const AddInstruction& add)
     {
          const IntegerOperand& source = std::get<IntegerOperand>(add.from);
          const RegisterOperand& destination = std::get<RegisterOperand>(add.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = self->RegisterToIndex(destination);

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
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificAddImpl<ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const AddInstruction& add)
     {
          const RegisterOperand& source = std::get<RegisterOperand>(add.from);
          const MemoryLocationOperand& destination = std::get<MemoryLocationOperand>(add.to);

          const auto sourceRegister = self->RegisterToIndex(source);
          const auto destinationRegister = self->RegisterToIndex(destination.Register());
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
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificAddImpl<ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const AddInstruction& add)
     {
          const RegisterOperand& source = std::get<RegisterOperand>(add.from);
          const RegisterOperand& destination = std::get<RegisterOperand>(add.to);

          const auto sourceRegister = self->RegisterToIndex(source);
          const auto destinationRegister = self->RegisterToIndex(destination);

          switch (add.width)
          {
          case ecpps::abi::byteSize: return x86_64::GenerateAddRegToReg8(destinationRegister, sourceRegister);
          case ecpps::abi::wordSize: return x86_64::GenerateAddRegToReg16(destinationRegister, sourceRegister);
          case ecpps::abi::dwordSize: return x86_64::GenerateAddRegToReg32(destinationRegister, sourceRegister);
          case ecpps::abi::qwordSize: return x86_64::GenerateAddRegToReg64(destinationRegister, sourceRegister);
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

//
// Sub
//

template <>
struct ecpps::codegen::emitters::EmitSpecificSubImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const SubInstruction& sub)
     {
          const IntegerOperand& source = std::get<IntegerOperand>(sub.from);
          const MemoryLocationOperand& destination = std::get<MemoryLocationOperand>(sub.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = self->RegisterToIndex(destination.Register());
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
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificSubImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const SubInstruction& sub)
     {
          const IntegerOperand& source = std::get<IntegerOperand>(sub.from);
          const RegisterOperand& destination = std::get<RegisterOperand>(sub.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = self->RegisterToIndex(destination);

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

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificSubImpl<ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const SubInstruction& sub)
     {
          const RegisterOperand& source = std::get<RegisterOperand>(sub.from);
          const MemoryLocationOperand& destination = std::get<MemoryLocationOperand>(sub.to);

          const auto sourceRegister = self->RegisterToIndex(source);
          const auto destinationRegister = self->RegisterToIndex(destination.Register());
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

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificSubImpl<ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const SubInstruction& sub)
     {
          const RegisterOperand& source = std::get<RegisterOperand>(sub.from);
          const RegisterOperand& destination = std::get<RegisterOperand>(sub.to);

          const auto sourceRegister = self->RegisterToIndex(source);
          const auto destinationRegister = self->RegisterToIndex(destination);

          switch (sub.width)
          {
          case ecpps::abi::byteSize: return x86_64::GenerateSubRegToReg8(destinationRegister, sourceRegister);
          case ecpps::abi::wordSize: return x86_64::GenerateSubRegToReg16(destinationRegister, sourceRegister);
          case ecpps::abi::dwordSize: return x86_64::GenerateSubRegToReg32(destinationRegister, sourceRegister);
          case ecpps::abi::qwordSize: return x86_64::GenerateSubRegToReg64(destinationRegister, sourceRegister);
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

//
// Mul
//

template <>
struct ecpps::codegen::emitters::EmitSpecificMulImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToMemory>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const MulInstruction& mul)
     {
          const IntegerOperand& source = std::get<IntegerOperand>(mul.from);
          const MemoryLocationOperand& destination = std::get<MemoryLocationOperand>(mul.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = self->RegisterToIndex(destination.Register());
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

          throw std::logic_error("Invalid mul operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMulImpl<ecpps::codegen::emitters::OperandCombination::ImmediateToRegister>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const MulInstruction& mul)
     {
          const IntegerOperand& source = std::get<IntegerOperand>(mul.from);
          const RegisterOperand& destination = std::get<RegisterOperand>(mul.to);

          const auto sourceImmediate = source.Value();
          const auto destinationRegister = self->RegisterToIndex(destination);

          switch (mul.width)
          {
          case ecpps::abi::byteSize:
               return x86_64::GenerateSignedMulImmToReg8(destinationRegister, static_cast<std::uint8_t>(sourceImmediate));
          case ecpps::abi::wordSize:
               return x86_64::GenerateSignedMulImmToReg16(destinationRegister, static_cast<std::uint16_t>(sourceImmediate));
          case ecpps::abi::dwordSize:
               return x86_64::GenerateSignedMulImmToReg32(destinationRegister, static_cast<std::uint32_t>(sourceImmediate));
          case ecpps::abi::qwordSize:
               return x86_64::GenerateSignedMulImmToReg64(destinationRegister, static_cast<std::uint64_t>(sourceImmediate));
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMulImpl<ecpps::codegen::emitters::OperandCombination::RegisterToMemory>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const MulInstruction& mul)
     {
          const RegisterOperand& source = std::get<RegisterOperand>(mul.from);
          const MemoryLocationOperand& destination = std::get<MemoryLocationOperand>(mul.to);

          const auto sourceRegister = self->RegisterToIndex(source);
          const auto destinationRegister = self->RegisterToIndex(destination.Register());
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

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};

template <>
struct ecpps::codegen::emitters::EmitSpecificMulImpl<ecpps::codegen::emitters::OperandCombination::RegisterToRegister>
{
     static std::vector<std::byte> Go(X8664Emitter* self, const MulInstruction& mul)
     {
          const RegisterOperand& source = std::get<RegisterOperand>(mul.from);
          const RegisterOperand& destination = std::get<RegisterOperand>(mul.to);

          const auto sourceRegister = self->RegisterToIndex(source);
          const auto destinationRegister = self->RegisterToIndex(destination);

          switch (mul.width)
          {
          case ecpps::abi::byteSize: return x86_64::GenerateSignedMulRegToReg8(destinationRegister, sourceRegister);
          case ecpps::abi::wordSize: return x86_64::GenerateSignedMulRegToReg16(destinationRegister, sourceRegister);
          case ecpps::abi::dwordSize: return x86_64::GenerateSignedMulRegToReg32(destinationRegister, sourceRegister);
          case ecpps::abi::qwordSize: return x86_64::GenerateSignedMulRegToReg64(destinationRegister, sourceRegister);
          }

          throw std::logic_error("Invalid mov operation");
          return {};
     }
};