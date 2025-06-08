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

} // namespace ecpps::codegen::x86_64