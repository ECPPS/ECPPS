#define CATCH_CONFIG_MAIN
#include <CodeGeneration/Emitters/x86_64/Opcodes.h>
#include <TestHelpers.h>
#include <catch_amalgamated.hpp>
#include <cstring>
#include <vector>

using namespace TestHelpers;

// Helper function for byte-by-byte comparison
bool AreEqual(const std::vector<std::byte>& a, const std::vector<std::byte>& b) noexcept
{
     if (a.size() != b.size()) return false;
     return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

// Note: operator""_b is provided by TestHelpers.h

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
