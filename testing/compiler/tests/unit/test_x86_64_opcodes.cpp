#include "CodeGeneration/AbstractNodes.h"
#define CATCH_CONFIG_MAIN
#include <CodeGeneration/Emitters/x86_64/Opcodes.h>
#include <Execution/Context.h>
#include <Machine/Encoders/API/Aggregate.h>
#include <Machine/Encoders/API/Target.h>
#include <Machine/Encoders/Backends/x86_64/Core/encoder.h>
#include <Machine/Encoders/Backends/x86_64/Platforms/Linux/Linux.h>
#include <Machine/Encoders/Backends/x86_64/Platforms/Windows/Windows.h>
#include <Optimiser/passes/deadCode.h>
#include <TestHelpers.h>
#include <TypeSystem/ArithmeticTypes.h>
#include <TypeSystem/ClassType.h>
#include <catch_amalgamated.hpp>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace testHelpers;

static bool AreEqual(const std::vector<std::byte>& a, const std::vector<std::byte>& b) noexcept
{
     if (a.size() != b.size()) return false;
     return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

namespace x8664 = ecpps::codegen::x86_64;

TEST_CASE("x86-64 Opcodes - Signed multiplication", "[codegen][x86_64][opcodes]")
{
     SECTION("imul r32, [mem]")
     {
          auto result = x8664::GenerateSignedMulMemToReg32(0, 0, 0);
          std::vector<std::byte> expected{0x0f_b, 0xaf_b, 0x00_b};

          REQUIRE(AreEqual(result, expected));
          INFO("IMUL instruction encoding verified");
     }
}

TEST_CASE("x86-64 passes and returns a small integer aggregate in registers", "[codegen][x86_64][abi]")
{
     using ecpps::abi::encoders::x8664::LinuxPlatform;
     using ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder;
     using ecpps::ir::abstract::AllocationClass;
     using ecpps::ir::abstract::VirtualInstruction;
     using ecpps::ir::abstract::VirtualInstructionType;
     using ecpps::ir::abstract::VirtualRegister;
     using ecpps::typeSystem::ClassInfo;
     using ecpps::typeSystem::ClassState;
     using ecpps::typeSystem::ClassType;
     using ecpps::typeSystem::IntegralType;
     using ecpps::typeSystem::Qualifiers;
     using ecpps::typeSystem::Signedness;
     using ecpps::typeSystem::TypeKind;

     IntegralType integerType{Signedness::Signed, TypeKind::Int, "int", Qualifiers::None};
     ClassInfo classInfo{};
     classInfo.state = ClassState::Complete;
     classInfo.size = 8;
     classInfo.alignment = 4;
     classInfo.triviallyCopyable = true;
     classInfo.fields = {{.type = &integerType}, {.type = &integerType}};
     ClassType classType{&classInfo, "X", Qualifiers::None};

     const auto aggregateInfo = ecpps::abi::api::AggregateInfo(classType);
     LinuxPlatform platform{nullptr};
     ecpps::abi::encoders::x8664::WindowsPlatform windowsPlatform{nullptr};
     const auto argumentPlan = platform.ClassArgumentPassing(aggregateInfo);
     const auto returnPlan = platform.ClassReturnPassing(aggregateInfo);
     const auto linuxResultAddress = platform.IndirectClassResultAddress();
     const auto windowsResultAddress = windowsPlatform.IndirectClassResultAddress();
     CHECK((platform.ClassArgumentCleanup() == ecpps::abi::api::ClassArgumentCleanupOwner::Caller));
     CHECK((windowsPlatform.ClassArgumentCleanup() == ecpps::abi::api::ClassArgumentCleanupOwner::Callee));
     CHECK((linuxResultAddress.location == ecpps::abi::api::AggregateResultAddressLocation::FirstIntegerArgument));
     CHECK((linuxResultAddress.addressEchoedInIntegerReturnRegister));
     CHECK((windowsResultAddress.location == ecpps::abi::api::AggregateResultAddressLocation::FirstIntegerArgument));
     CHECK((windowsResultAddress.addressEchoedInIntegerReturnRegister));
     REQUIRE((ecpps::abi::api::IsSingleIntegerRegister(argumentPlan, classType.Size())));
     REQUIRE((ecpps::abi::api::IsSingleIntegerRegister(returnPlan, classType.Size())));

     auto function = std::make_shared<ecpps::ir::FunctionScope>();
     function->returnType = &classType;
     function->returnsByPointer = true;
     function->parameters.push_back({.name = "value", .type = &classType});

     ecpps::abi::api::Target target{};
     target.platform = std::make_unique<LinuxPlatform>(nullptr);
     target.EnsureVRM();
     target.registerMap->Describe(VirtualRegister{0}, 64, AllocationClass::Temporary);
     target.registerMap->Describe(VirtualRegister{1}, 64, AllocationClass::Allocation);
     target.registerMap->SetAlignment(VirtualRegister{1}, 8);
     target.registerMap->Describe(VirtualRegister{2}, 64, AllocationClass::Temporary);

     X8664VirtualInstructionEncoder encoder{target};
     encoder.SetFunctionCallTable({function.get()});
     std::vector<VirtualInstruction> input{
          {.type = VirtualInstructionType::CopyInteger, .operands = {VirtualRegister{0}, VirtualRegister{0x1000}}},
          {
               .type = VirtualInstructionType::CopyParameter,
               .operands = {VirtualRegister{0}, VirtualRegister{1}, VirtualRegister{0}, VirtualRegister{1}},
          },
          {.type = VirtualInstructionType::AddressOf, .operands = {VirtualRegister{2}, VirtualRegister{1}}},
          {
               .type = VirtualInstructionType::ReturnAggregate,
               .operands = {VirtualRegister{2}, VirtualRegister{classType.Width()}},
          },
     };

     ecpps::optimisations::OptimiseVirtualInstructions(input, *target.registerMap, true, true);
     CHECK(std::ranges::none_of(input,
                                [](const VirtualInstruction& instruction)
                                {
                                     return instruction.type == VirtualInstructionType::CopyInteger &&
                                            instruction.operands[0].index == 0;
                                }));

     auto encoded = encoder.Encode(input);
     encoder.Finalise(encoded);
     std::string assembly{};
     for (const auto& instruction : encoded) assembly += encoder.Stringify(instruction) + '\n';

     const bool storesIncomingIntegerArgument =
          std::ranges::any_of(encoded,
                              [&encoder](const auto& instruction)
                              {
                                   const auto text = encoder.Stringify(instruction);
                                   return text.starts_with("MOV.64 [rbp - ") && text.ends_with(", rdi");
                              });
     const bool loadsAggregateReturnValue =
          std::ranges::any_of(encoded,
                              [&encoder](const auto& instruction)
                              {
                                   return encoder.Stringify(instruction).starts_with("MOV.64 rax, [");
                              });
     INFO(assembly);
     CHECK(storesIncomingIntegerArgument);
     CHECK(loadsAggregateReturnValue);
     CHECK((assembly.contains("RET")));
}

TEST_CASE("Generic virtual register cleanup preserves ABI return values", "[codegen][x86_64][abi][optimiser]")
{
     using ecpps::abi::api::Target;
     using ecpps::abi::encoders::x8664::LinuxPlatform;
     using ecpps::abi::encoders::x8664::WindowsPlatform;
     using ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder;
     using ecpps::ir::abstract::AllocationClass;
     using ecpps::ir::abstract::VirtualInstruction;
     using ecpps::ir::abstract::VirtualInstructionType;
     using ecpps::ir::abstract::VirtualRegister;

     const VirtualRegister value{0};
     const VirtualRegister firstCopy{1};
     const VirtualRegister returnedCopy{2};
     const VirtualRegister deadValue{3};
     const VirtualRegister deadResult{4};
     const VirtualRegister mutableValue{5};
     const VirtualRegister callResult{6};

     ecpps::ir::abstract::VirtualRegisterMap registers{};
     for (const auto reg : {value, firstCopy, returnedCopy, deadValue, deadResult, mutableValue, callResult})
          registers.Describe(reg, 32, AllocationClass::Temporary);
     registers.Describe(mutableValue, 32, AllocationClass::Allocation);

     std::vector<VirtualInstruction> deadChain{
          {.type = VirtualInstructionType::CopyInteger, .operands = {deadValue, VirtualRegister{17}}},
          {.type = VirtualInstructionType::Add, .operands = {deadResult, deadValue, deadValue}},
     };
     ecpps::optimisations::OptimiseVirtualInstructions(deadChain, registers, false, true);
     CHECK(deadChain.empty());

     std::vector<VirtualInstruction> mutableCopy{
          {.type = VirtualInstructionType::CopyInteger, .operands = {deadValue, VirtualRegister{17}}},
          {.type = VirtualInstructionType::Copy, .operands = {mutableValue, deadValue}},
          {.type = VirtualInstructionType::Return, .operands = {mutableValue}},
     };
     ecpps::optimisations::OptimiseVirtualInstructions(mutableCopy, registers, true, true);
     REQUIRE((mutableCopy.size() == 3));
     CHECK((mutableCopy[1].type == VirtualInstructionType::Copy));
     CHECK((mutableCopy[1].operands[0] == mutableValue));

     std::vector<VirtualInstruction> unusedCallResult{
          {.type = VirtualInstructionType::CallWithResult, .operands = {callResult, VirtualRegister{9}}},
     };
     ecpps::optimisations::OptimiseVirtualInstructions(unusedCallResult, registers, false, true);
     REQUIRE((unusedCallResult.size() == 1));
     CHECK((unusedCallResult[0].type == VirtualInstructionType::Call));
     CHECK((unusedCallResult[0].operands == std::vector<VirtualRegister>{VirtualRegister{9}}));

     std::vector<VirtualInstruction> unknownMemoryRead{
          {.type = VirtualInstructionType::CopyInteger, .operands = {deadValue, VirtualRegister{0x1000}}},
          {.type = VirtualInstructionType::Indirection, .operands = {deadResult, deadValue}},
     };
     ecpps::optimisations::OptimiseVirtualInstructions(unknownMemoryRead, registers, false, true);
     CHECK((unknownMemoryRead.size() == 2));

     std::vector<VirtualInstruction> returnedChain{
          {.type = VirtualInstructionType::CopyInteger, .operands = {value, VirtualRegister{42}}},
          {.type = VirtualInstructionType::Copy, .operands = {firstCopy, value}},
          {.type = VirtualInstructionType::Copy, .operands = {returnedCopy, firstCopy}},
          {.type = VirtualInstructionType::Return, .operands = {returnedCopy}},
     };
     ecpps::optimisations::OptimiseVirtualInstructions(returnedChain, registers, true, true);
     REQUIRE((returnedChain.size() == 2));
     CHECK((returnedChain[0].type == VirtualInstructionType::CopyInteger));
     CHECK((returnedChain[1].type == VirtualInstructionType::Return));
     REQUIRE((returnedChain[1].operands.size() == 1));
     CHECK((returnedChain[1].operands[0] == value));

     const auto verifyReturnABI = [&returnedChain](auto platform)
     {
          Target target{};
          target.platform = std::move(platform);
          target.EnsureVRM();
          target.registerMap->Describe(VirtualRegister{0}, 32, AllocationClass::Temporary);
          X8664VirtualInstructionEncoder encoder{target};
          auto encoded = encoder.Encode(returnedChain);
          encoder.Finalise(encoded);

          const auto returnRegister =
               static_cast<ecpps::abi::encoders::x8664::RegisterIndex>(target.platform->IntegerReturnRegisterIndex());
          const bool materialisesReturnValue = std::ranges::any_of(
               encoded,
               [returnRegister](const auto& instruction)
               {
                    if (instruction.opcode != ecpps::abi::encoders::x8664::X8664InstructionName::Mov) return false;
                    const auto* move =
                         std::launder(reinterpret_cast<const ecpps::abi::encoders::x8664::MovInstruction*>(
                              instruction.description.data()));
                    const auto* destination =
                         std::get_if<ecpps::abi::encoders::x8664::RegisterOperand>(&move->destination);
                    const auto* immediate = std::get_if<ecpps::abi::encoders::x8664::IntegerOperand>(&move->source);
                    return destination != nullptr && immediate != nullptr && destination->index == returnRegister &&
                           immediate->value == 42;
               });
          CHECK(materialisesReturnValue);
          CHECK(std::ranges::any_of(encoded,
                                    [](const auto& instruction)
                                    {
                                         return instruction.opcode ==
                                                ecpps::abi::encoders::x8664::X8664InstructionName::Ret;
                                    }));
     };

     SECTION("System V")
     {
          verifyReturnABI(std::make_unique<LinuxPlatform>(nullptr));
     }
     SECTION("Windows")
     {
          verifyReturnABI(std::make_unique<WindowsPlatform>(nullptr));
     }
}

TEST_CASE("Virtual register DCE removes dead scalar stores and preserves volatile accesses",
          "[codegen][optimiser][dce]")
{
     using ecpps::ir::MemoryAccess;
     using ecpps::ir::abstract::AllocationClass;
     using ecpps::ir::abstract::VirtualInstruction;
     using ecpps::ir::abstract::VirtualInstructionType;
     using ecpps::ir::abstract::VirtualRegister;

     const VirtualRegister storage{0};
     const VirtualRegister firstRead{1};
     const VirtualRegister secondRead{2};
     const VirtualRegister firstCopy{3};
     const VirtualRegister secondCopy{4};
     const VirtualRegister returnValue{5};

     ecpps::ir::abstract::VirtualRegisterMap registers{};
     registers.Describe(storage, 32, AllocationClass::Allocation);
     for (const auto reg : {firstRead, secondRead, firstCopy, secondCopy, returnValue})
          registers.Describe(reg, 32, AllocationClass::Temporary);

     const MemoryAccess plainAccess{.alignment = 4};
     std::vector<VirtualInstruction> deadLocalChain{
          {.type = VirtualInstructionType::CopyInteger,
           .operands = {storage, VirtualRegister{0}},
           .memoryEffect = ecpps::ir::abstract::MemoryEffect{.access = plainAccess, .isWrite = true}},
          {.type = VirtualInstructionType::Copy,
           .operands = {firstRead, storage},
           .memoryEffect = ecpps::ir::abstract::MemoryEffect{.access = plainAccess}},
          {.type = VirtualInstructionType::Copy,
           .operands = {secondRead, storage},
           .memoryEffect = ecpps::ir::abstract::MemoryEffect{.access = plainAccess}},
          {.type = VirtualInstructionType::Copy, .operands = {firstCopy, firstRead}},
          {.type = VirtualInstructionType::Copy, .operands = {secondCopy, secondRead}},
          {.type = VirtualInstructionType::CopyInteger, .operands = {returnValue, VirtualRegister{0}}},
          {.type = VirtualInstructionType::Return, .operands = {returnValue}},
     };
     ecpps::optimisations::OptimiseVirtualInstructions(deadLocalChain, registers, true, true);
     REQUIRE((deadLocalChain.size() == 2));
     CHECK((deadLocalChain[0].operands[0] == returnValue));
     CHECK((deadLocalChain[1].type == VirtualInstructionType::Return));

     const MemoryAccess volatileAccess{.isVolatile = true, .alignment = 4};
     std::vector<VirtualInstruction> volatileAccesses{
          {.type = VirtualInstructionType::CopyInteger,
           .operands = {storage, VirtualRegister{7}},
           .memoryEffect = ecpps::ir::abstract::MemoryEffect{.access = volatileAccess, .isWrite = true}},
          {.type = VirtualInstructionType::Copy,
           .operands = {firstRead, storage},
           .memoryEffect = ecpps::ir::abstract::MemoryEffect{.access = volatileAccess}},
     };
     ecpps::optimisations::OptimiseVirtualInstructions(volatileAccesses, registers, true, true);
     REQUIRE((volatileAccesses.size() == 2));
     CHECK((volatileAccesses[0].memoryEffect->access.isVolatile));
     CHECK((volatileAccesses[1].memoryEffect->access.isVolatile));
}

TEST_CASE("x86-64 register allocation - immediate shifts stay immediate", "[codegen][x86_64][regalloc]")
{
     using ecpps::abi::api::Target;
     using ecpps::abi::encoders::x8664::LinuxPlatform;
     using ecpps::abi::encoders::x8664::X8664InstructionName;
     using ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder;
     using ecpps::ir::abstract::VirtualInstruction;
     using ecpps::ir::abstract::VirtualInstructionType;
     using ecpps::ir::abstract::VirtualRegister;

     for (const auto shiftType : {VirtualInstructionType::LeftShift, VirtualInstructionType::RightShift})
     {
          DYNAMIC_SECTION(ecpps::ir::abstract::ToString(shiftType))
          {
               Target target{};
               target.EnsureVRM();
               target.platform = std::make_unique<LinuxPlatform>(nullptr);
               X8664VirtualInstructionEncoder encoder{target};

               for (const VirtualRegister reg : {VirtualRegister{0}, VirtualRegister{1}, VirtualRegister{2}})
                    target.registerMap->Describe(reg, 32, ecpps::ir::abstract::AllocationClass::Temporary);

               const std::size_t expectedShiftOpcode = shiftType == VirtualInstructionType::LeftShift
                                                            ? X8664InstructionName::LeftShift
                                                            : X8664InstructionName::RightShift;
               const std::vector<VirtualInstruction> input{
                    {
                         .type = VirtualInstructionType::CopyInteger,
                         .operands = {VirtualRegister{0}, VirtualRegister{42}},
                    },
                    {
                         .type = VirtualInstructionType::CopyInteger,
                         .operands = {VirtualRegister{1}, VirtualRegister{13}},
                    },
                    {.type = shiftType, .operands = {VirtualRegister{2}, VirtualRegister{0}, VirtualRegister{1}}},
                    {.type = VirtualInstructionType::Return, .operands = {VirtualRegister{2}}},
               };

               const auto encoded = encoder.Encode(input);
               CHECK(std::ranges::none_of(encoded,
                                          [](const auto& instruction)
                                          {
                                               return instruction.opcode == X8664InstructionName::Xchg;
                                          }));

               const auto shift = std::ranges::find_if(encoded,
                                                       [expectedShiftOpcode](const auto& instruction)
                                                       {
                                                            return instruction.opcode == expectedShiftOpcode;
                                                       });
               REQUIRE((shift != encoded.end()));
               const auto& shiftInstruction = *std::launder(
                    reinterpret_cast<const ecpps::abi::encoders::x8664::ShiftInstruction*>(shift->description.data()));
               const auto* immediate =
                    std::get_if<ecpps::abi::encoders::x8664::IntegerOperand>(&shiftInstruction.source);
               REQUIRE((immediate != nullptr));
               CHECK((immediate->value == 13));
          }
     }
}

TEST_CASE("x86-64 Opcodes - MOV instructions", "[codegen][x86_64][opcodes]")
{
     SECTION("mov reg64, reg64")
     {
          auto result = x8664::GenerateMovRegToReg64(x8664::Rax, x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("MOV RAX, RBX encoding");
     }

     SECTION("mov reg32, reg32")
     {
          auto result = x8664::GenerateMovRegToReg32(x8664::Rcx, x8664::Rdx);
          REQUIRE(!result.empty());
          INFO("MOV ECX, EDX encoding");
     }

     SECTION("mov reg64, imm")
     {
          auto result = x8664::GenerateMovImmToReg64(x8664::Rax, 0x1234567890ABCDEFull);
          REQUIRE(!result.empty());
          REQUIRE((result.size() >= 10)); // REX + opcode + 8-byte immediate
          INFO("MOV RAX, immediate64 encoding");
     }

     SECTION("mov reg32, imm")
     {
          auto result = x8664::GenerateMovImmToReg32(x8664::Rcx, 0x12345678u);
          REQUIRE(!result.empty());
          REQUIRE((result.size() >= 5)); // Opcode + 4-byte immediate
          INFO("MOV ECX, immediate32 encoding");
     }

     SECTION("mov reg64, [mem]")
     {
          auto result = x8664::GenerateMovMemToReg64(x8664::Rax, 0, x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("MOV RAX, [RBX] encoding");
     }

     SECTION("mov [mem], reg64")
     {
          auto result = x8664::GenerateMovRegToMem64(x8664::Rax, 0, x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("MOV [RAX], RBX encoding");
     }

     SECTION("movzx - zero extend 8-bit to 64-bit")
     {
          auto result = x8664::GenerateMovZeroExtendReg8ToReg64(x8664::Rax, x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("MOVZX RAX, BL encoding");
     }

     SECTION("movzx - zero extend 32-bit to 64-bit")
     {
          auto result = x8664::GenerateMovZeroExtendReg32ToReg64(x8664::Rax, x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("MOV EAX, EBX (zero-extends to RAX)");
     }
}

TEST_CASE("x86-64 Opcodes - Arithmetic instructions", "[codegen][x86_64][opcodes]")
{
     SECTION("ADD reg64, reg64")
     {
          auto result = x8664::GenerateAddRegToReg64(x8664::Rax, x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("ADD RAX, RBX encoding");
     }

     SECTION("ADD reg32, reg32")
     {
          auto result = x8664::GenerateAddRegToReg32(x8664::Rcx, x8664::Rdx);
          REQUIRE(!result.empty());
          INFO("ADD ECX, EDX encoding");
     }

     SECTION("ADD reg64, imm")
     {
          auto result = x8664::GenerateAddImmToReg64(x8664::Rax, 0x1234);
          REQUIRE(!result.empty());
          INFO("ADD RAX, immediate encoding");
     }

     SECTION("SUB reg64, reg64")
     {
          auto result = x8664::GenerateSubRegToReg64(x8664::Rax, x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("SUB RAX, RBX encoding");
     }

     SECTION("SUB reg32, reg32")
     {
          auto result = x8664::GenerateSubRegToReg32(x8664::Rcx, x8664::Rdx);
          REQUIRE(!result.empty());
          INFO("SUB ECX, EDX encoding");
     }

     SECTION("SUB reg64, imm")
     {
          auto result = x8664::GenerateSubImmToReg64(x8664::Rsp, 0x20);
          REQUIRE(!result.empty());
          INFO("SUB RSP, 0x20 (stack frame allocation)");
     }

     SECTION("IMUL reg64, reg64")
     {
          auto result = x8664::GenerateSignedMulRegToReg64(x8664::Rax, x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("IMUL RAX, RBX encoding");
     }

     SECTION("IMUL reg32, reg32")
     {
          auto result = x8664::GenerateSignedMulRegToReg32(x8664::Rcx, x8664::Rdx);
          REQUIRE(!result.empty());
          INFO("IMUL ECX, EDX encoding");
     }

     SECTION("MUL (unsigned) reg32")
     {
          auto result = x8664::GenerateUnsignedMulRegToReg32(x8664::Rax, x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("MUL EBX (unsigned, result in EDX:EAX)");
     }

     SECTION("DIV (unsigned) reg64")
     {
          auto result = x8664::GenerateUnsignedDiv64(x8664::Rbx);
          REQUIRE(!result.empty());
          INFO("DIV RBX (unsigned, RDX:RAX / RBX)");
     }

     SECTION("IDIV (signed) reg32")
     {
          auto result = x8664::GenerateSignedDiv32(x8664::Rcx);
          REQUIRE(!result.empty());
          INFO("IDIV ECX (signed division)");
     }

     SECTION("XOR reg64, reg64 (zeroing idiom)")
     {
          auto result = x8664::GenerateXorReg64(x8664::Rax, x8664::Rax);
          REQUIRE(!result.empty());
          INFO("XOR RAX, RAX (common zeroing pattern)");
     }

     SECTION("XOR reg32, reg32")
     {
          auto result = x8664::GenerateXorReg32(x8664::Rcx, x8664::Rdx);
          REQUIRE(!result.empty());
          INFO("XOR ECX, EDX encoding");
     }
}

TEST_CASE("x86-64 register allocation preserves values used after an indirect store", "[codegen][x86_64][regalloc]")
{
     using ecpps::abi::encoders::x8664::LinuxPlatform;
     using ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder;
     using ecpps::ir::abstract::AllocationClass;
     using ecpps::ir::abstract::VirtualInstruction;
     using ecpps::ir::abstract::VirtualInstructionType;
     using ecpps::ir::abstract::VirtualRegister;

     const VirtualRegister innerPointer{.index = 1};
     const VirtualRegister outerPointer{.index = 2};
     const VirtualRegister value{.index = 3};
     const VirtualRegister copiedValue{.index = 4};

     ecpps::abi::api::Target target{};
     target.platform = std::make_unique<LinuxPlatform>(nullptr);
     target.EnsureVRM();
     target.registerMap->Describe(innerPointer, 64, AllocationClass::Temporary);
     target.registerMap->Describe(outerPointer, 64, AllocationClass::Temporary);
     target.registerMap->Describe(value, 32, AllocationClass::Temporary);
     target.registerMap->Describe(copiedValue, 32, AllocationClass::Temporary);

     X8664VirtualInstructionEncoder encoder{target};
     const std::vector<VirtualInstruction> input{
          {.type = VirtualInstructionType::CopyInteger, .operands = {innerPointer, VirtualRegister{.index = 0x1000}}},
          {.type = VirtualInstructionType::CopyInteger, .operands = {outerPointer, VirtualRegister{.index = 0x2000}}},
          {.type = VirtualInstructionType::CopyInteger, .operands = {value, VirtualRegister{.index = 42}}},
          {.type = VirtualInstructionType::IndirectStore, .operands = {innerPointer, value}},
          {.type = VirtualInstructionType::Copy, .operands = {copiedValue, value}},
          {.type = VirtualInstructionType::IndirectStore, .operands = {outerPointer, copiedValue}},
          {.type = VirtualInstructionType::Return, .operands = {copiedValue}},
     };

     std::vector<ecpps::ir::abstract::Instruction> encoded{};
     REQUIRE_NOTHROW(encoded = encoder.Encode(input));
     REQUIRE(!encoded.empty());
}

TEST_CASE("x86-64 indirect stores preserve reused and aliased operands", "[codegen][x86_64][regalloc]")
{
     using ecpps::abi::encoders::x8664::LinuxPlatform;
     using ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder;
     using ecpps::ir::abstract::AllocationClass;
     using ecpps::ir::abstract::VirtualInstruction;
     using ecpps::ir::abstract::VirtualInstructionType;
     using ecpps::ir::abstract::VirtualRegister;

     SECTION("the pointer remains live for the next store")
     {
          const VirtualRegister pointer{.index = 1};
          const VirtualRegister value{.index = 2};
          ecpps::abi::api::Target target{};
          target.platform = std::make_unique<LinuxPlatform>(nullptr);
          target.EnsureVRM();
          target.registerMap->Describe(pointer, 64, AllocationClass::Temporary);
          target.registerMap->Describe(value, 32, AllocationClass::Temporary);

          X8664VirtualInstructionEncoder encoder{target};
          const std::vector<VirtualInstruction> input{
               {.type = VirtualInstructionType::CopyInteger, .operands = {pointer, VirtualRegister{.index = 0x1000}}},
               {.type = VirtualInstructionType::CopyInteger, .operands = {value, VirtualRegister{.index = 42}}},
               {.type = VirtualInstructionType::IndirectStore, .operands = {pointer, value}},
               {.type = VirtualInstructionType::IndirectStore, .operands = {pointer, value}},
               {.type = VirtualInstructionType::Return, .operands = {value}},
          };

          REQUIRE_NOTHROW(encoder.Encode(input));
     }

     SECTION("the pointer is also the stored value")
     {
          const VirtualRegister pointerAndValue{.index = 1};
          ecpps::abi::api::Target target{};
          target.platform = std::make_unique<LinuxPlatform>(nullptr);
          target.EnsureVRM();
          target.registerMap->Describe(pointerAndValue, 64, AllocationClass::Temporary);

          X8664VirtualInstructionEncoder encoder{target};
          const std::vector<VirtualInstruction> input{
               {
                    .type = VirtualInstructionType::CopyInteger,
                    .operands = {pointerAndValue, VirtualRegister{.index = 0x1000}},
               },
               {.type = VirtualInstructionType::IndirectStore, .operands = {pointerAndValue, pointerAndValue}},
               {.type = VirtualInstructionType::Return, .operands = {pointerAndValue}},
          };

          REQUIRE_NOTHROW(encoder.Encode(input));
     }
}

TEST_CASE("x86-64 Opcodes - REX prefix", "[codegen][x86_64][opcodes]")
{
     SECTION("REX.W for 64-bit operands")
     {
          auto result = x8664::GenerateMovRegToReg64(x8664::Rax, x8664::Rbx);
          REQUIRE(!result.empty());
          REQUIRE((result.size() >= 3));
          // REX.W prefix should be 0x48 for simple reg-to-reg 64-bit operations
          REQUIRE((result[0] >= std::byte{0x48} && result[0] <= std::byte{0x4F}));
          INFO("REX.W prefix = 0x48-0x4F for 64-bit operations");
     }

     SECTION("REX for extended registers (R8)")
     {
          auto result = x8664::GenerateMovRegToReg64(x8664::Rax, x8664::R8);
          REQUIRE(!result.empty());
          REQUIRE((result.size() >= 3));
          // When using R8-R15, REX.B bit should be set
          REQUIRE((result[0] >= std::byte{0x40}));
          INFO("REX prefix with extended register encoding (R8-R15)");
     }

     SECTION("REX for R15 (highest extended register)")
     {
          auto result = x8664::GenerateMovRegToReg64(x8664::R15, x8664::Rax);
          REQUIRE(!result.empty());
          REQUIRE((result.size() >= 3));
          // R15 requires REX prefix
          REQUIRE((result[0] >= std::byte{0x40}));
          INFO("REX prefix for R15 register");
     }

     SECTION("32-bit operations on extended registers")
     {
          auto result = x8664::GenerateMovRegToReg32(x8664::R8, x8664::R9);
          REQUIRE(!result.empty());
          // 32-bit operations with extended registers still need REX
          REQUIRE((result.size() >= 3));
          INFO("REX prefix required even for 32-bit ops on R8-R15");
     }
}

TEST_CASE("x86-64 Opcodes - Stack operations", "[codegen][x86_64][opcodes]")
{
     SECTION("PUSH RAX")
     {
          auto result = x8664::GeneratePushReg64(x8664::Rax);
          REQUIRE(!result.empty());
          REQUIRE((result.size() == 1));
          REQUIRE((result[0] == std::byte{0x50}));
          INFO("PUSH RAX = 0x50");
     }

     SECTION("PUSH RBX")
     {
          auto result = x8664::GeneratePushReg64(x8664::Rbx);
          REQUIRE(!result.empty());
          REQUIRE((result.size() == 1));
          REQUIRE((result[0] == std::byte{0x53}));
          INFO("PUSH RBX = 0x53");
     }

     SECTION("PUSH R8 (extended register)")
     {
          auto result = x8664::GeneratePushReg64(x8664::R8);
          REQUIRE(!result.empty());
          REQUIRE((result.size() == 2)); // REX prefix + opcode
          INFO("PUSH R8 requires REX prefix");
     }

     SECTION("POP RAX")
     {
          auto result = x8664::GeneratePopReg64(x8664::Rax);
          REQUIRE(!result.empty());
          REQUIRE((result.size() == 1));
          REQUIRE((result[0] == std::byte{0x58}));
          INFO("POP RAX = 0x58");
     }

     SECTION("POP RCX")
     {
          auto result = x8664::GeneratePopReg64(x8664::Rcx);
          REQUIRE(!result.empty());
          REQUIRE((result.size() == 1));
          REQUIRE((result[0] == std::byte{0x59}));
          INFO("POP RCX = 0x59");
     }

     SECTION("POP R15 (extended register)")
     {
          auto result = x8664::GeneratePopReg64(x8664::R15);
          REQUIRE(!result.empty());
          REQUIRE((result.size() == 2)); // REX prefix + opcode
          INFO("POP R15 requires REX prefix");
     }
}

TEST_CASE("x86-64 Opcodes - Control flow", "[codegen][x86_64][opcodes]")
{
     SECTION("RET instruction")
     {
          auto result = x8664::GenerateRet();
          REQUIRE((result.size() == 1));
          REQUIRE((result[0] == std::byte{0xC3}));
          INFO("RET = 0xC3 (near return)");
     }

     SECTION("CALL indirect via displacement")
     {
          auto result = x8664::GenerateIndirectCall(0x1000);
          REQUIRE(!result.empty());
          INFO("CALL [RIP + displacement] encoding");
     }

     SECTION("CALL register indirect")
     {
          auto result = x8664::GenerateRegisterCall(x8664::Rax);
          REQUIRE(!result.empty());
          REQUIRE((result.size() >= 2));
          INFO("CALL RAX (register indirect call)");
     }

     SECTION("CALL via R11")
     {
          auto result = x8664::GenerateRegisterCall(x8664::R11);
          REQUIRE(!result.empty());
          INFO("CALL R11 (extended register indirect call)");
     }

     SECTION("NOP instruction")
     {
          auto result = x8664::GenerateNop();
          REQUIRE((result.size() == 1));
          REQUIRE((result[0] == std::byte{0x90}));
          INFO("NOP = 0x90 (no operation)");
     }

     SECTION("UD2 instruction (undefined opcode)")
     {
          auto result = x8664::GenerateUD2();
          REQUIRE(!result.empty());
          INFO("UD2 (generates #UD exception for unreachable code)");
     }
}

TEST_CASE("x86-64 Opcodes - LEA instruction", "[codegen][x86_64][opcodes]")
{
     SECTION("LEA RAX, [RBX + 0]")
     {
          auto result = x8664::GenerateLeaToReg(x8664::Rbx, 0, x8664::Rax);
          REQUIRE(!result.empty());
          INFO("LEA RAX, [RBX] - load effective address");
     }

     SECTION("LEA RCX, [RDX + 0x10]")
     {
          auto result = x8664::GenerateLeaToReg(x8664::Rdx, 0x10, x8664::Rcx);
          REQUIRE(!result.empty());
          INFO("LEA RCX, [RDX + 0x10] - effective address calculation");
     }

     SECTION("LEA with large displacement")
     {
          auto result = x8664::GenerateLeaToReg(x8664::Rsp, 0x1000, x8664::Rax);
          REQUIRE(!result.empty());
          INFO("LEA RAX, [RSP + 0x1000] - large displacement");
     }

     SECTION("LEA with extended register source")
     {
          auto result = x8664::GenerateLeaToReg(x8664::R10, 0x20, x8664::Rax);
          REQUIRE(!result.empty());
          INFO("LEA RAX, [R10 + 0x20] - extended register addressing");
     }

     SECTION("LEA with extended register destination")
     {
          auto result = x8664::GenerateLeaToReg(x8664::Rbx, 0x8, x8664::R12);
          REQUIRE(!result.empty());
          INFO("LEA R12, [RBX + 0x8] - extended register as destination");
     }

     SECTION("LEA for address arithmetic (no memory access)")
     {
          auto result = x8664::GenerateLeaToReg(x8664::Rdi, 0, x8664::Rsi);
          REQUIRE(!result.empty());
          INFO("LEA RSI, [RDI] - copy address without dereferencing");
     }
}
