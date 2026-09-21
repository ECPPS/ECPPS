#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"

namespace ecpps::codegen::x86_64 // NOLINT(readability-identifier-naming)
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

     constexpr std::size_t Rip = 16;

     constexpr std::vector<std::byte> GenerateRet(void)
     {
          return {std::byte{0xC3}};
     }
     constexpr std::vector<std::byte> GenerateNop(void)
     {
          return {std::byte{0x90}};
     }
     std::vector<std::byte> GenerateUD2(void);
     std::vector<std::byte> GenerateCwd(void);
     std::vector<std::byte> GenerateCqo(void);
     std::vector<std::byte> GenerateCdq(void);
     std::vector<std::byte> GenerateCbw(void);
     std::vector<std::byte> GenerateCwde(void);
     std::vector<std::byte> GenerateCdqe(void);

     // mov
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToReg64(std::size_t reg, std::uint64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToReg32(std::size_t reg, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToReg16(std::size_t reg, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovImmToReg8(std::size_t reg, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateMovImmToMem64(std::size_t reg, std::size_t offset, std::uint64_t imm);
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
     [[nodiscard]] std::vector<std::byte> GenerateMovMemToReg32(std::size_t destinationRegister,
                                                                std::size_t sourceOffset, std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovMemToReg16(std::size_t destinationRegister,
                                                                std::size_t sourceOffset, std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovMemToReg8(std::size_t destinationRegister,
                                                               std::size_t sourceOffset, std::size_t sourceRegister);

     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendMem8ToReg64(std::size_t destinationRegister,
                                                                           std::size_t sourceOffset,
                                                                           std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendMem8ToReg32(std::size_t destinationRegister,
                                                                           std::size_t sourceOffset,
                                                                           std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendMem8ToReg16(std::size_t destinationRegister,
                                                                           std::size_t sourceOffset,
                                                                           std::size_t sourceRegister);

     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendMem16ToReg64(std::size_t destinationRegister,
                                                                            std::size_t sourceOffset,
                                                                            std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendMem16ToReg32(std::size_t destinationRegister,
                                                                            std::size_t sourceOffset,
                                                                            std::size_t sourceRegister);

     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendMem32ToReg64(std::size_t destinationRegister,
                                                                            std::size_t sourceOffset,
                                                                            std::size_t sourceRegister);

     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendReg8ToReg64(std::size_t destinationRegister,
                                                                           std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendReg8ToReg32(std::size_t destinationRegister,
                                                                           std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendReg8ToReg16(std::size_t destinationRegister,
                                                                           std::size_t sourceRegister);

     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendReg16ToReg64(std::size_t destinationRegister,
                                                                            std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendReg16ToReg32(std::size_t destinationRegister,
                                                                            std::size_t sourceRegister);

     [[nodiscard]] std::vector<std::byte> GenerateMovZeroExtendReg32ToReg64(std::size_t destinationRegister,
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

     [[nodiscard]] std::vector<std::byte> GenerateAddMemToReg64(std::size_t destination, std::size_t sourceOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateAddMemToReg32(std::size_t destination, std::size_t sourceOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateAddMemToReg16(std::size_t destination, std::size_t sourceOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateAddMemToReg8(std::size_t destination, std::size_t sourceOffset,
                                                               std::size_t sourceRegister);

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

     [[nodiscard]] std::vector<std::byte> GenerateSubMemToReg64(std::size_t destination, std::size_t sourceOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSubMemToReg32(std::size_t destination, std::size_t sourceOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSubMemToReg16(std::size_t destination, std::size_t sourceOffset,
                                                                std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSubMemToReg8(std::size_t destination, std::size_t sourceOffset,
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

     [[nodiscard]] std::vector<std::byte> GenerateSignedMulMemToReg64(std::size_t destination, std::size_t sourceOffset,
                                                                      std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulMemToReg32(std::size_t destination, std::size_t sourceOffset,
                                                                      std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulMemToReg16(std::size_t destination, std::size_t sourceOffset,
                                                                      std::size_t sourceRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSignedMulMemToReg8(std::size_t destination, std::size_t sourceOffset,
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
     [[nodiscard]] std::vector<std::byte> GenerateRegisterCall(std::size_t reg);

     // push/pop
     [[nodiscard]] std::vector<std::byte> GeneratePushReg64(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GeneratePopReg64(std::size_t reg);

     // lea
     [[nodiscard]] std::vector<std::byte> GenerateLeaToReg(std::size_t sourceRegister, std::size_t sourceDisplacement,
                                                           std::size_t destinationRegister);

     // sar
     [[nodiscard]] std::vector<std::byte> GenerateSignedSarImmToReg64(std::size_t reg, std::uint64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedSarImmToReg32(std::size_t reg, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedSarImmToReg16(std::size_t reg, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedSarImmToReg8(std::size_t reg, std::uint8_t imm);

     // shr
     [[nodiscard]] std::vector<std::byte> GenerateSignedShrImmToReg64(std::size_t reg, std::uint64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedShrImmToReg32(std::size_t reg, std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedShrImmToReg16(std::size_t reg, std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedShrImmToReg8(std::size_t reg, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateSignedShrImmToMem64(std::size_t reg, std::size_t offset,
                                                                      std::uint64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedShrImmToMem32(std::size_t reg, std::size_t offset,
                                                                      std::uint32_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedShrImmToMem16(std::size_t reg, std::size_t offset,
                                                                      std::uint16_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSignedShrImmToMem8(std::size_t reg, std::size_t offset,
                                                                     std::uint8_t imm);

     // neg

     [[nodiscard]] std::vector<std::byte> GenerateNegReg8(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateNegReg16(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateNegReg32(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateNegReg64(std::size_t reg);

     // sal
     [[nodiscard]] std::vector<std::byte> GenerateSalReg64(std::size_t reg, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSalReg32(std::size_t reg, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSalReg16(std::size_t reg, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSalReg8(std::size_t reg, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateSalMem64(std::size_t reg, std::size_t offset, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSalMem32(std::size_t reg, std::size_t offset, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSalMem16(std::size_t reg, std::size_t offset, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSalMem8(std::size_t reg, std::size_t offset, std::uint8_t imm);

     // sar
     [[nodiscard]] std::vector<std::byte> GenerateSarReg64(std::size_t reg, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSarReg32(std::size_t reg, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSarReg16(std::size_t reg, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSarReg8(std::size_t reg, std::uint8_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateSarMem64(std::size_t reg, std::size_t offset, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSarMem32(std::size_t reg, std::size_t offset, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSarMem16(std::size_t reg, std::size_t offset, std::uint8_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateSarMem8(std::size_t reg, std::size_t offset, std::uint8_t imm);

     constexpr std::size_t NoRegister = static_cast<std::size_t>(-1);

     using abi::encoders::x8664::Width;

     struct Memory
     {
          std::size_t base = NoRegister;
          std::size_t index = NoRegister;
          std::uint8_t scale = 1; // 1, 2, 4, 8
          std::int32_t displacement = 0;
     };

     [[nodiscard]] constexpr Memory MemBase(std::size_t base, std::int32_t displacement = 0)
     {
          return Memory{.base = base, .index = NoRegister, .scale = 1, .displacement = displacement};
     }
     [[nodiscard]] constexpr Memory MemIndexed(std::size_t base, std::size_t index, std::uint8_t scale,
                                               std::int32_t displacement = 0)
     {
          return Memory{.base = base, .index = index, .scale = scale, .displacement = displacement};
     }
     [[nodiscard]] constexpr Memory MemRip(std::int32_t displacement)
     {
          return Memory{.base = Rip, .index = NoRegister, .scale = 1, .displacement = displacement};
     }
     [[nodiscard]] constexpr Memory MemAbsolute(std::int32_t address)
     {
          return Memory{.base = NoRegister, .index = NoRegister, .scale = 1, .displacement = address};
     }

     enum struct AluOp : std::uint8_t
     {
          Add = 0,
          Or = 1,
          Adc = 2,
          Sbb = 3,
          And = 4,
          Sub = 5,
          Xor = 6,
          Cmp = 7
     };
     enum struct ShiftOp : std::uint8_t
     {
          Rol = 0,
          Ror = 1,
          Rcl = 2,
          Rcr = 3,
          Shl = 4,
          Shr = 5,
          Sar = 7
     };
     enum struct UnaryOp : std::uint8_t
     {
          Inc = 0,
          Dec = 1,
          Not = 2,
          Neg = 3,
          Mul = 4,
          Imul = 5,
          Div = 6,
          Idiv = 7
     };
     enum struct BitOp : std::uint8_t
     {
          Bt = 0,
          Bts = 1,
          Btr = 2,
          Btc = 3
     };
     enum struct ExtendKind : std::uint8_t
     {
          Zero,
          Sign
     };
     enum struct Condition : std::uint8_t
     {
          Overflow = 0,
          NoOverflow = 1,
          Below = 2,
          AboveOrEqual = 3,
          Equal = 4,
          NotEqual = 5,
          BelowOrEqual = 6,
          Above = 7,
          Sign = 8,
          NoSign = 9,
          Parity = 10,
          NoParity = 11,
          Less = 12,
          GreaterOrEqual = 13,
          LessOrEqual = 14,
          Greater = 15
     };
     enum struct StringOp : std::uint8_t
     {
          Movs = 0xA4,
          Cmps = 0xA6,
          Stos = 0xAA,
          Lods = 0xAC,
          Scas = 0xAE
     };
     enum struct RepPrefix : std::uint8_t
     {
          None,
          Rep,
          Repne
     };
     enum struct SystemTableOp : std::uint8_t
     {
          Sgdt = 0,
          Sidt = 1,
          Lgdt = 2,
          Lidt = 3,
          Invlpg = 7
     };

     constexpr std::vector<std::byte> GenerateInt3(void)
     {
          return {std::byte{0xCC}};
     }
     constexpr std::vector<std::byte> GenerateHlt(void)
     {
          return {std::byte{0xF4}};
     }
     constexpr std::vector<std::byte> GenerateLeave(void)
     {
          return {std::byte{0xC9}};
     }
     constexpr std::vector<std::byte> GenerateCli(void)
     {
          return {std::byte{0xFA}};
     }
     constexpr std::vector<std::byte> GenerateSti(void)
     {
          return {std::byte{0xFB}};
     }
     constexpr std::vector<std::byte> GenerateClc(void)
     {
          return {std::byte{0xF8}};
     }
     constexpr std::vector<std::byte> GenerateStc(void)
     {
          return {std::byte{0xF9}};
     }
     constexpr std::vector<std::byte> GenerateCmc(void)
     {
          return {std::byte{0xF5}};
     }
     constexpr std::vector<std::byte> GenerateCld(void)
     {
          return {std::byte{0xFC}};
     }
     constexpr std::vector<std::byte> GenerateStd(void)
     {
          return {std::byte{0xFD}};
     }
     constexpr std::vector<std::byte> GeneratePushfq(void)
     {
          return {std::byte{0x9C}};
     }
     constexpr std::vector<std::byte> GeneratePopfq(void)
     {
          return {std::byte{0x9D}};
     }
     constexpr std::vector<std::byte> GenerateLahf(void)
     {
          return {std::byte{0x9F}};
     }
     constexpr std::vector<std::byte> GenerateSahf(void)
     {
          return {std::byte{0x9E}};
     }
     constexpr std::vector<std::byte> GeneratePause(void)
     {
          return {std::byte{0xF3}, std::byte{0x90}};
     }
     constexpr std::vector<std::byte> GenerateLockPrefix(void)
     {
          return {std::byte{0xF0}};
     }
     constexpr std::vector<std::byte> GenerateSyscall(void)
     {
          return {std::byte{0x0F}, std::byte{0x05}};
     }
     constexpr std::vector<std::byte> GenerateSysretq(void)
     {
          return {std::byte{0x48}, std::byte{0x0F}, std::byte{0x07}};
     }
     constexpr std::vector<std::byte> GenerateSysenter(void)
     {
          return {std::byte{0x0F}, std::byte{0x34}};
     }
     constexpr std::vector<std::byte> GenerateIretq(void)
     {
          return {std::byte{0x48}, std::byte{0xCF}};
     }
     constexpr std::vector<std::byte> GenerateCpuid(void)
     {
          return {std::byte{0x0F}, std::byte{0xA2}};
     }
     constexpr std::vector<std::byte> GenerateRdtsc(void)
     {
          return {std::byte{0x0F}, std::byte{0x31}};
     }
     constexpr std::vector<std::byte> GenerateRdtscp(void)
     {
          return {std::byte{0x0F}, std::byte{0x01}, std::byte{0xF9}};
     }
     constexpr std::vector<std::byte> GenerateRdmsr(void)
     {
          return {std::byte{0x0F}, std::byte{0x32}};
     }
     constexpr std::vector<std::byte> GenerateWrmsr(void)
     {
          return {std::byte{0x0F}, std::byte{0x30}};
     }
     constexpr std::vector<std::byte> GenerateSwapgs(void)
     {
          return {std::byte{0x0F}, std::byte{0x01}, std::byte{0xF8}};
     }
     constexpr std::vector<std::byte> GenerateWbinvd(void)
     {
          return {std::byte{0x0F}, std::byte{0x09}};
     }
     constexpr std::vector<std::byte> GenerateInvd(void)
     {
          return {std::byte{0x0F}, std::byte{0x08}};
     }

     [[nodiscard]] std::vector<std::byte> GenerateNopN(std::size_t count);
     [[nodiscard]] std::vector<std::byte> GenerateRetImm(std::uint16_t popBytes);
     [[nodiscard]] std::vector<std::byte> GenerateInt(std::uint8_t vector);

     [[nodiscard]] std::vector<std::byte> GenerateAluRegReg(AluOp op, Width size, std::size_t destination,
                                                            std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateAluRegMem(AluOp op, Width size, const Memory& destination,
                                                            std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateAluMemReg(AluOp op, Width size, std::size_t destination,
                                                            const Memory& source);
     [[nodiscard]] std::vector<std::byte> GenerateAluImmReg(AluOp op, Width size, std::size_t reg, std::int64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateAluImmMem(AluOp op, Width size, const Memory& destination,
                                                            std::int64_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateTestRegReg(Width size, std::size_t a, std::size_t b);
     [[nodiscard]] std::vector<std::byte> GenerateTestMemReg(Width size, const Memory& a, std::size_t b);
     [[nodiscard]] std::vector<std::byte> GenerateTestImmReg(Width size, std::size_t reg, std::int64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateTestImmMem(Width size, const Memory& mem, std::int64_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateMovLoad(Width size, std::size_t destination, const Memory& source);
     [[nodiscard]] std::vector<std::byte> GenerateMovStore(Width size, const Memory& destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateMovStoreImm(Width size, const Memory& destination, std::int64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateMovExtendRegToReg(ExtendKind kind, Width destinationSize,
                                                                    Width sourceSize, std::size_t destination,
                                                                    std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateMovExtendMemToReg(ExtendKind kind, Width destinationSize,
                                                                    Width sourceSize, std::size_t destination,
                                                                    const Memory& source);
     [[nodiscard]] std::vector<std::byte> GenerateXchgRegReg(Width size, std::size_t a, std::size_t b);
     [[nodiscard]] std::vector<std::byte> GenerateXchgMemReg(Width size, const Memory& mem, std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateCmpxchgMemReg(Width size, const Memory& destination,
                                                                std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateXaddMemReg(Width size, const Memory& destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateCmovRegReg(Condition cc, Width size, std::size_t destination,
                                                             std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateCmovMemReg(Condition cc, Width size, std::size_t destination,
                                                             const Memory& source);
     [[nodiscard]] std::vector<std::byte> GenerateSetccReg(Condition cc, std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateSetccMem(Condition cc, const Memory& mem);

     [[nodiscard]] std::vector<std::byte> GenerateLea(Width size, std::size_t destination, const Memory& source);

     [[nodiscard]] std::vector<std::byte> GenerateUnaryReg(UnaryOp op, Width size, std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateUnaryMem(UnaryOp op, Width size, const Memory& mem);

     [[nodiscard]] std::vector<std::byte> GenerateImulRegReg(Width size, std::size_t destination, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateImulRegMem(Width size, std::size_t destination, const Memory& source);
     [[nodiscard]] std::vector<std::byte> GenerateImulRegRegImm(Width size, std::size_t destination, std::size_t source,
                                                                std::int64_t imm);
     [[nodiscard]] std::vector<std::byte> GenerateImulRegMemImm(Width size, std::size_t destination,
                                                                const Memory& source, std::int64_t imm);

     [[nodiscard]] std::vector<std::byte> GenerateShiftRegImm(ShiftOp op, Width size, std::size_t reg,
                                                              std::uint8_t count);
     [[nodiscard]] std::vector<std::byte> GenerateShiftRegCl(ShiftOp op, Width size, std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateShiftMemImm(ShiftOp op, Width size, const Memory& mem,
                                                              std::uint8_t count);
     [[nodiscard]] std::vector<std::byte> GenerateShiftMemCl(ShiftOp op, Width size, const Memory& mem);
     [[nodiscard]] std::vector<std::byte> GenerateDoubleShiftRegRegImm(bool right, Width size, std::size_t destination,
                                                                       std::size_t source, std::uint8_t count);
     [[nodiscard]] std::vector<std::byte> GenerateDoubleShiftRegRegCl(bool right, Width size, std::size_t destination,
                                                                      std::size_t source);

     [[nodiscard]] std::vector<std::byte> GenerateBitTestRegReg(BitOp op, Width size, std::size_t base,
                                                                std::size_t bit);
     [[nodiscard]] std::vector<std::byte> GenerateBitTestRegImm(BitOp op, Width size, std::size_t base,
                                                                std::uint8_t bit);
     [[nodiscard]] std::vector<std::byte> GenerateBitTestMemReg(BitOp op, Width size, const Memory& base,
                                                                std::size_t bit);
     [[nodiscard]] std::vector<std::byte> GenerateBitTestMemImm(BitOp op, Width size, const Memory& base,
                                                                std::uint8_t bit);
     [[nodiscard]] std::vector<std::byte> GenerateBitScanRegReg(bool reverse, Width size, std::size_t destination,
                                                                std::size_t source); // bsf / bsr
     [[nodiscard]] std::vector<std::byte> GenerateBitScanMemReg(bool reverse, Width size, std::size_t destination,
                                                                const Memory& source);
     [[nodiscard]] std::vector<std::byte> GenerateBswap(Width size, std::size_t reg);

     [[nodiscard]] std::vector<std::byte> GenerateJmpRel8(std::int8_t rel);
     [[nodiscard]] std::vector<std::byte> GenerateJmpRel32(std::int32_t rel);
     [[nodiscard]] std::vector<std::byte> GenerateJccRel8(Condition cc, std::int8_t rel);
     [[nodiscard]] std::vector<std::byte> GenerateJccRel32(Condition cc, std::int32_t rel);
     [[nodiscard]] std::vector<std::byte> GenerateCallRel32(std::int32_t rel);
     [[nodiscard]] std::vector<std::byte> GenerateJmpReg(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateJmpMem(const Memory& target);
     [[nodiscard]] std::vector<std::byte> GenerateCallReg(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateCallMem(const Memory& target);
     [[nodiscard]] std::vector<std::byte> GenerateJrcxz(std::int8_t rel);
     [[nodiscard]] std::vector<std::byte> GenerateLoop(std::int8_t rel);

     [[nodiscard]] std::vector<std::byte> GeneratePushImm(std::int32_t imm);
     [[nodiscard]] std::vector<std::byte> GeneratePushMem(const Memory& mem);
     [[nodiscard]] std::vector<std::byte> GeneratePopMem(const Memory& mem);

     [[nodiscard]] std::vector<std::byte> GenerateStringOp(StringOp op, Width size, RepPrefix rep = RepPrefix::None);

     [[nodiscard]] std::vector<std::byte> GenerateMovToControlReg(std::size_t controlRegister, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateMovFromControlReg(std::size_t destination,
                                                                    std::size_t controlRegister);
     [[nodiscard]] std::vector<std::byte> GenerateMovToDebugReg(std::size_t debugRegister, std::size_t source);
     [[nodiscard]] std::vector<std::byte> GenerateMovFromDebugReg(std::size_t destination, std::size_t debugRegister);
     [[nodiscard]] std::vector<std::byte> GenerateSystemTableMem(SystemTableOp op,
                                                                 const Memory& mem); // sgdt/sidt/lgdt/lidt/invlpg
     [[nodiscard]] std::vector<std::byte> GenerateLtr(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateLldt(std::size_t reg);
     [[nodiscard]] std::vector<std::byte> GenerateInImm8(Width size, std::uint8_t port);
     [[nodiscard]] std::vector<std::byte> GenerateInDx(Width size);
     [[nodiscard]] std::vector<std::byte> GenerateOutImm8(Width size, std::uint8_t port);
     [[nodiscard]] std::vector<std::byte> GenerateOutDx(Width size);
} // namespace ecpps::codegen::x86_64
