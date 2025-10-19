#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ecpps::codegen::x86_64
{
     constexpr std::size_t Rax = 0;
     constexpr std::size_t Rcx = 1;
     constexpr std::size_t Rdx = 2;
     constexpr std::size_t Rbx = 3;
     constexpr std::size_t Rsp = 4;
     constexpr std::size_t Rbp = 5;
     constexpr std::size_t Rsi = 6;
     constexpr std::size_t Rdi = 7;

     constexpr std::size_t R8 = 8;
     constexpr std::size_t R9 = 9;
     constexpr std::size_t R10 = 10;
     constexpr std::size_t R11 = 11;
     constexpr std::size_t R12 = 12;
     constexpr std::size_t R13 = 13;
     constexpr std::size_t R14 = 14;
     constexpr std::size_t R15 = 15;

     constexpr std::vector<std::byte> GenerateRet(void) { return {std::byte{0xC3}}; }
     constexpr std::vector<std::byte> GenerateNop(void) { return {std::byte{0x90}}; }
     std::vector<std::byte> GenerateUD2(void);

     // mov
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToReg64(std::size_t reg, std::uint64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToReg32(std::size_t reg, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToReg16(std::size_t reg, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToReg8(std::size_t reg, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateMovImmToMem64(std::size_t reg, std::size_t offset, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToMem32(std::size_t reg, std::size_t offset, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToMem16(std::size_t reg, std::size_t offset, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToMem8(std::size_t reg, std::size_t offset, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateMovRegToReg64(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateMovRegToReg32(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateMovRegToReg16(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateMovRegToReg8(std::size_t destination, std::size_t source);

     [[nodiscard]] std::vector<std::byte> GenerateMovRegToMem64(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovRegToMem32(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovRegToMem16(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovRegToMem8(std::size_t destination, std::size_t destinationOffset,
                                                               std::size_t sourceRegister);

     [[nodiscard]] std::vector<std::byte> GenerateMovMemToReg64(std::size_t destinationRegister,
                                                                std::size_t sourceOffset, std::size_t sourceRegister);

     [[nodiscard]] std::vector<std::byte> GenerateMovRipToReg64(std::size_t destinationRegister,
                                                                std::size_t sourceOffset);
     [[nodiscard]] std::vector<std::byte> GenerateMovMemToReg32(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovMemToReg16(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovMemToReg8(std::size_t destination, std::size_t destinationOffset,
                                                               std::size_t sourceRegister);

     // add
     [[nodiscard]] std::vector<std::byte> GenerateAddImmToReg64(std::size_t reg, std::uint64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateAddImmToReg32(std::size_t reg, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateAddImmToReg16(std::size_t reg, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateAddImmToReg8(std::size_t reg, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateAddImmToMem64(std::size_t reg, std::size_t offset, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateAddImmToMem32(std::size_t reg, std::size_t offset, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateAddImmToMem16(std::size_t reg, std::size_t offset, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateAddImmToMem8(std::size_t reg, std::size_t offset, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateAddRegToReg64(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateAddRegToReg32(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateAddRegToReg16(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateAddRegToReg8(std::size_t destination, std::size_t source);

     [[nodiscard]] std::vector<std::byte> GenerateAddRegToMem64(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateAddRegToMem32(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateAddRegToMem16(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateAddRegToMem8(std::size_t destination, std::size_t destinationOffset,
                                                               std::size_t sourceRegister);

     // sub
     [[nodiscard]] std::vector<std::byte> GenerateSubImmToReg64(std::size_t reg, std::uint64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSubImmToReg32(std::size_t reg, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSubImmToReg16(std::size_t reg, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSubImmToReg8(std::size_t reg, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateSubImmToMem64(std::size_t reg, std::size_t offset, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSubImmToMem32(std::size_t reg, std::size_t offset, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSubImmToMem16(std::size_t reg, std::size_t offset, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSubImmToMem8(std::size_t reg, std::size_t offset, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateSubRegToReg64(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateSubRegToReg32(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateSubRegToReg16(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateSubRegToReg8(std::size_t destination, std::size_t source);

     [[nodiscard]] std::vector<std::byte> GenerateSubRegToMem64(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSubRegToMem32(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSubRegToMem16(std::size_t destination, std::size_t destinationOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSubRegToMem8(std::size_t destination, std::size_t destinationOffset,
                                                               std::size_t sourceRegister);

     // mul

     [[nodiscard]] std::vector<std::byte> GenerateUnsignedMulRegToReg64(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedMulRegToReg32(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedMulRegToReg16(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedMulRegToReg8(std::size_t destination, std::size_t source);

     [[nodiscard]] std::vector<std::byte> GenerateUnsignedMulRegToMem64(std::size_t destination,
                                                                        std::size_t destinationOffset,
                                                                        std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedMulRegToMem32(std::size_t destination,
                                                                        std::size_t destinationOffset,
                                                                        std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedMulRegToMem16(std::size_t destination,
                                                                        std::size_t destinationOffset,
                                                                        std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedMulRegToMem8(std::size_t destination,
                                                                       std::size_t destinationOffset,
                                                                       std::size_t sourceRegister);

     // imul
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulImmToReg64(std::size_t reg, std::uint64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulImmToReg32(std::size_t reg, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulImmToReg16(std::size_t reg, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulImmToReg8(std::size_t reg, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateSignedMulImmToMem64(std::size_t reg, std::size_t offset,
                                                                      std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulImmToMem32(std::size_t reg, std::size_t offset,
                                                                      std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulImmToMem16(std::size_t reg, std::size_t offset,
                                                                      std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulImmToMem8(std::size_t reg, std::size_t offset,
                                                                     std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateSignedMulRegToReg64(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulRegToReg32(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulRegToReg16(std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulRegToReg8(std::size_t destination, std::size_t source);

     [[nodiscard]] std::vector<std::byte> GenerateSignedMulRegToMem64(std::size_t destination,
                                                                      std::size_t destinationOffset,
                                                                      std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulRegToMem32(std::size_t destination,
                                                                      std::size_t destinationOffset,
                                                                      std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulRegToMem16(std::size_t destination,
                                                                      std::size_t destinationOffset,
                                                                      std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulRegToMem8(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister);

     // div
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedDiv64(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedDiv32(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedDiv16(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedDiv8(std::size_t reg);

     [[nodiscard]] std::vector<std::byte> GenerateUnsignedDivMem64(std::size_t baseReg, std::int32_t displacement);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedDivMem32(std::size_t baseReg, std::int32_t displacement);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedDivMem16(std::size_t baseReg, std::int32_t displacement);
     [[nodiscard]] std::vector<std::byte> GenerateUnsignedDivMem8(std::size_t baseReg, std::int32_t displacement);

     // idiv
     [[nodiscard]] std::vector<std::byte> GenerateSignedDiv64(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateSignedDiv32(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateSignedDiv16(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateSignedDiv8(std::size_t reg);

     [[nodiscard]] std::vector<std::byte> GenerateSignedDivMem64(std::size_t baseReg, std::int32_t displacement);
     [[nodiscard]] std::vector<std::byte> GenerateSignedDivMem32(std::size_t baseReg, std::int32_t displacement);
     [[nodiscard]] std::vector<std::byte> GenerateSignedDivMem16(std::size_t baseReg, std::int32_t displacement);
     [[nodiscard]] std::vector<std::byte> GenerateSignedDivMem8(std::size_t baseReg, std::int32_t displacement);

     // xor
     [[nodiscard]] std::vector<std::byte> GenerateXorReg8(std::size_t destReg, std::size_t srcReg);
     [[nodiscard]] std::vector<std::byte> GenerateXorReg16(std::size_t destReg, std::size_t srcReg);
     [[nodiscard]] std::vector<std::byte> GenerateXorReg32(std::size_t destReg, std::size_t srcReg);
     [[nodiscard]] std::vector<std::byte> GenerateXorReg64(std::size_t destReg, std::size_t srcReg);

     // call
     [[nodiscard]] std::vector<std::byte> GenerateIndirectCall(std::int32_t displacement);
     [[nodiscard]] std::vector<std::byte> GenerateIndirectCall2(std::int32_t displacement);
     [[nodiscard]] std::vector<std::byte> GenerateRegisterCall(std::size_t displacement);

     // push/pop
     [[nodiscard]] std::vector<std::byte> GeneratePushReg64(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GeneratePopReg64(std::size_t reg);

} // namespace ecpps::codegen::x86_64