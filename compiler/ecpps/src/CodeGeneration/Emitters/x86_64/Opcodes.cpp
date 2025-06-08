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
     std::vector<std::byte> binary{};
     const bool rexW = true;
     const bool rexR = (source & 0b1000) != 0;
     const bool rexB = (destination & 0b1000) != 0;

     std::byte rex =
         static_cast<std::byte>(0x40 | (rexW ? 0x08 : 0x00) | (rexR ? 0x04 : 0x00) | (0x00) | // X = 0 (not used here)
                                (rexB ? 0x01 : 0x00));
     binary.push_back(rex);
     binary.push_back(static_cast<std::byte>(0x89));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg32(const std::size_t destination,
                                                                     const std::size_t source)
{
     std::vector<std::byte> binary{};
     const bool rexR = (source & 0b1000) != 0;
     const bool rexB = (destination & 0b1000) != 0;

     if (rexR || rexB)
     {
          std::byte rex = static_cast<std::byte>(0x40 | (rexR ? 0x04 : 0x00) | (0x00) | // X = 0
                                                 (rexB ? 0x01 : 0x00));
          binary.push_back(rex);
     }

     binary.push_back(static_cast<std::byte>(0x89));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg16(const std::size_t destination,
                                                                     const std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66)); // Operand size override

     const bool rexR = (source & 0b1000) != 0;
     const bool rexB = (destination & 0b1000) != 0;

     if (rexR || rexB)
     {
          std::byte rex = static_cast<std::byte>(0x40 | (rexR ? 0x04 : 0x00) | (0x00) | (rexB ? 0x01 : 0x00));
          binary.push_back(rex);
     }

     binary.push_back(static_cast<std::byte>(0x89));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg8(const std::size_t destination,
                                                                    const std::size_t source)
{
     std::vector<std::byte> binary{};
     const bool rexR = (source & 0b1000) != 0;
     const bool rexB = (destination & 0b1000) != 0;
     const bool needsRex = rexR || rexB || ((source & 7) >= 4) || ((destination & 7) >= 4); // AH..BH excluded in REX

     if (needsRex)
     {
          std::byte rex = static_cast<std::byte>(0x40 | (0x00) | // W = 0
                                                 (rexR ? 0x04 : 0x00) | (0x00) | (rexB ? 0x01 : 0x00));
          binary.push_back(rex);
     }

     binary.push_back(static_cast<std::byte>(0x88));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToReg64(std::size_t reg, std::uint64_t imm)
{
     std::vector<std::byte> binary{};

     if (reg == 0)
     {
          binary.push_back(static_cast<std::byte>(0x48));

          if (imm <= 0x7f) // encode more efficiently
          {
               binary.push_back(static_cast<std::byte>(0x83));
               binary.push_back(static_cast<std::byte>(0xC0));

               binary.push_back(static_cast<std::byte>(imm & 0xFF));
          }
          else
          {
               binary.push_back(static_cast<std::byte>(0x05));

               for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
          }

          return binary;
     }

     if (reg < R8) { binary.push_back(static_cast<std::byte>(0x48)); }
     else
     {
          reg -= 8;
          binary.push_back(static_cast<std::byte>(0x49));
     }
     if (imm <= 0x7f) // encode more efficiently
     {
          binary.push_back(static_cast<std::byte>(0x83));
          binary.push_back(static_cast<std::byte>(0xC0 + reg));

          binary.push_back(static_cast<std::byte>(imm & 0xFF));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x81));
          binary.push_back(static_cast<std::byte>(0xC0 + reg));

          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     if (reg == 0)
     {
          if (imm <= 0x7f) // encode more efficiently
          {
               binary.push_back(static_cast<std::byte>(0x83));
               binary.push_back(static_cast<std::byte>(0xC0));

               binary.push_back(static_cast<std::byte>(imm & 0xFF));
          }
          else
          {
               binary.push_back(static_cast<std::byte>(0x05));

               for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
          }

          return binary;
     }

     if (reg >= R8)
     {
          reg -= 8;
          binary.push_back(static_cast<std::byte>(0x41));
     }
     if (imm <= 0x7f) // encode more efficiently
     {
          binary.push_back(static_cast<std::byte>(0x83));
          binary.push_back(static_cast<std::byte>(0xC0 + reg));

          binary.push_back(static_cast<std::byte>(imm & 0xFF));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x81));
          binary.push_back(static_cast<std::byte>(0xC0 + reg));

          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));

     if (reg == 0)
     {
          if (imm <= 0x7f) // encode more efficiently
          {
               binary.push_back(static_cast<std::byte>(0x83));
               binary.push_back(static_cast<std::byte>(0xC0));

               binary.push_back(static_cast<std::byte>(imm & 0xFF));
          }
          else
          {
               binary.push_back(static_cast<std::byte>(0x05));

               for (int i = 0; i < 2; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
          }

          return binary;
     }

     if (reg >= R8)
     {
          reg -= 8;
          binary.push_back(static_cast<std::byte>(0x41));
     }
     if (imm <= 0x7f) // encode more efficiently
     {
          binary.push_back(static_cast<std::byte>(0x83));
          binary.push_back(static_cast<std::byte>(0xC0 + reg));

          binary.push_back(static_cast<std::byte>(imm & 0xFF));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x81));
          binary.push_back(static_cast<std::byte>(0xC0 + reg));

          for (int i = 0; i < 2; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};

     if (reg == 0)
     {
          binary.push_back(static_cast<std::byte>(0x04));
          binary.push_back(static_cast<std::byte>(imm));

          return binary;
     }

     if (reg >= R8)
     {
          reg -= 8;
          binary.push_back(static_cast<std::byte>(0x41));
          binary.push_back(static_cast<std::byte>(0x83));
     }
     else
          binary.push_back(static_cast<std::byte>(0x80));

     binary.push_back(static_cast<std::byte>(0xC0 + reg));

     binary.push_back(static_cast<std::byte>(imm));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem64(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem32(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem16(std::size_t reg, std::size_t offset,
                                                                     std::uint16_t imm)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem8(std::size_t reg, std::size_t offset,
                                                                    std::uint8_t imm)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg64(std::size_t destination, std::size_t source)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg32(std::size_t destination, std::size_t source)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg16(std::size_t destination, std::size_t source)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg8(std::size_t destination, std::size_t source)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     return std::vector<std::byte>();
}
