#include "Opcodes.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// NOLINTBEGIN(readability-identifier-length)

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
     std::vector<std::byte> binary{};

     auto rex = std::byte(0x48);                 // REX.W = 1
     if (reg >= R8) rex |= std::byte(0x01 << 2); // REX.B
     binary.push_back(rex);

     binary.push_back(std::byte(0xC7)); // opcode (we still use /0)

     // Mod calculation
     std::uint8_t mod = 0;
     if (offset == 0 && reg != Rbp && reg != R13) mod = 0x00;
     else if (offset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<uint8_t>(reg % 8);
     bool needsSIB = (rm == 4);
     std::uint8_t modrm = (mod << 6) | (0 << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));
     if (needsSIB) binary.push_back(std::byte(0x24));

     // displacement
     if (mod == 0x01) binary.push_back(static_cast<std::byte>(offset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));

     // immediate (truncate to 32-bit; x86-64 MOV [mem], imm64 is encoded via two instructions in practice)
     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem32(const std::size_t reg, const std::size_t offset,
                                                                     const std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     auto rex = std::byte(0x40);                 // no REX.W for 32-bit
     if (reg >= R8) rex |= std::byte(0x01 << 2); // REX.B
     if (rex != std::byte(0x40)) binary.push_back(rex);

     binary.push_back(std::byte(0xC7)); // opcode

     // Determine Mod
     std::uint8_t mod = 0;
     if (offset == 0 && reg != Rbp && reg != R13) mod = 0x00; // no displacement
     else if (offset < 0x80)
          mod = 0x01; // 8-bit displacement
     else
          mod = 0x02; // 32-bit displacement

     auto rm = static_cast<uint8_t>(reg % 8);

     // Special case: RSP or R12 always need a SIB
     bool needsSIB = (rm == 4);
     std::uint8_t modrm = (mod << 6) | (0 << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSIB) binary.push_back(std::byte(0x24)); // SIB: scale=0, index=4(none), base=rsp/r12

     // displacement
     if (mod == 0x01) // 8-bit
          binary.push_back(static_cast<std::byte>(offset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13))) // 32-bit
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));

     // immediate
     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem16(const std::size_t reg, const std::size_t offset,
                                                                     const std::uint16_t imm)
{
     std::vector<std::byte> binary{};

     binary.push_back(std::byte(0x66)); // operand-size override

     auto rex = std::byte(0x40);
     if (reg >= R8) rex |= std::byte(0x01 << 2); // REX.B
     if (rex != std::byte(0x40)) binary.push_back(rex);

     binary.push_back(std::byte(0xC7));

     std::uint8_t mod = 0;
     if (offset == 0 && reg != Rbp && reg != R13) mod = 0x00;
     else if (offset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<uint8_t>(reg % 8);
     bool needsSIB = (rm == 4);
     std::uint8_t modrm = (mod << 6) | (0 << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));
     if (needsSIB) binary.push_back(std::byte(0x24));

     if (mod == 0x01) binary.push_back(static_cast<std::byte>(offset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));

     for (int i = 0; i < 2; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem8(const std::size_t reg, const std::size_t offset,
                                                                    const std::uint8_t imm)
{
     std::vector<std::byte> binary{};

     auto rex = std::byte(0x40);
     if (reg >= R8) rex |= std::byte(0x01 << 2); // REX.B
     if (rex != std::byte(0x40)) binary.push_back(rex);

     binary.push_back(std::byte(0xC6)); // opcode for MOV r/m8, imm8

     std::uint8_t mod = 0;
     if (offset == 0 && reg != Rbp && reg != R13) mod = 0x00;
     else if (offset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<uint8_t>(reg % 8);
     bool needsSIB = (rm == 4);
     std::uint8_t modrm = (mod << 6) | (0 << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));
     if (needsSIB) binary.push_back(std::byte(0x24));

     if (mod == 0x01) binary.push_back(static_cast<std::byte>(offset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));

     binary.push_back(static_cast<std::byte>(imm));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg64(const std::size_t destination,
                                                                     const std::size_t source)
{
     std::vector<std::byte> binary{};
     const bool rexW = true;
     const bool rexR = (source & 0b1000) != 0;
     const bool rexB = (destination & 0b1000) != 0;

     auto rex =
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
          auto rex = static_cast<std::byte>(0x40 | (rexR ? 0x04 : 0x00) | (0x00) | // X = 0
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
          auto rex = static_cast<std::byte>(0x40 | (rexR ? 0x04 : 0x00) | (0x00) | (rexB ? 0x01 : 0x00));
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
          auto rex = static_cast<std::byte>(0x40 | (0x00) | // W = 0
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
     std::vector<std::byte> binary{};

     if (destination == Rsp)
     {
          binary.reserve(destinationOffset == 0 ? 4 : destinationOffset <= 0x7f ? 5 : 7);

          const bool rexR = sourceRegister >= R8;
          if (rexR) sourceRegister -= R8;

          binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(rexR) << 2))); // REX.W | R
          binary.push_back(static_cast<std::byte>(0x89));

          if (destinationOffset == 0)
          {
               binary.push_back(static_cast<std::byte>(0x04 | (sourceRegister << 3)));
               binary.push_back(static_cast<std::byte>(0x24));
          }
          else if (destination <= 0x7f)
          {
               binary.push_back(static_cast<std::byte>(0x44 | (sourceRegister << 3)));
               binary.push_back(static_cast<std::byte>(0x24));
               binary.push_back(static_cast<std::byte>(destinationOffset));
          }
          else
          {
               binary.push_back(static_cast<std::byte>(0x84 | (sourceRegister << 3)));
               binary.push_back(static_cast<std::byte>(0x24));
               for (int i = 0; i < 4; ++i)
                    binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
          }
          return binary;
     }

     const bool rexR = sourceRegister >= 8;
     const bool rexB = destination >= 8;

     binary.push_back(
         static_cast<std::byte>(0x48 | (static_cast<int>(rexR) << 2) | static_cast<int>(rexB))); // REX.W | R | B
     binary.push_back(static_cast<std::byte>(0x89));

     const bool needsSib = (destination & 7) == 4;

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));

          if (needsSib) binary.push_back(static_cast<std::byte>(0x24)); // SIB: no index, base = rsp

          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));

          if (needsSib) binary.push_back(static_cast<std::byte>(0x24)); // SIB: no index, base = rsp

          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     // Determine if REX is needed
     bool rexW = false; // 32-bit, so W = 0
     bool rexR = sourceRegister >= 8;
     bool rexB = destination >= 8;

     bool needsRex = rexW || rexR || rexB;

     // Push REX if needed
     if (needsRex) { binary.push_back(static_cast<std::byte>(0x40 | (rexR << 2) | (rexB << 0))); }

     binary.push_back(static_cast<std::byte>(0x89)); // mov r/m32, r32

     // ModRM byte
     std::uint8_t modrm{};
     std::byte sib{};
     bool useSib = (destination & 7) == 4; // RSP or R12 as base requires SIB

     if (destinationOffset == 0 && (destination & 7) != 5) // no disp
     {
          modrm = static_cast<std::uint8_t>((sourceRegister & 7) << 3 | (destination & 7));
     }
     else if (destinationOffset <= 0xFF) // 8-bit displacement
     {
          modrm = static_cast<std::uint8_t>(0x40 | ((sourceRegister & 7) << 3) | (destination & 7));
     }
     else // 32-bit displacement
     {
          modrm = static_cast<std::uint8_t>(0x80 | ((sourceRegister & 7) << 3) | (destination & 7));
     }

     if (useSib)
     {
          modrm = (modrm & 0xC0) | 0x04 | ((sourceRegister & 7) << 3); // Mod bits + reg + rm=100
          sib = static_cast<std::byte>(0x24 | ((rexB ? 1 << 2 : 0)));  // scale=0, index=4 (none), base=destination&7
          binary.push_back(std::byte{modrm});
          binary.push_back(sib);
     }
     else
     {
          binary.push_back(std::byte{modrm});
     }

     // Append displacement if needed
     if (destinationOffset != 0 || (destination & 7) == 5) // displacement mandatory if base=RBP/R13
     {
          if (destinationOffset <= 0xFF) { binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF)); }
          else
          {
               for (int i = 0; i < 4; ++i)
                    binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
          }
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66)); // operand size override

     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                                  static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x89));

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     if (destination >= 8 || sourceRegister >= 8 || ((sourceRegister & 4) != 0U))
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                                  static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x88));

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg64(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     if (sourceRegister == Rsp) return GenerateMovRspToReg64(destinationRegister, sourceOffset);

     std::vector<std::byte> binary{};

     std::uint8_t rex = 0x48;
     if (destinationRegister >= 8) rex |= 0x04; // R
     if (sourceRegister >= 8) rex |= 0x01;      // B
     binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(static_cast<std::byte>(0x8B));

     if (sourceOffset <= 0x7F)
     {
          std::uint8_t modrm = 0b01000000 | ((destinationRegister & 7) << 3) | (sourceRegister & 7);
          binary.push_back(static_cast<std::byte>(modrm));
          binary.push_back(static_cast<std::byte>(sourceOffset));
     }
     else
     {
          std::uint8_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | (sourceRegister & 7);
          binary.push_back(static_cast<std::byte>(modrm));
          for (int i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg32(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     if (sourceRegister == Rsp) return GenerateMovRspToReg32(destinationRegister, sourceOffset);
     std::vector<std::byte> binary{};

     // REX (no W bit for 32-bit)
     std::uint8_t rex = 0x40;
     if (destinationRegister >= 8) rex |= 0x04; // R
     if (sourceRegister >= 8) rex |= 0x01;      // B
     if (rex != 0x40) binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(static_cast<std::byte>(0x8B)); // MOV r32, [r/m32]

     if (sourceOffset <= 0x7F)
     {
          std::uint8_t modrm = 0b01000000 | ((destinationRegister & 7) << 3) | (sourceRegister & 7);
          binary.push_back(static_cast<std::byte>(modrm));
          binary.push_back(static_cast<std::byte>(sourceOffset));
     }
     else
     {
          std::uint8_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | (sourceRegister & 7);
          binary.push_back(static_cast<std::byte>(modrm));
          for (int i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg16(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     if (sourceRegister == Rsp) return GenerateMovRspToReg16(destinationRegister, sourceOffset);
     std::vector<std::byte> binary{};

     // 16-bit requires 0x66 (operand-size override)
     binary.push_back(static_cast<std::byte>(0x66));

     std::uint8_t rex = 0x40;
     if (destinationRegister >= 8) rex |= 0x04;
     if (sourceRegister >= 8) rex |= 0x01;
     binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(static_cast<std::byte>(0x8B)); // MOV r16, [r/m16]

     if (sourceOffset <= 0x7F)
     {
          std::uint8_t modrm = 0b01000000 | ((destinationRegister & 7) << 3) | (sourceRegister & 7);
          binary.push_back(static_cast<std::byte>(modrm));
          binary.push_back(static_cast<std::byte>(sourceOffset));
     }
     else
     {
          std::uint8_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | (sourceRegister & 7);
          binary.push_back(static_cast<std::byte>(modrm));
          for (int i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg8(std::size_t destinationRegister,
                                                                    std::size_t sourceOffset,
                                                                    std::size_t sourceRegister)
{
     if (sourceRegister == Rsp) return GenerateMovRspToReg8(destinationRegister, sourceOffset);
     std::vector<std::byte> binary{};

     std::uint8_t rex = 0x40; // Must keep REX to access SIL, DIL, etc.
     if (destinationRegister >= 8) rex |= 0x04;
     if (sourceRegister >= 8) rex |= 0x01;
     binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(static_cast<std::byte>(0x8A)); // MOV r8, [r/m8]

     if (sourceOffset <= 0x7F)
     {
          std::uint8_t modrm = 0b01000000 | ((destinationRegister & 7) << 3) | (sourceRegister & 7);
          binary.push_back(static_cast<std::byte>(modrm));
          binary.push_back(static_cast<std::byte>(sourceOffset));
     }
     else
     {
          std::uint8_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | (sourceRegister & 7);
          binary.push_back(static_cast<std::byte>(modrm));
          for (int i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem8ToReg64(std::size_t destinationRegister,
                                                                                std::size_t sourceOffset,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     std::uint8_t rex = 0x48;
     if (destinationRegister >= 8) rex |= 0x04;
     if (sourceRegister >= 8) rex |= 0x01;
     binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xB6));

     const bool needsSib = (sourceRegister & 7) == Rsp;
     const bool needsDisp = (sourceRegister & 7) == Rbp;
     const bool disp8 = sourceOffset <= 0x7F;

     std::uint8_t mod = needsDisp ? 0b01 : disp8 ? 0b01 : 0b10;

     std::uint8_t rm = needsSib ? 0b100 : (sourceRegister & 7);
     std::uint8_t modrm = (mod << 6) | ((destinationRegister & 7) << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSib)
     {
          std::uint8_t sib = 0b00100100 | (sourceRegister & 7);
          binary.push_back(static_cast<std::byte>(sib));
     }

     if (needsDisp || disp8) { binary.push_back(static_cast<std::byte>(sourceOffset)); }
     else
     {
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem8ToReg32(std::size_t destinationRegister,
                                                                                std::size_t sourceOffset,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     std::uint8_t rex = 0;
     if (destinationRegister >= 8) rex |= 0x44; // REX.R
     if (sourceRegister >= 8) rex |= 0x41;      // REX.B
     if (rex != 0U) binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xB6)); // MOVZX r32, r/m8

     const bool needsSib = (sourceRegister & 7) == Rsp;
     const bool needsDisp = (sourceRegister & 7) == Rbp;
     const bool disp8 = sourceOffset <= 0x7F;

     const std::uint8_t mod = needsDisp ? 0b01 : disp8 ? 0b01 : 0b10;

     const std::uint8_t rm = needsSib ? 0b100 : (sourceRegister & 7);

     binary.push_back(static_cast<std::byte>((mod << 6) | ((destinationRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(static_cast<std::byte>(0b00100100 | (sourceRegister & 7)));

     if (needsDisp || disp8) binary.push_back(static_cast<std::byte>(sourceOffset));
     else
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     binary.append_range(GenerateMovZeroExtendReg8ToReg32(destinationRegister, destinationRegister));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem8ToReg16(std::size_t destinationRegister,
                                                                                std::size_t sourceOffset,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     std::uint8_t rex = 0x40;                   // base REX
     if (destinationRegister >= 8) rex |= 0x04; // REX.R
     if (sourceRegister >= 8) rex |= 0x01;      // REX.B
     if (rex != 0x40) binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(std::byte{0x0F});
     binary.push_back(std::byte{0xB6}); // MOVZX r16, r/m8

     const bool needsSib = (sourceRegister & 7) == Rsp;
     const bool needsDisp = (sourceRegister & 7) == Rbp;
     const bool disp8 = sourceOffset <= 0x7F;

     const std::uint8_t mod = needsDisp ? 0b01 : disp8 ? 0b01 : 0b10;
     const std::uint8_t rm = needsSib ? 0b100 : (sourceRegister & 7);

     binary.push_back(static_cast<std::byte>((mod << 6) | ((destinationRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(static_cast<std::byte>(0b00100100 | (sourceRegister & 7)));

     if (needsDisp || disp8) binary.push_back(static_cast<std::byte>(sourceOffset));
     else
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem16ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceOffset,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     std::uint8_t rex = 0x48;                   // REX.W for 64-bit
     if (destinationRegister >= 8) rex |= 0x04; // REX.R
     if (sourceRegister >= 8) rex |= 0x01;      // REX.B
     binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(std::byte{0x0F});
     binary.push_back(std::byte{0xB7}); // MOVZX r64, r/m16

     const bool needsSib = (sourceRegister & 7) == Rsp;
     const bool needsDisp = (sourceRegister & 7) == Rbp;
     const bool disp8 = sourceOffset <= 0x7F;

     const std::uint8_t mod = needsDisp ? 0b01 : disp8 ? 0b01 : 0b10;
     const std::uint8_t rm = needsSib ? 0b100 : (sourceRegister & 7);

     binary.push_back(static_cast<std::byte>((mod << 6) | ((destinationRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(static_cast<std::byte>(0b00100100 | (sourceRegister & 7)));

     if (needsDisp || disp8) binary.push_back(static_cast<std::byte>(sourceOffset));
     else
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem16ToReg32(std::size_t destinationRegister,
                                                                                 std::size_t sourceOffset,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     std::uint8_t rex = 0x40;                   // base
     if (destinationRegister >= 8) rex |= 0x04; // REX.R
     if (sourceRegister >= 8) rex |= 0x01;      // REX.B
     if (rex != 0x40) binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(std::byte{0x0F});
     binary.push_back(std::byte{0xB7}); // MOVZX r32, r/m16

     const bool needsSib = (sourceRegister & 7) == Rsp;
     const bool needsDisp = (sourceRegister & 7) == Rbp;
     const bool disp8 = sourceOffset <= 0x7F;

     const std::uint8_t mod = needsDisp ? 0b01 : disp8 ? 0b01 : 0b10;
     const std::uint8_t rm = needsSib ? 0b100 : (sourceRegister & 7);

     binary.push_back(static_cast<std::byte>((mod << 6) | ((destinationRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(static_cast<std::byte>(0b00100100 | (sourceRegister & 7)));

     if (needsDisp || disp8) binary.push_back(static_cast<std::byte>(sourceOffset));
     else
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem32ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceOffset,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     std::uint8_t rex = 0x48; // REX.W
     if (destinationRegister >= 8) rex |= 0x04;
     if (sourceRegister >= 8) rex |= 0x01;
     binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(std::byte{0x0F});
     binary.push_back(std::byte{0xB7});
     binary.push_back(static_cast<std::byte>(0x8B)); // MOV r32, r/m32

     const bool needsSib = (sourceRegister & 7) == Rsp;
     const bool needsDisp = (sourceRegister & 7) == Rbp;
     const bool disp8 = sourceOffset <= 0x7F;

     const std::uint8_t mod = needsDisp ? 0b01 : disp8 ? 0b01 : 0b10;
     const std::uint8_t rm = needsSib ? 0b100 : (sourceRegister & 7);

     binary.push_back(static_cast<std::byte>((mod << 6) | ((destinationRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(static_cast<std::byte>(0b00100100 | (sourceRegister & 7)));

     if (needsDisp || disp8) binary.push_back(static_cast<std::byte>(sourceOffset));
     else
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg8ToReg64(std::size_t destinationRegister,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(4);
     // MOVZX r64, r/m8
     std::byte rex{0x48}; // REX.W
     binary.push_back(rex);
     binary.push_back(std::byte{0x0f});
     binary.push_back(std::byte{0xb8});
     binary.push_back(std::byte(0xc0 | sourceRegister | (destinationRegister + 8)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg8ToReg32(std::size_t destinationRegister,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);
     // MOVZX r32, r/m8
     binary.push_back(std::byte{0x0f});
     binary.push_back(std::byte{0xb6});
     binary.push_back(std::byte(0xc0 | sourceRegister | (destinationRegister + 8)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg8ToReg16(std::size_t destinationRegister,
                                                                                std::size_t sourceRegister)
{
     // MOVZX r16, r/m8
     std::vector<std::byte> binary{};
     std::uint8_t rex = 0x40; // base
     if (destinationRegister >= 8) rex |= 0x04;
     if (sourceRegister >= 8) rex |= 0x01;
     if (rex != 0x40) binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(std::byte{0x0F});
     binary.push_back(std::byte{0xB6}); // MOVZX r16, r/m8
     binary.push_back(static_cast<std::byte>(0xC0 | ((destinationRegister & 7) << 3) | (sourceRegister & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg16ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceRegister)
{
     // MOVZX r64, r/m16
     std::vector<std::byte> binary{};
     std::uint8_t rex = 0x48; // REX.W
     if (destinationRegister >= 8) rex |= 0x04;
     if (sourceRegister >= 8) rex |= 0x01;
     binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(std::byte{0x0F});
     binary.push_back(std::byte{0xB7}); // MOVZX r64, r/m16
     binary.push_back(static_cast<std::byte>(0xC0 | ((destinationRegister & 7) << 3) | (sourceRegister & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg16ToReg32(std::size_t destinationRegister,
                                                                                 std::size_t sourceRegister)
{
     // MOVZX r32, r/m16
     std::vector<std::byte> binary{};
     std::uint8_t rex = 0x40; // base
     if (destinationRegister >= 8) rex |= 0x04;
     if (sourceRegister >= 8) rex |= 0x01;
     if (rex != 0x40) binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(std::byte{0x0F});
     binary.push_back(std::byte{0xB7}); // MOVZX r32, r/m16
     binary.push_back(static_cast<std::byte>(0xC0 | ((destinationRegister & 7) << 3) | (sourceRegister & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg32ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceRegister)
{
     return GenerateMovRegToReg64(destinationRegister, sourceRegister);
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRspToReg64(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset)
{
     std::vector<std::byte> binary{};

     // REX prefix for 64-bit operand
     std::uint8_t rex = 0x48;
     if (destinationRegister >= 8) rex |= 0x04; // R bit for destination
     binary.push_back(static_cast<std::byte>(rex));

     // Opcode for MOV r64, r/m64
     binary.push_back(static_cast<std::byte>(0x8B));

     // ModRM byte: mod=01 (8-bit displacement) or mod=10 (32-bit), r/m=100 (SIB)
     std::uint8_t mod = 0b10; // 32-bit displacement
     std::uint8_t rm = 0b100; // SIB follows
     std::uint8_t reg = destinationRegister & 7;
     std::uint8_t modrm = (mod << 6) | (reg << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     // SIB byte: scale=00, index=100 (none), base=100 (RSP)
     std::uint8_t sib = (0b00 << 6) | (0b100 << 3) | 0b100;
     binary.push_back(static_cast<std::byte>(sib));

     // 4-byte little-endian displacement
     for (int i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRspToReg32(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset)
{
     std::vector<std::byte> binary{};

     // No REX.W, only REX.R if >7
     std::uint8_t rex = 0x40;
     if (destinationRegister >= 8) rex |= 0x04;
     binary.push_back(static_cast<std::byte>(rex));

     // MOV r32, r/m32
     binary.push_back(static_cast<std::byte>(0x8B));

     std::uint8_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | 0b100;
     binary.push_back(static_cast<std::byte>(modrm));
     binary.push_back(static_cast<std::byte>(0x24));

     for (int i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRspToReg16(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset)
{
     std::vector<std::byte> binary{};

     // Operand-size prefix 0x66
     binary.push_back(static_cast<std::byte>(0x66));

     std::uint8_t rex = 0x40;
     if (destinationRegister >= 8) rex |= 0x04;
     binary.push_back(static_cast<std::byte>(rex));

     // MOV r16, r/m16
     binary.push_back(static_cast<std::byte>(0x8B));

     std::uint8_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | 0b100;
     binary.push_back(static_cast<std::byte>(modrm));
     binary.push_back(static_cast<std::byte>(0x24));

     for (int i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRspToReg8(std::size_t destinationRegister,
                                                                    std::size_t sourceOffset)
{
     std::vector<std::byte> binary{};

     std::uint8_t rex = 0x40;
     if (destinationRegister >= 8) rex |= 0x04;
     binary.push_back(static_cast<std::byte>(rex));

     // MOV r8, r/m8
     binary.push_back(static_cast<std::byte>(0x8A));

     std::uint8_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | 0b100;
     binary.push_back(static_cast<std::byte>(modrm));
     binary.push_back(static_cast<std::byte>(0x24));

     for (int i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
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
     std::vector<std::byte> binary{};

     const bool disp8 = offset <= 0x7F;
     const bool needsSib = ((reg & 7) == 4); // RSP / R12

     std::uint8_t rex = 0x48;   // REX.W
     if (reg >= 8) rex |= 0x01; // REX.B
     binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(static_cast<std::byte>(0x81)); // ADD r/m, imm32

     const std::uint8_t mod = disp8 ? 0b01 : 0b10;
     const std::uint8_t rm = needsSib ? 4 : (reg & 7);

     binary.push_back(static_cast<std::byte>((mod << 6) | rm));

     if (needsSib)
     {
          // scale=0, index=none(100), base=reg&7
          binary.push_back(static_cast<std::byte>((0 << 6) | (4 << 3) | (reg & 7)));
     }

     if (disp8) { binary.push_back(static_cast<std::byte>(offset)); }
     else
     {
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem32(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     const bool disp8 = offset <= 0x7F;
     const bool needsSib = ((reg & 7) == 4);

     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x41)); // REX.B

     binary.push_back(static_cast<std::byte>(0x81));

     const std::uint8_t mod = disp8 ? 0b01 : 0b10;
     const std::uint8_t rm = needsSib ? 4 : (reg & 7);

     binary.push_back(static_cast<std::byte>((mod << 6) | rm));

     if (needsSib) { binary.push_back(static_cast<std::byte>((0 << 6) | (4 << 3) | (reg & 7))); }

     if (disp8) { binary.push_back(static_cast<std::byte>(offset)); }
     else
     {
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem16(std::size_t reg, std::size_t offset,
                                                                     std::uint16_t imm)
{
     std::vector<std::byte> binary{};

     binary.push_back(static_cast<std::byte>(0x66)); // operand-size override

     const bool disp8 = offset <= 0x7F;
     const bool needsSib = ((reg & 7) == 4);

     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x41));

     binary.push_back(static_cast<std::byte>(0x81));

     const std::uint8_t mod = disp8 ? 0b01 : 0b10;
     const std::uint8_t rm = needsSib ? 4 : (reg & 7);

     binary.push_back(static_cast<std::byte>((mod << 6) | rm));

     if (needsSib) { binary.push_back(static_cast<std::byte>((0 << 6) | (4 << 3) | (reg & 7))); }

     if (disp8) { binary.push_back(static_cast<std::byte>(offset)); }
     else
     {
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     binary.push_back(static_cast<std::byte>(imm & 0xFF));
     binary.push_back(static_cast<std::byte>((imm >> 8) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem8(std::size_t reg, std::size_t offset,
                                                                    std::uint8_t imm)
{
     std::vector<std::byte> binary{};

     const bool disp8 = offset <= 0x7F;
     const bool needsSib = ((reg & 7) == 4);

     if (reg >= 8 || (reg & 7) >= 4)
     {
          std::uint8_t rex = 0x40;
          if (reg >= 8) rex |= 0x01;
          binary.push_back(static_cast<std::byte>(rex));
     }

     binary.push_back(static_cast<std::byte>(0x80));

     const std::uint8_t mod = disp8 ? 0b01 : 0b10;
     const std::uint8_t rm = needsSib ? 4 : (reg & 7);

     binary.push_back(static_cast<std::byte>((mod << 6) | rm));

     if (needsSib) { binary.push_back(static_cast<std::byte>((0 << 6) | (4 << 3) | (reg & 7))); }

     if (disp8) { binary.push_back(static_cast<std::byte>(offset)); }
     else
     {
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     binary.push_back(static_cast<std::byte>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     bool rexR = source >= 8;
     bool rexB = destination >= 8;
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(rexR) << 2) | static_cast<int>(rexB)));
     binary.push_back(static_cast<std::byte>(0x01));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     if (destination >= 8 || source >= 8)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(source >= 8) << 2) | static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x01));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));
     if (destination >= 8 || source >= 8)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(source >= 8) << 2) | static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x01));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     bool rex = destination >= 8 || source >= 8 || (destination & 7) >= 4 || (source & 7) >= 4;
     if (rex)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(source >= 8) << 2) | static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x00));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg64(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const bool rexR = destination >= 8;
     const bool rexB = sourceRegister >= 8;
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(rexR) << 2) | static_cast<int>(rexB)));
     binary.push_back(static_cast<std::byte>(0x03)); // ADD r64, r/m64

     const auto mod = (sourceOffset == 0) ? 0b00 : (sourceOffset <= 0x7F) ? 0b01 : 0b10;
     const auto modrm = static_cast<std::byte>((mod << 6) | ((destination & 7) << 3) | (sourceRegister & 7));
     binary.push_back(modrm);

     if (sourceRegister == 4) // rsp/r12 needs SIB
     {
          binary.push_back(std::byte{0x24});
     }

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
     {
          const auto offset32 = static_cast<std::uint32_t>(sourceOffset);
          const auto* p = reinterpret_cast<const std::byte*>(&offset32);
          binary.insert(binary.end(), p, p + 4);
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg32(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(destination >= 8) << 2) |
                                                  static_cast<int>(sourceRegister >= 8)));

     binary.push_back(static_cast<std::byte>(0x03)); // ADD r32, r/m32

     const auto mod = (sourceOffset == 0) ? 0b00 : (sourceOffset <= 0x7F) ? 0b01 : 0b10;
     const auto modrm = static_cast<std::byte>((mod << 6) | ((destination & 7) << 3) | (sourceRegister & 7));
     binary.push_back(modrm);

     if (sourceRegister == 4) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
     {
          const auto offset32 = static_cast<std::uint32_t>(sourceOffset);
          const auto* p = reinterpret_cast<const std::byte*>(&offset32);
          binary.insert(binary.end(), p, p + 4);
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg16(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     binary.push_back(static_cast<std::byte>(0x66));
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(destination >= 8) << 2) |
                                                  static_cast<int>(sourceRegister >= 8)));

     binary.push_back(static_cast<std::byte>(0x03)); // ADD r16, r/m16

     const auto mod = (sourceOffset == 0) ? 0b00 : (sourceOffset <= 0x7F) ? 0b01 : 0b10;
     const auto modrm = static_cast<std::byte>((mod << 6) | ((destination & 7) << 3) | (sourceRegister & 7));
     binary.push_back(modrm);

     if (sourceRegister == 4) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
     {
          const auto offset32 = static_cast<std::uint32_t>(sourceOffset);
          const auto* p = reinterpret_cast<const std::byte*>(&offset32);
          binary.insert(binary.end(), p, p + 4);
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg8(std::size_t destination, std::size_t sourceOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const bool rex = destination >= 8 || sourceRegister >= 8 || (destination & 7) >= 4 || (sourceRegister & 7) >= 4;
     if (rex)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(destination >= 8) << 2) |
                                                  static_cast<int>(sourceRegister >= 8)));

     binary.push_back(static_cast<std::byte>(0x02)); // ADD r8, r/m8

     const auto mod = (sourceOffset == 0) ? 0b00 : (sourceOffset <= 0x7F) ? 0b01 : 0b10;
     const auto modrm = static_cast<std::byte>((mod << 6) | ((destination & 7) << 3) | (sourceRegister & 7));
     binary.push_back(modrm);

     if (sourceRegister == 4) // rsp/r12 needs SIB
          binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
     {
          const auto offset32 = static_cast<std::uint32_t>(sourceOffset);
          const auto* p = reinterpret_cast<const std::byte*>(&offset32);
          binary.insert(binary.end(), p, p + 4);
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                             static_cast<int>(destination >= 8)));
     binary.push_back(static_cast<std::byte>(0x01));

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                                  static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x01));

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                                  static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x01));

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     if (destination >= 8 || sourceRegister >= 8 || (sourceRegister & 7) >= 4)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                                  static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x00));

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg64(std::size_t reg, std::uint64_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexW = true;
     const bool rexB = (reg & 0b1000) != 0;

     auto rex = static_cast<std::byte>(0x40 | (rexW ? 0x08 : 0x00) | (rexB ? 0x01 : 0x00));
     binary.push_back(rex);

     if (imm <= 0x7FFFFFFF)
     {
          binary.push_back(static_cast<std::byte>(0x81));
          binary.push_back(static_cast<std::byte>(0xE8 | (reg & 0x07)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x48));
          binary.push_back(static_cast<std::byte>(0xB8 + (reg & 0x07)));
          for (int i = 0; i < 8; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
          binary.push_back(static_cast<std::byte>(0x2B)); // SUB reg, rax
          binary.push_back(static_cast<std::byte>(0xC0 | ((0 & 7) << 3) | (reg & 7)));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     if (reg >= 8)
     {
          binary.push_back(static_cast<std::byte>(0x41));
          reg -= 8;
     }

     binary.push_back(static_cast<std::byte>(0x81));
     binary.push_back(static_cast<std::byte>(0xE8 | reg));
     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66)); // Operand size override

     if (reg >= 8)
     {
          binary.push_back(static_cast<std::byte>(0x41));
          reg -= 8;
     }

     binary.push_back(static_cast<std::byte>(0x81));
     binary.push_back(static_cast<std::byte>(0xE8 | reg));
     for (int i = 0; i < 2; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexB = (reg & 8) != 0;
     const bool extendedLow = (reg & 7) >= 4; // SPL, BPL etc.

     if (rexB || extendedLow)
     {
          auto rex = static_cast<std::byte>(0x40 | (rexB ? 0x01 : 0x00));
          binary.push_back(rex);
     }

     binary.push_back(static_cast<std::byte>(0x80));
     binary.push_back(static_cast<std::byte>(0xE8 | (reg & 7)));
     binary.push_back(static_cast<std::byte>(imm));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem64(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexW = true;
     const bool rexB = reg >= 8;
     if (rexW || rexB) binary.push_back(static_cast<std::byte>(0x48 | (rexB ? 0x01 : 0x00)));

     binary.push_back(static_cast<std::byte>(0x81)); // /5 for SUB

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 0x07) << 0))); // mod = 01
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 0x07) << 0))); // mod = 10
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem32(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x41));

     binary.push_back(static_cast<std::byte>(0x81)); // /5

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 0x07) << 0)));
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 0x07) << 0)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem16(std::size_t reg, std::size_t offset,
                                                                     std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66)); // 16-bit override

     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x41));

     binary.push_back(static_cast<std::byte>(0x81));

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 0x07) << 0)));
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 0x07) << 0)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     binary.push_back(static_cast<std::byte>(imm & 0xFF));
     binary.push_back(static_cast<std::byte>((imm >> 8) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem8(std::size_t reg, std::size_t offset,
                                                                    std::uint8_t imm)
{
     std::vector<std::byte> binary{};

     if (reg >= 8 || (reg & 0x07) >= 4) // high byte regs (SPL, etc.) require REX
          binary.push_back(static_cast<std::byte>(0x40 | ((reg >= 8) ? 0x01 : 0x00)));

     binary.push_back(static_cast<std::byte>(0x80)); // /5

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 0x07) << 0)));
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 0x07) << 0)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     binary.push_back(static_cast<std::byte>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     const bool rexR = source >= 8;
     const bool rexB = destination >= 8;

     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(rexR) << 2) | static_cast<int>(rexB)));
     binary.push_back(static_cast<std::byte>(0x29));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};

     if (source >= 8 || destination >= 8)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(source >= 8) << 2) | static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));

     if (source >= 8 || destination >= 8)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(source >= 8) << 2) | static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};

     const bool rex = (destination >= 8) || (source >= 8) || (destination & 7) >= 4 || (source & 7) >= 4;
     if (rex)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(source >= 8) << 2) | static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x28));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                             static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29));
     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                                  static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29));
     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                                  static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29));
     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     if (destination >= 8 || sourceRegister >= 8 || (sourceRegister & 7) >= 4)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                                  static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x28));
     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg64(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     auto rex = std::byte(0x48);                        // REX.W = 1
     if (destination >= 8) rex |= std::byte(0x01 << 2); // REX.R
     if (sourceRegister >= 8) rex |= std::byte(0x01);   // REX.B
     binary.push_back(rex);

     binary.push_back(std::byte(0x2B)); // opcode SUB reg, r/m64

     // Mod calculation
     std::uint8_t mod = 0;
     if (sourceOffset == 0 && sourceRegister != Rbp && sourceRegister != R13) mod = 0x00;
     else if (sourceOffset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<std::uint8_t>(sourceRegister % 8);
     bool needsSIB = (rm == 4); // rsp/r12 needs SIB
     std::uint8_t modrm = (mod << 6) | ((destination % 8) << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSIB) binary.push_back(std::byte(0x24)); // SIB byte: scale=0, index=none, base=rsp/r12

     // displacement
     if (mod == 0x01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg32(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     auto rex = std::byte(0x40); // REX prefix without W
     if (destination >= 8) rex |= std::byte(0x01 << 2);
     if (sourceRegister >= 8) rex |= std::byte(0x01);
     if (rex != std::byte(0x40)) binary.push_back(rex);

     binary.push_back(std::byte(0x2B)); // SUB reg, r/m32

     std::uint8_t mod = 0;
     if (sourceOffset == 0 && sourceRegister != Rbp && sourceRegister != R13) mod = 0x00;
     else if (sourceOffset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<std::uint8_t>(sourceRegister % 8);
     bool needsSIB = (rm == 4);
     std::uint8_t modrm = (mod << 6) | ((destination % 8) << 3) | rm;
     binary.push_back(std::byte(modrm));

     if (needsSIB) binary.push_back(std::byte(0x24));

     if (mod == 0x01) binary.push_back(std::byte(sourceOffset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (int i = 0; i < 4; ++i) binary.push_back(std::byte((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg16(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(std::byte(0x66)); // operand size prefix

     auto rex = std::byte(0x40); // REX prefix without W
     if (destination >= 8) rex |= std::byte(0x01 << 2);
     if (sourceRegister >= 8) rex |= std::byte(0x01);
     if (rex != std::byte(0x40)) binary.push_back(rex);

     binary.push_back(std::byte(0x2B)); // SUB reg, r/m16

     std::uint8_t mod = 0;
     if (sourceOffset == 0 && sourceRegister != Rbp && sourceRegister != R13) mod = 0x00;
     else if (sourceOffset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<std::uint8_t>(sourceRegister % 8);
     bool needsSIB = (rm == 4);
     std::uint8_t modrm = (mod << 6) | ((destination % 8) << 3) | rm;
     binary.push_back(std::byte(modrm));

     if (needsSIB) binary.push_back(std::byte(0x24));

     if (mod == 0x01) binary.push_back(std::byte(sourceOffset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (int i = 0; i < 4; ++i) binary.push_back(std::byte((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg8(std::size_t destination, std::size_t sourceOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     auto rex = std::byte(0x40); // REX prefix
     if (destination >= 8) rex |= std::byte(0x01 << 2);
     if (sourceRegister >= 8) rex |= std::byte(0x01);
     if (rex != std::byte(0x40)) binary.push_back(rex);

     binary.push_back(std::byte(0x2A)); // SUB reg, r/m8

     std::uint8_t mod = 0;
     if (sourceOffset == 0 && sourceRegister != Rbp && sourceRegister != R13) mod = 0x00;
     else if (sourceOffset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<std::uint8_t>(sourceRegister % 8);
     bool needsSIB = (rm == 4);
     std::uint8_t modrm = (mod << 6) | ((destination % 8) << 3) | rm;
     binary.push_back(std::byte(modrm));

     if (needsSIB) binary.push_back(std::byte(0x24));

     if (mod == 0x01) binary.push_back(std::byte(sourceOffset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (int i = 0; i < 4; ++i) binary.push_back(std::byte((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg64([[maybe_unused]] std::size_t destination,
                                                                             [[maybe_unused]] std::size_t source)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg32([[maybe_unused]] std::size_t destination,
                                                                             [[maybe_unused]] std::size_t source)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg16([[maybe_unused]] std::size_t destination,
                                                                             [[maybe_unused]] std::size_t source)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg8([[maybe_unused]] std::size_t destination,
                                                                            [[maybe_unused]] std::size_t source)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem64(
    [[maybe_unused]] std::size_t destination, [[maybe_unused]] std::size_t destinationOffset,
    [[maybe_unused]] std::size_t sourceRegister)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem32(
    [[maybe_unused]] std::size_t destination, [[maybe_unused]] std::size_t destinationOffset,
    [[maybe_unused]] std::size_t sourceRegister)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem16(
    [[maybe_unused]] std::size_t destination, [[maybe_unused]] std::size_t destinationOffset,
    [[maybe_unused]] std::size_t sourceRegister)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem8(
    [[maybe_unused]] std::size_t destination, [[maybe_unused]] std::size_t destinationOffset,
    [[maybe_unused]] std::size_t sourceRegister)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg64(std::size_t reg, std::uint64_t imm)
{
     std::vector<std::byte> binary{};
     bool rexR = reg >= 8;
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(rexR) << 2)));
     binary.push_back(static_cast<std::byte>(0x69));
     binary.push_back(static_cast<std::byte>(0xC0 | ((reg & 7) << 3) | (reg & 7)));

     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x41));
     binary.push_back(static_cast<std::byte>(0x69));
     binary.push_back(static_cast<std::byte>(0xC0 | ((reg & 7) << 3) | (reg & 7)));

     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));
     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x41));
     binary.push_back(static_cast<std::byte>(0x69));
     binary.push_back(static_cast<std::byte>(0xC0 | ((reg & 7) << 3) | (reg & 7)));
     binary.push_back(static_cast<std::byte>(imm & 0xFF));
     binary.push_back(static_cast<std::byte>((imm >> 8) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     bool rex = reg >= 8 || (reg & 7) >= 4;
     if (rex) binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(reg >= 8) << 0)));
     binary.push_back(static_cast<std::byte>(0x6B));
     binary.push_back(static_cast<std::byte>(0xC0 | ((reg & 7) << 3) | (reg & 7)));
     binary.push_back(static_cast<std::byte>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem64(std::size_t reg, std::size_t offset,
                                                                           std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x48 | (reg >= 8 ? 0x01 : 0)));
     binary.push_back(static_cast<std::byte>(0x69));

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 7) << 3)));
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 7) << 3)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem32(std::size_t reg, std::size_t offset,
                                                                           std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x41));
     binary.push_back(static_cast<std::byte>(0x69));

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 7) << 3)));
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 7) << 3)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem16(std::size_t reg, std::size_t offset,
                                                                           std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));
     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x41));
     binary.push_back(static_cast<std::byte>(0x69));

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 7) << 3)));
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 7) << 3)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     binary.push_back(static_cast<std::byte>(imm & 0xFF));
     binary.push_back(static_cast<std::byte>((imm >> 8) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem8(std::size_t reg, std::size_t offset,
                                                                          std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     bool rex = reg >= 8 || (reg & 7) >= 4;
     if (rex) binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(reg >= 8) << 0)));
     binary.push_back(static_cast<std::byte>(0x6B));

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 7) << 3)));
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 7) << 3)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     binary.push_back(static_cast<std::byte>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     bool rexR = source >= 8;
     bool rexB = destination >= 8;
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(rexR) << 2) | static_cast<int>(rexB)));
     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     if (source >= 8 || destination >= 8)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(source >= 8) << 2) | static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));
     if (source >= 8 || destination >= 8)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(source >= 8) << 2) | static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     bool rex = destination >= 8 || source >= 8 || (destination & 7) >= 4 || (source & 7) >= 4;
     if (rex)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(source >= 8) << 2) | static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem64(std::size_t destination,
                                                                           std::size_t destinationOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                             static_cast<int>(destination >= 8)));
     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem32(std::size_t destination,
                                                                           std::size_t destinationOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem16(std::size_t destination,
                                                                           std::size_t destinationOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     binary.push_back(static_cast<std::byte>(0x66));
     if (sourceRegister >= 8 || destination >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(sourceRegister >= 8) << 2) |
                                                  static_cast<int>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));

     if (destinationOffset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((sourceRegister & 7) << 3) | (destination & 7)));
          binary.push_back(static_cast<std::byte>(destinationOffset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((sourceRegister & 7) << 3) | (destination & 7)));
          for (int i = 0; i < 4; ++i) binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem8(
    [[maybe_unused]] std::size_t destination, [[maybe_unused]] std::size_t destinationOffset,
    [[maybe_unused]] std::size_t sourceRegister)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv64(std::size_t reg)
{
     std::vector<std::byte> binary{};
     bool rexR = reg >= 8;
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(rexR) << 2))); // REX.W
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF0 | (reg & 7))); // ModRM: reg=6 for div, but idiv=7, so div=6 = 0xF0
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv32(std::size_t reg)
{
     std::vector<std::byte> binary{};
     if (reg >= 8)
          binary.push_back(
              static_cast<std::byte>(0x40 | (static_cast<int>(reg >= 8) << 2))); // REX prefix for extended regs
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF0 | (reg & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv16(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66)); // operand-size prefix for 16-bit
     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(reg >= 8) << 2)));
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF0 | (reg & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv8(std::size_t reg)
{
     std::vector<std::byte> binary{};
     bool rex = reg >= 8 || (reg & 7) >= 4;
     if (rex) binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(reg >= 8) << 2)));
     binary.push_back(static_cast<std::byte>(0xF6));
     binary.push_back(static_cast<std::byte>(0xF0 | (reg & 7))); // reg=6 for div
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem64(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> code;

     // opcode for 8-bit div: F6 /6
     code.push_back(std::byte{0xF6});

     std::byte mod{};
     if (displacement == 0 && baseReg != 5) // mod=00 no disp, except baseReg=5 means RIP-relative, special case
          mod = std::byte{0x00};
     else if (displacement >= -128 && displacement <= 127)
          mod = std::byte{0x40}; // mod=01
     else
          mod = std::byte{0x80}; // mod=10

     std::uint8_t modrm = (static_cast<std::uint8_t>(mod) & 0xC0) // mod bits
                          | (6 << 3)                              // reg bits = 6 for div
                          | (static_cast<std::uint8_t>(baseReg) & 0x07);

     code.push_back(std::byte{modrm});

     if (mod == std::byte{0x40}) code.push_back(std::byte{static_cast<std::uint8_t>(displacement & 0xFF)});
     else if (mod == std::byte{0x80})
     {
          for (int i = 0; i < 4; ++i)
               code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});
     }

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem32(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> code;

     // operand-size prefix for 16-bit: 0x66
     code.push_back(std::byte{0x66});
     // opcode for 16/32/64 bit div: F7 /6
     code.push_back(std::byte{0xF7});

     std::byte mod{};
     if (displacement == 0 && baseReg != 5) mod = std::byte{0x00};
     else if (displacement >= -128 && displacement <= 127)
          mod = std::byte{0x40};
     else
          mod = std::byte{0x80};

     std::uint8_t modrm =
         (static_cast<std::uint8_t>(mod) & 0xC0) | (6 << 3) | (static_cast<std::uint8_t>(baseReg) & 0x07);

     code.push_back(std::byte{modrm});

     if (mod == std::byte{0x40}) code.push_back(std::byte{static_cast<std::uint8_t>(displacement & 0xFF)});
     else if (mod == std::byte{0x80})
     {
          for (int i = 0; i < 4; ++i)
               code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});
     }

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem16(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> code;

     // opcode F7 /6
     code.push_back(std::byte{0xF7});

     std::byte mod{};
     if (displacement == 0 && baseReg != 5) mod = std::byte{0x00};
     else if (displacement >= -128 && displacement <= 127)
          mod = std::byte{0x40};
     else
          mod = std::byte{0x80};

     std::uint8_t modrm =
         (static_cast<std::uint8_t>(mod) & 0xC0) | (6 << 3) | (static_cast<std::uint8_t>(baseReg) & 0x07);

     code.push_back(std::byte{modrm});

     if (mod == std::byte{0x40}) code.push_back(std::byte{static_cast<std::uint8_t>(displacement & 0xFF)});
     else if (mod == std::byte{0x80})
     {
          for (int i = 0; i < 4; ++i)
               code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});
     }

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem8(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> code;

     // REX prefix for 64-bit: 0x48
     code.push_back(std::byte{0x48});

     // opcode F7 /6
     code.push_back(std::byte{0xF7});

     std::byte mod{};
     if (displacement == 0 && baseReg != 5) mod = std::byte{0x00};
     else if (displacement >= -128 && displacement <= 127)
          mod = std::byte{0x40};
     else
          mod = std::byte{0x80};

     std::uint8_t modrm =
         (static_cast<std::uint8_t>(mod) & 0xC0) | (6 << 3) | (static_cast<std::uint8_t>(baseReg) & 0x07);

     code.push_back(std::byte{modrm});

     if (mod == std::byte{0x40}) code.push_back(std::byte{static_cast<std::uint8_t>(displacement & 0xFF)});
     else if (mod == std::byte{0x80})
     {
          for (int i = 0; i < 4; ++i)
               code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});
     }

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv64(std::size_t reg)
{
     std::vector<std::byte> binary{};
     bool rexR = reg >= 8;
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<int>(rexR) << 2))); // REX.W
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF8 | (reg & 7))); // reg=7 for idiv
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv32(std::size_t reg)
{
     std::vector<std::byte> binary{};
     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(reg >= 8) << 2)));
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF8 | (reg & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv16(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66)); // operand-size prefix for 16-bit
     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(reg >= 8) << 2)));
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF8 | (reg & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv8(std::size_t reg)
{
     std::vector<std::byte> binary{};
     bool rex = reg >= 8 || (reg & 7) >= 4;
     if (rex) binary.push_back(static_cast<std::byte>(0x40 | (static_cast<int>(reg >= 8) << 2)));
     binary.push_back(static_cast<std::byte>(0xF6));
     binary.push_back(static_cast<std::byte>(0xF8 | (reg & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg8(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> code;

     // opcode: 30 /r (XOR r/m8, r8)
     code.push_back(std::byte{0x30});

     // ModRM byte: mod=11 (register), reg=srcReg, rm=destReg
     std::uint8_t modrm = 0xC0 | ((srcReg & 0x7) << 3) | (destReg & 0x7);
     code.push_back(std::byte{modrm});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg16(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> code;

     // operand size prefix for 16-bit
     code.push_back(std::byte{0x66});

     // opcode: 31 /r (XOR r/m16, r16)
     code.push_back(std::byte{0x31});

     // ModRM: mod=11 reg=srcReg rm=destReg
     std::uint8_t modrm = 0xC0 | ((srcReg & 0x7) << 3) | (destReg & 0x7);
     code.push_back(std::byte{modrm});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg32(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> code;

     // opcode: 31 /r (XOR r/m32, r32)
     code.push_back(std::byte{0x31});

     // ModRM: mod=11 reg=srcReg rm=destReg
     std::uint8_t modrm = 0xC0 | ((srcReg & 0x7) << 3) | (destReg & 0x7);
     code.push_back(std::byte{modrm});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg64(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> code;

     // REX prefix for 64-bit, default to 0x48 (W=1)
     code.push_back(std::byte{0x48});

     // opcode: 31 /r (XOR r/m64, r64)
     code.push_back(std::byte{0x31});

     // ModRM: mod=11 reg=srcReg rm=destReg
     std::uint8_t modrm = 0xC0 | ((srcReg & 0x7) << 3) | (destReg & 0x7);
     code.push_back(std::byte{modrm});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GeneratePushReg64(std::size_t reg)
{
     std::vector<std::byte> code;

     if (reg >= 8) code.push_back(std::byte{0x41}); // REX.B for r8..r15
     code.push_back(std::byte{static_cast<std::uint8_t>(0x50 + (reg & 0x7))});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GeneratePopReg64(std::size_t reg)
{
     std::vector<std::byte> code;

     if (reg >= 8) code.push_back(std::byte{0x41}); // REX.B for r8..r15
     code.push_back(std::byte{static_cast<std::uint8_t>(0x58 + (reg & 0x7))});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateIndirectCall(std::int32_t displacement)
{
     displacement -= 5; // subtract the instruction size itself
     std::vector<std::byte> code;
     code.reserve(5); // E8 + rel32

     code.push_back(std::byte{0xe8});
     for (int i = 0; i < 4; ++i) code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateIndirectCall2(std::int32_t displacement)
{
     std::vector<std::byte> code;
     code.reserve(6); // 2 bytes opcode + 4 bytes displacement

     code.push_back(std::byte{0xFF}); // opcode
     code.push_back(std::byte{0x15}); // ModRM for RIP-relative memory

     // RIP-relative displacement (32-bit)
     for (int i = 0; i < 4; ++i) code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateRegisterCall(std::size_t reg)
{
     std::vector<std::byte> code;

     // FF /2 -> ModR/M: 11 010 reg
     std::uint8_t modrm = 0b11010000 | static_cast<std::uint8_t>(reg & 0x07);
     code.push_back(std::byte{0xFF});
     code.push_back(std::byte{modrm});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateLeaToReg(std::size_t sourceRegister,
                                                                std::size_t sourceDisplacement,
                                                                std::size_t destinationRegister)
{
     std::vector<std::byte> code;

     constexpr bool rexW = true;
     const bool rexR = destinationRegister >= 8;
     const bool rexB = sourceRegister >= 8;

     std::uint8_t rex = 0x40;
     if (rexW) rex |= 0x08;
     if (rexR) rex |= 0x04;
     if (rexB) rex |= 0x01;
     code.push_back(std::byte{rex});

     // opcode: LEA r64, m
     code.push_back(std::byte{0x8D});

     const std::uint8_t dst = static_cast<std::uint8_t>(destinationRegister & 7);
     const std::uint8_t base = static_cast<std::uint8_t>(sourceRegister & 7);

     const bool needsSib = base == 4;   // RSP / R12
     const bool forceDisp8 = base == 5; // RBP / R13 with mod=00 is illegal

     std::int64_t disp = static_cast<std::int64_t>(sourceDisplacement);

     std::uint8_t mod{};
     if (disp == 0 && !forceDisp8) mod = 0b00;
     else if (disp >= -128 && disp <= 127)
          mod = 0b01;
     else
          mod = 0b10;

     // ModRM
     std::uint8_t modrm = static_cast<std::uint8_t>((mod << 6) | (dst << 3) | (needsSib ? 4 : base));
     code.push_back(std::byte{modrm});

     // SIB
     if (needsSib)
     {
          std::uint8_t sib = static_cast<std::uint8_t>((0 << 6) | (4 << 3) | base);
          code.push_back(std::byte{sib});
     }

     if (mod == 0b01) { code.push_back(std::byte{static_cast<std::uint8_t>(disp)}); }
     else if (mod == 0b10)
     {
          std::int32_t d32 = static_cast<std::int32_t>(disp);
          for (int i = 0; i < 4; ++i) code.push_back(std::byte{static_cast<std::uint8_t>((d32 >> (i * 8)) & 0xFF)});
     }

     return code;
}

// NOLINTEND(readability-identifier-length)
