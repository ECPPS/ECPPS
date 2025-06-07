#include "Opcodes.h"
#include <cstddef>
#include <cstdint>
#include <vector>

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUD2(void) { return {std::byte{0x0f}, std::byte{0x0b}}; }

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg64(std::size_t reg, const std::uint64_t imm)
{
     std::vector<std::byte> binary{};

     if (reg < R8) { binary.push_back(static_cast<std::byte>(0x48)); }
     else
     {
          reg -= 8;
          binary.push_back(static_cast<std::byte>(0x49));
     }
     if (imm <= 0x7fffffff) // encode more efficiently
     {
          binary.push_back(static_cast<std::byte>(0xC7));
          binary.push_back(static_cast<std::byte>(0xC0 + reg));

          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0xB8 + reg));
          for (int i = 0; i < 8; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg32(std::size_t reg, const std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     if (reg >= R8)
     {
          binary.push_back(static_cast<std::byte>(0x41));
          reg -= 8;
     }

     binary.push_back(static_cast<std::byte>(0xB8 + reg));

     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg16(std::size_t reg, const std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));

     if (reg >= R8)
     {
          binary.push_back(static_cast<std::byte>(0x41));
          reg -= 8;
     }

     binary.push_back(static_cast<std::byte>(0xB8 + reg));

     for (int i = 0; i < 2; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg8(std::size_t reg, const std::uint8_t imm)
{
     std::vector<std::byte> binary{};

     if (reg >= R8)
     {
          binary.push_back(static_cast<std::byte>(0x41));
          reg -= 8;
     }
     binary.push_back(static_cast<std::byte>(0xB0 + reg));
     binary.push_back(static_cast<std::byte>(imm));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem64(const std::size_t reg, const std::size_t offset,
                                                                     const std::uint32_t imm)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem32(const std::size_t reg, const std::size_t offset,
                                                                     const std::uint32_t imm)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem16(const std::size_t reg, const std::size_t offset,
                                                                     const std::uint16_t imm)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem8(const std::size_t reg, const std::size_t offset,
                                                                    const std::uint8_t imm)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg64(const std::size_t destination,
                                                                     const std::size_t source)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg32(const std::size_t destination,
                                                                     const std::size_t source)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg16(const std::size_t destination,
                                                                     const std::size_t source)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg8(const std::size_t destination,
                                                                    const std::size_t source)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem64(std::size_t destination, std::size_t destinationOffset, std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem32(std::size_t destination, std::size_t destinationOffset, std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem16(std::size_t destination, std::size_t destinationOffset, std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem8(std::size_t destination, std::size_t destinationOffset, std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}
