#include "Opcodes.h"
#include <RuntimeAssert.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <vector>

// NOLINTBEGIN(readability-identifier-length)

inline namespace detail
{
     constexpr auto MakePusher(auto&& vector)
          requires std::ranges::output_range<decltype(vector), std::byte>
     {
          return [&vector](std::byte b)
          {
               vector.push_back(b);
          };
     }
     template <typename T>
     concept IsPushByteFunctor = requires(T t, std::byte b) {
          { t(b) } -> std::same_as<void>;
     };
     static void Emit(IsPushByteFunctor auto&& push, std::integral auto b)
     {
          push(static_cast<std::byte>(b));
     }
     static void Rex(IsPushByteFunctor auto&& push, bool w, bool r, bool x, bool b)
     {
          std::uint8_t rex = 0x40uz;
          if (w) rex |= 0x08uz;
          if (r) rex |= 0x04uz;
          if (x) rex |= 0x02uz;
          if (b) rex |= 0x01uz;
          if (rex != 0x40uz || w) push(static_cast<std::byte>(rex));
     }
     [[maybe_unused]] static bool RequiresByteRex(const std::size_t reg)
     {
          return reg >= 4 && reg <= 7;
     }
     [[maybe_unused]] static void RexByte(IsPushByteFunctor auto&& push, bool r, bool x, bool b, bool force)
     {
          std::uint8_t rex = 0x40uz;
          if (r) rex |= 0x04uz;
          if (x) rex |= 0x02uz;
          if (b) rex |= 0x01uz;
          if (rex != 0x40uz || force) push(static_cast<std::byte>(rex));
     }
     [[maybe_unused]] static void RexOpt(IsPushByteFunctor auto&& push, bool r, bool x, bool b)
     {
          std::uint8_t rex = 0x40uz;
          if (r) rex |= 0x04uz;
          if (x) rex |= 0x02uz;
          if (b) rex |= 0x01uz;
          if (rex != 0x40uz) push(static_cast<std::byte>(rex));
     }
     [[maybe_unused]] static void ModRM(IsPushByteFunctor auto&& push, std::uint8_t mod, std::uint8_t reg,
                                        std::uint8_t rm)
     {
          push(static_cast<std::byte>(((mod & 3uz) << 6uz) | ((reg & 7uz) << 3uz) | (rm & 7uz)));
     }
     static void Sib(IsPushByteFunctor auto&& push, std::uint8_t scale, std::uint8_t index, std::uint8_t base)
     {
          push(static_cast<std::byte>(((scale & 3uz) << 6uz) | ((index & 7uz) << 3uz) | (base & 7uz)));
     }
     static void Imm16(IsPushByteFunctor auto&& push, std::uint16_t v)
     {
          Emit(push, static_cast<std::uint8_t>(v));
          Emit(push, static_cast<std::uint8_t>(v >> 8uz));
     }
     static void Imm32(IsPushByteFunctor auto&& push, std::uint32_t v)
     {
          Emit(push, static_cast<std::uint8_t>(v));
          Emit(push, static_cast<std::uint8_t>(v >> 8uz));
          Emit(push, static_cast<std::uint8_t>(v >> 16uz));
          Emit(push, static_cast<std::uint8_t>(v >> 24uz));
     }
     static void Imm64(IsPushByteFunctor auto&& push, std::uint64_t v)
     {
          Imm32(push, static_cast<std::uint32_t>(v));
          Imm32(push, static_cast<std::uint32_t>(v >> 32uz));
     }
     static std::uint8_t DispMod(std::uint8_t base, std::int32_t disp)
     {
          if (disp == 0 && (base & 7uz) != 5uz) return 0x00uz; // [base]
          if (disp >= -128 && disp <= 127) return 0x01uz;      // [base+disp8]
          return 0x02uz;                                       // [base+disp32]
     }
     static void EmitDisp(IsPushByteFunctor auto&& push, std::uint8_t mod, std::int32_t disp)
     {
          if (mod == 0x01uz) Emit(push, static_cast<std::uint8_t>(static_cast<std::int8_t>(disp)));
          else if (mod == 0x02uz)
               Imm32(push, static_cast<std::uint32_t>(disp));
     }
     static void ModRMMemory(IsPushByteFunctor auto&& push, std::uint8_t regField, std::uint8_t memReg,
                             std::int32_t disp)
     {
          std::uint8_t mod = DispMod(memReg, disp);

          if ((memReg & 7uz) == 4uz)
          {
               ModRM(push, mod, regField & 7uz, 4uz);
               Sib(push, 0uz, 4uz, memReg & 7uz);
               EmitDisp(push, mod, disp);
               return;
          }

          ModRM(push, mod, regField & 7uz, memReg & 7uz);
          EmitDisp(push, mod, disp);
     }
} // namespace detail

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUD2(void)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0xf);
     Emit(MakePusher(binary), 0xb);
     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCwd(void)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x99);
     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCqo(void)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, false, false, false);
     Emit(MakePusher(binary), 0x99);
     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCdq(void)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x99);
     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCbw(void)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x98);
     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCwde(void)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x98);
     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCdqe(void)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, false, false, false);
     Emit(MakePusher(binary), 0x98);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg64(std::size_t reg, const std::uint64_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= R8;
     reg &= 7;
     Rex(MakePusher(binary), true, false, false, isExtendedRegister);
     if (imm <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
     {
          Emit(MakePusher(binary), 0xc7);
          ModRM(MakePusher(binary), 0b11, 0, static_cast<std::uint8_t>(reg));
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
          return binary;
     }
     Emit(MakePusher(binary), 0xb8 | reg);
     Imm64(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg32(std::size_t reg, const std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= R8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     Emit(MakePusher(binary), 0xb8 | reg);
     Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg16(std::size_t reg, const std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(5);
     Emit(MakePusher(binary), 0x66);
     const bool isExtendedRegister = reg >= R8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     Emit(MakePusher(binary), 0xB8 | reg);
     Imm16(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg8(std::size_t reg, const std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(5);
     const bool isExtendedRegister = reg >= R8;
     const bool needsRex = RequiresByteRex(reg);
     reg &= 7;
     RexByte(MakePusher(binary), false, false, isExtendedRegister, needsRex);
     Emit(MakePusher(binary), 0xB0 | reg);
     Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem64(std::size_t reg, const std::size_t offset,
                                                                     const std::uint64_t imm)
{
     std::vector<std::byte> binary{};
     if (imm > std::numeric_limits<std::uint32_t>::max())
     {
          binary.append_range(GenerateMovImmToMem32(
               reg, offset, static_cast<std::uint32_t>(imm & std::numeric_limits<std::uint32_t>::max())));
          binary.append_range(GenerateMovImmToMem32(reg, offset + 4, static_cast<std::uint32_t>(imm >> 32)));
          return binary;
     }
     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), true, false, false, isExtendedRegister);
     Emit(MakePusher(binary), 0xc7);
     ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem32(std::size_t reg, const std::size_t offset,
                                                                     const std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     Emit(MakePusher(binary), 0xc7);
     ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     Imm32(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem16(std::size_t reg, const std::size_t offset,
                                                                     const std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     Emit(MakePusher(binary), 0xc7);
     ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     Imm16(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem8(std::size_t reg, const std::size_t offset,
                                                                    const std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     Emit(MakePusher(binary), 0xc6);
     ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     const bool isSourceExtendedRegister = source >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;
     source &= 7;
     destination &= 7;
     Rex(MakePusher(binary), true, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source), static_cast<std::uint8_t>(destination));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     const bool isSourceExtendedRegister = source >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;
     source &= 7;
     destination &= 7;
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source), static_cast<std::uint8_t>(destination));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     const bool isSourceExtendedRegister = source >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;
     source &= 7;
     destination &= 7;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source), static_cast<std::uint8_t>(destination));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     const bool isSourceExtendedRegister = source >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;
     const bool needsRex = RequiresByteRex(source) || RequiresByteRex(destination);
     source &= 7;
     destination &= 7;
     RexByte(MakePusher(binary), isSourceExtendedRegister, false, isDestinationExtendedRegister, needsRex);
     Emit(MakePusher(binary), 0x88);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source), static_cast<std::uint8_t>(destination));
     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const auto displacement = static_cast<std::int32_t>(static_cast<std::uint32_t>(destinationOffset));

     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;

     Rex(MakePusher(binary), true, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);

     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), displacement);

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const auto displacement = static_cast<std::int32_t>(static_cast<std::uint32_t>(destinationOffset));

     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;

     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);

     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), displacement);

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const auto displacement = static_cast<std::int32_t>(static_cast<std::uint32_t>(destinationOffset));

     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;

     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);

     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), displacement);

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const auto displacement = static_cast<std::int32_t>(static_cast<std::uint32_t>(destinationOffset));

     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;

     RexByte(MakePusher(binary), isSourceExtendedRegister, false, isDestinationExtendedRegister,
             RequiresByteRex(sourceRegister));
     Emit(MakePusher(binary), 0x88);

     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), displacement);

     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg64(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const auto displacement = static_cast<std::int32_t>(static_cast<std::uint32_t>(sourceOffset));

     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destinationRegister >= 8;

     Rex(MakePusher(binary), true, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x8b);

     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), displacement);

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg32(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const auto displacement = static_cast<std::int32_t>(static_cast<std::uint32_t>(sourceOffset));

     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destinationRegister >= 8;

     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x8b);

     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), displacement);

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg16(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const auto displacement = static_cast<std::int32_t>(static_cast<std::uint32_t>(sourceOffset));

     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destinationRegister >= 8;

     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x8b);

     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), displacement);

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg8(std::size_t destinationRegister,
                                                                    std::size_t sourceOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const auto displacement = static_cast<std::int32_t>(static_cast<std::uint32_t>(sourceOffset));

     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destinationRegister >= 8;

     RexByte(MakePusher(binary), isDestinationExtendedRegister, false, isSourceExtendedRegister,
             RequiresByteRex(destinationRegister));
     Emit(MakePusher(binary), 0x8a);

     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), displacement);

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem8ToReg64(std::size_t destinationRegister,
                                                                                std::size_t sourceOffset,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Rex(MakePusher(binary), true, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb6);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem8ToReg32(std::size_t destinationRegister,
                                                                                std::size_t sourceOffset,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb6);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem8ToReg16(std::size_t destinationRegister,
                                                                                std::size_t sourceOffset,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb6);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem16ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceOffset,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Rex(MakePusher(binary), true, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb7);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem16ToReg32(std::size_t destinationRegister,
                                                                                 std::size_t sourceOffset,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb7);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem32ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceOffset,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x8b);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg8ToReg64(std::size_t destinationRegister,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Rex(MakePusher(binary), true, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb6);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destinationRegister & 7),
           static_cast<std::uint8_t>(sourceRegister & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg8ToReg32(std::size_t destinationRegister,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     RexByte(MakePusher(binary), isDestinationExtendedRegister, false, isSourceExtendedRegister,
             RequiresByteRex(sourceRegister));
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb6);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destinationRegister & 7),
           static_cast<std::uint8_t>(sourceRegister & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg8ToReg16(std::size_t destinationRegister,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Emit(MakePusher(binary), 0x66);
     RexByte(MakePusher(binary), isDestinationExtendedRegister, false, isSourceExtendedRegister,
             RequiresByteRex(sourceRegister));
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb6);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destinationRegister & 7),
           static_cast<std::uint8_t>(sourceRegister & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg16ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Rex(MakePusher(binary), true, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb7);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destinationRegister & 7),
           static_cast<std::uint8_t>(sourceRegister & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg16ToReg32(std::size_t destinationRegister,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xb7);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destinationRegister & 7),
           static_cast<std::uint8_t>(sourceRegister & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg32ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x8b);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destinationRegister & 7),
           static_cast<std::uint8_t>(sourceRegister & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToReg64(std::size_t reg, const std::uint64_t imm)
{
     std::vector<std::byte> binary{};
     if (imm == 0) return binary;

     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), true, false, false, isExtendedRegister);

     if (!isExtendedRegister && reg == 0 && imm >= 0x7f)
     {
          Emit(MakePusher(binary), 0x05);
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
          return binary;
     }
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRM(MakePusher(binary), 0b11, 0, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRM(MakePusher(binary), 0b11, 0, static_cast<std::uint8_t>(reg));
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     if (imm == 0) return binary;

     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);

     if (!isExtendedRegister && reg == 0 && imm >= 0x7f)
     {
          Emit(MakePusher(binary), 0x05);
          Imm32(MakePusher(binary), imm);
          return binary;
     }
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRM(MakePusher(binary), 0b11, 0, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRM(MakePusher(binary), 0b11, 0, static_cast<std::uint8_t>(reg));
          Imm32(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     if (imm == 0) return binary;

     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);

     if (!isExtendedRegister && reg == 0 && imm >= 0x7f)
     {
          Emit(MakePusher(binary), 0x05);
          Imm16(MakePusher(binary), imm);
          return binary;
     }
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRM(MakePusher(binary), 0b11, 0, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRM(MakePusher(binary), 0b11, 0, static_cast<std::uint8_t>(reg));
          Imm16(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     if (imm == 0) return binary;

     const bool isExtendedRegister = reg >= 8;
     const bool needsRex = RequiresByteRex(reg);
     reg &= 7;
     RexByte(MakePusher(binary), false, false, isExtendedRegister, needsRex);

     if (!isExtendedRegister && reg == 0)
     {
          Emit(MakePusher(binary), 0x04);
          Emit(MakePusher(binary), imm);
          return binary;
     }
     Emit(MakePusher(binary), 0x80);
     ModRM(MakePusher(binary), 0b11, 0, static_cast<std::uint8_t>(reg));
     Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem64(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     if (imm == 0) return binary;

     const bool isExtendedRegister = reg >= 8;
     Rex(MakePusher(binary), true, false, false, isExtendedRegister);
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Imm32(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem32(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     if (imm == 0) return binary;

     const bool isExtendedRegister = reg >= 8;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Imm32(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem16(std::size_t reg, std::size_t offset,
                                                                     std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Imm16(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToMem8(std::size_t reg, std::size_t offset,
                                                                    std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     Emit(MakePusher(binary), 0x80);
     ModRMMemory(MakePusher(binary), 0, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
     Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, source >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x01);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, source >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x01);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, source >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x01);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), source >= 8, false, destination >= 8,
             RequiresByteRex(source) || RequiresByteRex(destination));
     Emit(MakePusher(binary), 0x00);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg64(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x03);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg32(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x03);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg16(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x03);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg8(std::size_t destination, std::size_t sourceOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), destination >= 8, false, sourceRegister >= 8, RequiresByteRex(destination));
     Emit(MakePusher(binary), 0x02);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, sourceRegister >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x01);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, sourceRegister >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x01);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, sourceRegister >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x01);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), sourceRegister >= 8, false, destination >= 8, RequiresByteRex(sourceRegister));
     Emit(MakePusher(binary), 0x00);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg64(std::size_t reg, std::uint64_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     Rex(MakePusher(binary), true, false, false, isExtendedRegister);

     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
          Emit(MakePusher(binary), imm);
     }
     else if (imm <= std::numeric_limits<std::uint32_t>::max())
     {
          Emit(MakePusher(binary), 0x81);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
     }
     else
     {
          Emit(MakePusher(binary), 0x48); // REX.W
          Emit(MakePusher(binary), 0xB8); // MOV RAX, imm64
          Imm64(MakePusher(binary), imm);
          Emit(MakePusher(binary), 0x2B); // SUB reg, rax
          ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(reg & 7), 0);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
          Imm32(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
          Imm16(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     RexByte(MakePusher(binary), false, false, isExtendedRegister, RequiresByteRex(reg));
     Emit(MakePusher(binary), 0x80);
     ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
     Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem64(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     Rex(MakePusher(binary), true, false, false, isExtendedRegister);
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Imm32(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem32(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Imm32(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem16(std::size_t reg, std::size_t offset,
                                                                     std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     if (imm <= 0x7f)
     {
          Emit(MakePusher(binary), 0x83);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x81);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Imm16(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem8(std::size_t reg, std::size_t offset,
                                                                    std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
     Emit(MakePusher(binary), 0x80);
     ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
     Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, source >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x29);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, source >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x29);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, source >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x29);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), source >= 8, false, destination >= 8,
             RequiresByteRex(source) || RequiresByteRex(destination));
     Emit(MakePusher(binary), 0x28);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, sourceRegister >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x29);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, sourceRegister >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x29);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, sourceRegister >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x29);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), sourceRegister >= 8, false, destination >= 8, RequiresByteRex(sourceRegister));
     Emit(MakePusher(binary), 0x28);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg64(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x2B);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg32(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x2B);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg16(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x2B);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg8(std::size_t destination, std::size_t sourceOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), destination >= 8, false, sourceRegister >= 8, RequiresByteRex(destination));
     Emit(MakePusher(binary), 0x2A);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg64([[maybe_unused]] std::size_t destination,
                                                                             std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, false, false, source >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(source & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg32([[maybe_unused]] std::size_t destination,
                                                                             std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, source >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(source & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg16([[maybe_unused]] std::size_t destination,
                                                                             std::size_t source)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, source >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(source & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg8([[maybe_unused]] std::size_t destination,
                                                                            std::size_t source)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), false, false, source >= 8, RequiresByteRex(source));
     Emit(MakePusher(binary), 0xF6);
     ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(source & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem64(
     std::size_t destination, std::size_t destinationOffset, [[maybe_unused]] std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, false, false, destination >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRMMemory(MakePusher(binary), 4, static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem32(
     std::size_t destination, std::size_t destinationOffset, [[maybe_unused]] std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, destination >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRMMemory(MakePusher(binary), 4, static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem16(
     std::size_t destination, std::size_t destinationOffset, [[maybe_unused]] std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, destination >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRMMemory(MakePusher(binary), 4, static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem8(std::size_t destination,
                                                                            std::size_t destinationOffset,
                                                                            [[maybe_unused]] std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, destination >= 8);
     Emit(MakePusher(binary), 0xF6);
     ModRMMemory(MakePusher(binary), 4, static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg64(std::size_t reg, std::uint64_t imm)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, reg >= 8, false, reg >= 8);
     Emit(MakePusher(binary), 0x69);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7));
     Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, reg >= 8, false, reg >= 8);
     Emit(MakePusher(binary), 0x69);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7));
     Imm32(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, reg >= 8, false, reg >= 8);
     Emit(MakePusher(binary), 0x69);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7));
     Imm16(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, reg >= 8, false, reg >= 8);
     Emit(MakePusher(binary), 0x6B);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7));
     Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem64(std::size_t reg, std::size_t offset,
                                                                           std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, false, false, reg >= 8);
     Emit(MakePusher(binary), 0x69);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7),
                 static_cast<std::int32_t>(offset));
     Imm32(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem32(std::size_t reg, std::size_t offset,
                                                                           std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0x69);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7),
                 static_cast<std::int32_t>(offset));
     Imm32(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem16(std::size_t reg, std::size_t offset,
                                                                           std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0x69);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7),
                 static_cast<std::int32_t>(offset));
     Imm16(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem8(std::size_t reg, std::size_t offset,
                                                                          std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0x6B);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7),
                 static_cast<std::int32_t>(offset));
     Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, destination >= 8, false, source >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destination & 7), static_cast<std::uint8_t>(source & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, destination >= 8, false, source >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destination & 7), static_cast<std::uint8_t>(source & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, destination >= 8, false, source >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destination & 7), static_cast<std::uint8_t>(source & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, destination >= 8, false, source >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(destination & 7), static_cast<std::uint8_t>(source & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem64(std::size_t destination,
                                                                           std::size_t destinationOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, sourceRegister >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem32(std::size_t destination,
                                                                           std::size_t destinationOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, sourceRegister >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem16(std::size_t destination,
                                                                           std::size_t destinationOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, sourceRegister >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem8(
     [[maybe_unused]] std::size_t destination, [[maybe_unused]] std::size_t destinationOffset,
     [[maybe_unused]] std::size_t sourceRegister)
{
     return {};
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulMemToReg64(std::size_t destination,
                                                                           std::size_t sourceOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulMemToReg32(std::size_t destination,
                                                                           std::size_t sourceOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulMemToReg16(std::size_t destination,
                                                                           std::size_t sourceOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulMemToReg8(std::size_t destination,
                                                                          std::size_t sourceOffset,
                                                                          std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, destination >= 8, false, sourceRegister >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destination & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv64(std::size_t reg)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, false, false, reg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRM(MakePusher(binary), 0b11, 6, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv32(std::size_t reg)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRM(MakePusher(binary), 0b11, 6, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv16(std::size_t reg)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRM(MakePusher(binary), 0b11, 6, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv8(std::size_t reg)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), false, false, reg >= 8, RequiresByteRex(reg));
     Emit(MakePusher(binary), 0xF6);
     ModRM(MakePusher(binary), 0b11, 6, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem64(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, false, false, baseReg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRMMemory(MakePusher(binary), 6, static_cast<std::uint8_t>(baseReg & 7), displacement);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem32(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, baseReg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRMMemory(MakePusher(binary), 6, static_cast<std::uint8_t>(baseReg & 7), displacement);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem16(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, baseReg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRMMemory(MakePusher(binary), 6, static_cast<std::uint8_t>(baseReg & 7), displacement);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem8(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, baseReg >= 8);
     Emit(MakePusher(binary), 0xF6);
     ModRMMemory(MakePusher(binary), 6, static_cast<std::uint8_t>(baseReg & 7), displacement);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDivMem64(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, false, false, baseReg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRMMemory(MakePusher(binary), 7, static_cast<std::uint8_t>(baseReg & 7), displacement);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDivMem32(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, baseReg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRMMemory(MakePusher(binary), 7, static_cast<std::uint8_t>(baseReg & 7), displacement);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDivMem16(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, baseReg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRMMemory(MakePusher(binary), 7, static_cast<std::uint8_t>(baseReg & 7), displacement);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDivMem8(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, baseReg >= 8);
     Emit(MakePusher(binary), 0xF6);
     ModRMMemory(MakePusher(binary), 7, static_cast<std::uint8_t>(baseReg & 7), displacement);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv64(std::size_t reg)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, false, false, reg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv32(std::size_t reg)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv16(std::size_t reg)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0xF7);
     ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv8(std::size_t reg)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), false, false, reg >= 8, RequiresByteRex(reg));
     Emit(MakePusher(binary), 0xF6);
     ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg8(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> binary{};
     RexByte(MakePusher(binary), srcReg >= 8, false, destReg >= 8, RequiresByteRex(srcReg) || RequiresByteRex(destReg));
     Emit(MakePusher(binary), 0x30);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(srcReg & 7), static_cast<std::uint8_t>(destReg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg16(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, srcReg >= 8, false, destReg >= 8);
     Emit(MakePusher(binary), 0x31);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(srcReg & 7), static_cast<std::uint8_t>(destReg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg32(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, srcReg >= 8, false, destReg >= 8);
     Emit(MakePusher(binary), 0x31);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(srcReg & 7), static_cast<std::uint8_t>(destReg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg64(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), true, srcReg >= 8, false, destReg >= 8);
     Emit(MakePusher(binary), 0x31);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(srcReg & 7), static_cast<std::uint8_t>(destReg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GeneratePushReg64(std::size_t reg)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0x50 | (reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GeneratePopReg64(std::size_t reg)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0x58 | (reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateIndirectCall(std::int32_t displacement)
{
     displacement -= 5;
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0xe8);
     Imm32(MakePusher(binary), static_cast<std::uint32_t>(displacement));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateIndirectCall2(std::int32_t displacement)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0xFF);
     Emit(MakePusher(binary), 0x15);
     Imm32(MakePusher(binary), static_cast<std::uint32_t>(displacement));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateRegisterCall(std::size_t reg)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0xFF);
     ModRM(MakePusher(binary), 0b11, 2, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateLeaToReg(std::size_t sourceRegister,
                                                                std::size_t sourceDisplacement,
                                                                std::size_t destinationRegister)
{
     std::vector<std::byte> binary{};
     const bool rexR = destinationRegister >= 8;
     const bool rexB = sourceRegister >= 8;

     if (sourceRegister == Rip)
     {
          Rex(MakePusher(binary), true, rexR, false, false);
          Emit(MakePusher(binary), 0x8D);
          ModRM(MakePusher(binary), 0b00, static_cast<std::uint8_t>(destinationRegister & 7), 0b101);
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(static_cast<std::int32_t>(sourceDisplacement)));
          return binary;
     }

     Rex(MakePusher(binary), true, rexR, false, rexB);
     Emit(MakePusher(binary), 0x8D);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceDisplacement));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedSarImmToReg64(std::size_t reg, std::uint64_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexB = reg >= 8;

     Rex(MakePusher(binary), true, false, false, rexB);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));
     }
     else
     {
          Emit(MakePusher(binary), 0xc1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));

          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedSarImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexB = reg >= 8;

     Rex(MakePusher(binary), false, false, false, rexB);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));
     }
     else
     {
          Emit(MakePusher(binary), 0xc1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));

          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedSarImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexB = reg >= 8;

     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, rexB);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));
     }
     else
     {
          Emit(MakePusher(binary), 0xc1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));

          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedSarImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexB = reg >= 8;

     RexByte(MakePusher(binary), false, false, rexB, RequiresByteRex(reg));

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd0);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));
     }
     else
     {
          Emit(MakePusher(binary), 0xc0);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));

          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedShrImmToReg64(std::size_t reg, std::uint64_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexB = reg >= 8;

     Rex(MakePusher(binary), true, false, false, rexB);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd1);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
     }
     else
     {
          Emit(MakePusher(binary), 0xc1);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));

          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedShrImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexB = reg >= 8;

     Rex(MakePusher(binary), false, false, false, rexB);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd1);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
     }
     else
     {
          Emit(MakePusher(binary), 0xc1);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));

          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedShrImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexB = reg >= 8;

     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, rexB);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd1);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
     }
     else
     {
          Emit(MakePusher(binary), 0xc1);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));

          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedShrImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};

     const bool rexB = reg >= 8;

     RexByte(MakePusher(binary), false, false, rexB, RequiresByteRex(reg));

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd0);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));
     }
     else
     {
          Emit(MakePusher(binary), 0xc0);
          ModRM(MakePusher(binary), 0b11, 5, static_cast<std::uint8_t>(reg & 7));

          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedShrImmToMem64(std::size_t reg, std::size_t offset,
                                                                           std::uint64_t imm)
{
     std::vector<std::byte> binary{};

     const bool isExtendedRegister = reg >= 8;

     Rex(MakePusher(binary), true, false, false, isExtendedRegister);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd1);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
     }
     else
     {
          Emit(MakePusher(binary), 0xc1);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xff));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedShrImmToMem32(std::size_t reg, std::size_t offset,
                                                                           std::uint32_t imm)
{
     std::vector<std::byte> binary{};

     const bool isExtendedRegister = reg >= 8;

     Rex(MakePusher(binary), false, false, false, isExtendedRegister);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd1);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
     }
     else
     {
          Emit(MakePusher(binary), 0xc1);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xff));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedShrImmToMem16(std::size_t reg, std::size_t offset,
                                                                           std::uint16_t imm)
{
     std::vector<std::byte> binary{};

     const bool isExtendedRegister = reg >= 8;

     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd1);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
     }
     else
     {
          Emit(MakePusher(binary), 0xc1);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), static_cast<std::uint8_t>(imm & 0xff));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedShrImmToMem8(std::size_t reg, std::size_t offset,
                                                                          std::uint8_t imm)
{
     std::vector<std::byte> binary{};

     const bool isExtendedRegister = reg >= 8;

     RexByte(MakePusher(binary), false, false, isExtendedRegister, false);

     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xd0);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
     }
     else
     {
          Emit(MakePusher(binary), 0xc0);
          ModRMMemory(MakePusher(binary), 5, static_cast<std::uint8_t>(reg & 7), static_cast<std::int32_t>(offset));
          Emit(MakePusher(binary), imm);
     }

     return binary;
}

[[nodiscard]] std::vector<std::byte> ecpps::codegen::x86_64::GenerateNegReg8(std::size_t reg)
{
     std::vector<std::byte> binary{};
     const bool isRegisterExtended = reg >= 8;
     const bool needsRex = RequiresByteRex(reg);
     reg &= 7;
     RexByte(MakePusher(binary), false, false, isRegisterExtended, needsRex);
     Emit(MakePusher(binary), 0xf6);
     ModRM(MakePusher(binary), 0b11, 3, static_cast<std::uint8_t>(reg));
     return binary;
}
[[nodiscard]] std::vector<std::byte> ecpps::codegen::x86_64::GenerateNegReg16(std::size_t reg)
{
     std::vector<std::byte> binary{};
     const bool isRegisterExtended = reg >= 8;
     reg &= 7;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, false, false, isRegisterExtended);
     Emit(MakePusher(binary), 0xf7);
     ModRM(MakePusher(binary), 0b11, 3, static_cast<std::uint8_t>(reg));
     return binary;
}
[[nodiscard]] std::vector<std::byte> ecpps::codegen::x86_64::GenerateNegReg32(std::size_t reg)
{
     std::vector<std::byte> binary{};
     const bool isRegisterExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isRegisterExtended);
     Emit(MakePusher(binary), 0xf7);
     ModRM(MakePusher(binary), 0b11, 3, static_cast<std::uint8_t>(reg));
     return binary;
}
[[nodiscard]] std::vector<std::byte> ecpps::codegen::x86_64::GenerateNegReg64(std::size_t reg)
{
     std::vector<std::byte> binary{};
     const bool isRegisterExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), true, false, false, isRegisterExtended);
     Emit(MakePusher(binary), 0xf7);
     ModRM(MakePusher(binary), 0b11, 3, static_cast<std::uint8_t>(reg));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSalReg64(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), true, false, false, isExtended);
     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xD1);
          ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(reg));
     }
     else
     {
          Emit(MakePusher(binary), 0xC1);
          ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSalReg32(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xD1);
          ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(reg));
     }
     else
     {
          Emit(MakePusher(binary), 0xC1);
          ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSalReg16(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xD1);
          ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(reg));
     }
     else
     {
          Emit(MakePusher(binary), 0xC1);
          ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSalReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     const bool needsRex = RequiresByteRex(reg);
     reg &= 7;
     RexByte(MakePusher(binary), false, false, isExtended, needsRex);
     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xD0);
          ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(reg));
     }
     else
     {
          Emit(MakePusher(binary), 0xC0);
          ModRM(MakePusher(binary), 0b11, 4, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSalMem64(std::size_t reg, std::size_t offset, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), true, false, false, isExtended);
     Emit(MakePusher(binary), imm == 1 ? 0xD1 : 0xC1);
     ModRMMemory(MakePusher(binary), 4, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     if (imm != 1) Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSalMem32(std::size_t reg, std::size_t offset, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     Emit(MakePusher(binary), imm == 1 ? 0xD1 : 0xC1);
     ModRMMemory(MakePusher(binary), 4, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     if (imm != 1) Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSalMem16(std::size_t reg, std::size_t offset, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     Emit(MakePusher(binary), imm == 1 ? 0xD1 : 0xC1);
     ModRMMemory(MakePusher(binary), 4, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     if (imm != 1) Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSalMem8(std::size_t reg, std::size_t offset, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     Emit(MakePusher(binary), imm == 1 ? 0xD0 : 0xC0);
     ModRMMemory(MakePusher(binary), 4, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     if (imm != 1) Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSarReg64(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), true, false, false, isExtended);
     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xD1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg));
     }
     else
     {
          Emit(MakePusher(binary), 0xC1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSarReg32(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xD1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg));
     }
     else
     {
          Emit(MakePusher(binary), 0xC1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSarReg16(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xD1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg));
     }
     else
     {
          Emit(MakePusher(binary), 0xC1);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSarReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     const bool needsRex = RequiresByteRex(reg);
     reg &= 7;
     RexByte(MakePusher(binary), false, false, isExtended, needsRex);
     if (imm == 1)
     {
          Emit(MakePusher(binary), 0xD0);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg));
     }
     else
     {
          Emit(MakePusher(binary), 0xC0);
          ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg));
          Emit(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSarMem64(std::size_t reg, std::size_t offset, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), true, false, false, isExtended);
     Emit(MakePusher(binary), imm == 1 ? 0xD1 : 0xC1);
     ModRMMemory(MakePusher(binary), 7, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     if (imm != 1) Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSarMem32(std::size_t reg, std::size_t offset, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     Emit(MakePusher(binary), imm == 1 ? 0xD1 : 0xC1);
     ModRMMemory(MakePusher(binary), 7, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     if (imm != 1) Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSarMem16(std::size_t reg, std::size_t offset, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     Emit(MakePusher(binary), imm == 1 ? 0xD1 : 0xC1);
     ModRMMemory(MakePusher(binary), 7, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     if (imm != 1) Emit(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSarMem8(std::size_t reg, std::size_t offset, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtended = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, false, false, isExtended);
     Emit(MakePusher(binary), imm == 1 ? 0xD0 : 0xC0);
     ModRMMemory(MakePusher(binary), 7, static_cast<std::uint8_t>(reg), static_cast<std::int32_t>(offset));
     if (imm != 1) Emit(MakePusher(binary), imm);
     return binary;
}

using Bytes = std::vector<std::byte>;
using namespace ecpps::codegen::x86_64;

namespace
{
     void Put(Bytes& out, const std::size_t value)
     {
          out.push_back(static_cast<std::byte>(value & 0xFFuz));
     }
     void PutSeq(Bytes& out, std::initializer_list<std::size_t> seq)
     {
          for (const std::size_t b : seq) Put(out, b);
     }
     void PutImm(Bytes& out, const std::int64_t value, const std::size_t count)
     {
          const auto bits = static_cast<std::uint64_t>(value);
          for (std::size_t i : std::views::iota(0uz, count))
               Put(out, static_cast<std::size_t>((bits >> (8uz * i)) & 0xFFuz));
     }
     void PutModRM(Bytes& out, const std::size_t mod, const std::size_t reg, const std::size_t rm)
     {
          Put(out,
              (mod << 6uz) | ((static_cast<std::size_t>(reg) & 7uz) << 3uz) | (static_cast<std::size_t>(rm) & 7uz));
     }
     void PutRex(Bytes& out, const bool w, const bool r, const bool x, const bool b, const bool force)
     {
          std::size_t rex = 0x40uz;
          if (w) rex |= 8uz;
          if (r) rex |= 4uz;
          if (x) rex |= 2uz;
          if (b) rex |= 1uz;
          if (rex != 0x40uz || force) Put(out, rex);
     }
     std::size_t ScaleBits(const std::uint8_t scale)
     {
          switch (scale)
          {
          case 2: return 1uz;
          case 4: return 2uz;
          case 8: return 3uz;
          default: return 0uz;
          }
     }
     bool IsByte(const Width size)
     {
          return size == Width::W8;
     }
     std::size_t ImmSize(const Width size)
     {
          return size == Width::W8 ? 1uz : size == Width::W16 ? 2uz : 4uz;
     }
     bool FitsI8(const std::int64_t v)
     {
          return v >= -128 && v <= 127;
     }
     bool ImmFits(const Width size, const std::int64_t v)
     {
          switch (size)
          {
          case Width::W8:
               return v >= std::numeric_limits<std::int8_t>::min() && v <= std::numeric_limits<std::uint8_t>::max();
          case Width::W16:
               return v >= std::numeric_limits<std::int16_t>::min() && v <= std::numeric_limits<std::uint16_t>::max();
          case Width::W32:
               return v >= std::numeric_limits<std::int32_t>::min() &&
                      v <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
          case Width::W64:
               return v >= std::numeric_limits<std::int32_t>::min() && v <= std::numeric_limits<std::int32_t>::max();
          }
          return false;
     }
     std::int64_t ImmNormalize(const Width size, const std::int64_t v)
     {
          switch (size)
          {
          case Width::W8: return static_cast<std::int8_t>(static_cast<std::uint8_t>(v));
          case Width::W16: return static_cast<std::int16_t>(static_cast<std::uint16_t>(v));
          case Width::W32: return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
          case Width::W64: return v;
          }
          return v;
     }

     bool MemExtBase(const Memory& m)
     {
          return m.base != NoRegister && m.base != Rip && m.base >= 8;
     }
     bool MemExtIndex(const Memory& m)
     {
          return m.index != NoRegister && m.index >= 8;
     }

     void PutMemOperand(Bytes& out, const std::size_t regField, const Memory& m)
     {
          if (m.base == Rip)
          {
               PutModRM(out, 0uz, regField, 5uz);
               PutImm(out, m.displacement, 4);
               return;
          }
          if (m.base == NoRegister)
          {
               PutModRM(out, 0uz, regField, 4uz);
               const std::size_t idx = m.index == NoRegister ? 4uz : static_cast<std::size_t>(m.index & 7uz);
               Put(out, (ScaleBits(m.scale) << 6uz) | (idx << 3uz) | 5uz);
               PutImm(out, m.displacement, 4);
               return;
          }
          const auto base = static_cast<std::size_t>(m.base & 7uz);
          const bool needSib = m.index != NoRegister || base == 4uz;
          std::size_t mod = 2uz;
          if (m.displacement == 0 && base != 5uz) mod = 0uz;
          else if (FitsI8(m.displacement))
               mod = 1uz;
          PutModRM(out, mod, regField, needSib ? 4uz : base);
          if (needSib)
          {
               const std::size_t idx = m.index == NoRegister ? 4uz : static_cast<std::size_t>(m.index & 7uz);
               Put(out, (ScaleBits(m.scale) << 6uz) | (idx << 3uz) | base);
          }
          if (mod == 1uz) PutImm(out, m.displacement, 1);
          else if (mod == 2uz)
               PutImm(out, m.displacement, 4);
     }

     void EmitReg(Bytes& out, const Width size, std::initializer_list<std::size_t> op, const std::size_t reg,
                  const std::size_t rm, const bool regIsByte, const bool rmIsByte)
     {
          if (size == Width::W16) Put(out, 0x66uz);
          const bool force = (regIsByte && RequiresByteRex(reg)) || (rmIsByte && RequiresByteRex(rm));
          PutRex(out, size == Width::W64, reg >= 8, false, rm >= 8, force);
          PutSeq(out, op);
          PutModRM(out, 3uz, reg, rm);
     }
     void EmitMem(Bytes& out, const Width size, std::initializer_list<std::size_t> op, const std::size_t regField,
                  const Memory& mem, const bool regIsByte)
     {
          if (size == Width::W16) Put(out, 0x66uz);
          PutRex(out, size == Width::W64, regField >= 8, MemExtIndex(mem), MemExtBase(mem),
                 regIsByte && RequiresByteRex(regField));
          PutSeq(out, op);
          PutMemOperand(out, regField, mem);
     }

     struct ExtendEncoding
     {
          bool valid = false;
          std::size_t length = 0;
          std::size_t bytes[2]{};
          Width size = Width::W32;
     };
     ExtendEncoding PickExtend(const ExtendKind kind, const Width destination, const Width source)
     {
          ExtendEncoding e{};
          if (static_cast<std::size_t>(destination) <= static_cast<std::size_t>(source)) return e;
          const bool sign = kind == ExtendKind::Sign;
          e.size = destination;
          e.valid = true;
          switch (source)
          {
          case Width::W8:
               e.length = 2;
               e.bytes[0] = 0x0Fu;
               e.bytes[1] = sign ? 0xBEu : 0xB6u;
               break;
          case Width::W16:
               e.length = 2;
               e.bytes[0] = 0x0Fu;
               e.bytes[1] = sign ? 0xBFu : 0xB7u;
               break;
          case Width::W32:
               e.length = 1;
               if (sign) e.bytes[0] = 0x63uz;
               else
               {
                    e.bytes[0] = 0x8Bu;
                    e.size = Width::W32;
               }
               break;
          default: e.valid = false; break;
          }
          return e;
     }
     void PutExtendOps(Bytes& out, const ExtendEncoding& e)
     {
          for (auto i : std::views::iota(0uz, e.length)) Put(out, e.bytes[i]);
     }
} // namespace

std::vector<std::byte> ecpps::codegen::x86_64::GenerateNopN(std::size_t count)
{
     Bytes out{};
     while (count > 0)
     {
          const std::size_t n = count < 9 ? count : 9;
          switch (n)
          {
          case 1: PutSeq(out, {0x90uz}); break;
          case 2: PutSeq(out, {0x66uz, 0x90uz}); break;
          case 3: PutSeq(out, {0x0Fu, 0x1Fu, 0x00uz}); break;
          case 4: PutSeq(out, {0x0Fu, 0x1Fu, 0x40uz, 0x00uz}); break;
          case 5: PutSeq(out, {0x0Fu, 0x1Fu, 0x44uz, 0x00uz, 0x00uz}); break;
          case 6: PutSeq(out, {0x66uz, 0x0Fu, 0x1Fu, 0x44uz, 0x00uz, 0x00uz}); break;
          case 7: PutSeq(out, {0x0Fu, 0x1Fu, 0x80uz, 0x00uz, 0x00uz, 0x00uz, 0x00uz}); break;
          case 8: PutSeq(out, {0x0Fu, 0x1Fu, 0x84uz, 0x00uz, 0x00uz, 0x00uz, 0x00uz, 0x00uz}); break;
          default: PutSeq(out, {0x66uz, 0x0Fu, 0x1Fu, 0x84uz, 0x00uz, 0x00uz, 0x00uz, 0x00uz, 0x00uz}); break;
          }
          count -= n;
     }
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateRetImm(const std::uint16_t popBytes)
{
     Bytes out{};
     Put(out, 0xC2u);
     PutImm(out, popBytes, 2);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateInt(const std::uint8_t vector)
{
     Bytes out{};
     PutSeq(out, {0xCDu, vector});
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAluRegReg(const AluOp op, const Width size,
                                                                 const std::size_t destination,
                                                                 const std::size_t source)
{
     Bytes out{};
     const std::size_t opc = (static_cast<std::size_t>(op) << 3uz) | (IsByte(size) ? 0uz : 1uz);
     EmitReg(out, size, {opc}, source, destination, IsByte(size), IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateAluRegMem(const AluOp op, const Width size,
                                                                 const Memory& destination, const std::size_t source)
{
     Bytes out{};
     const std::size_t opc = (static_cast<std::size_t>(op) << 3uz) | (IsByte(size) ? 0uz : 1uz);
     EmitMem(out, size, {opc}, source, destination, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateAluMemReg(const AluOp op, const Width size,
                                                                 const std::size_t destination, const Memory& source)
{
     Bytes out{};
     const std::size_t opc = (static_cast<std::size_t>(op) << 3uz) | 2uz | (IsByte(size) ? 0uz : 1uz);
     EmitMem(out, size, {opc}, destination, source, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateAluImmReg(const AluOp op, const Width size,
                                                                 const std::size_t reg, std::int64_t imm)
{
     Bytes out{};
     if (!ImmFits(size, imm)) return out;
     imm = ImmNormalize(size, imm);
     const auto ext = static_cast<std::size_t>(op);
     if (IsByte(size))
     {
          if (reg == Rax)
          {
               Put(out, (ext << 3uz) | 4uz);
               PutImm(out, imm, 1);
               return out;
          }
          EmitReg(out, size, {0x80uz}, ext, reg, false, true);
          PutImm(out, imm, 1);
          return out;
     }
     if (FitsI8(imm))
     {
          EmitReg(out, size, {0x83uz}, ext, reg, false, false);
          PutImm(out, imm, 1);
          return out;
     }
     if (reg == Rax)
     {
          if (size == Width::W16) Put(out, 0x66uz);
          PutRex(out, size == Width::W64, false, false, false, false);
          Put(out, (ext << 3uz) | 5uz);
          PutImm(out, imm, ImmSize(size));
          return out;
     }
     EmitReg(out, size, {0x81uz}, ext, reg, false, false);
     PutImm(out, imm, ImmSize(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateAluImmMem(const AluOp op, const Width size,
                                                                 const Memory& destination, std::int64_t imm)
{
     Bytes out{};
     if (!ImmFits(size, imm)) return out;
     imm = ImmNormalize(size, imm);
     const auto ext = static_cast<std::size_t>(op);
     if (IsByte(size))
     {
          EmitMem(out, size, {0x80uz}, ext, destination, false);
          PutImm(out, imm, 1);
     }
     else if (FitsI8(imm))
     {
          EmitMem(out, size, {0x83uz}, ext, destination, false);
          PutImm(out, imm, 1);
     }
     else
     {
          EmitMem(out, size, {0x81uz}, ext, destination, false);
          PutImm(out, imm, ImmSize(size));
     }
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateTestRegReg(const Width size, const std::size_t a,
                                                                  const std::size_t b)
{
     Bytes out{};
     EmitReg(out, size, {IsByte(size) ? 0x84uz : 0x85uz}, b, a, IsByte(size), IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateTestMemReg(const Width size, const Memory& a,
                                                                  const std::size_t b)
{
     Bytes out{};
     EmitMem(out, size, {IsByte(size) ? 0x84uz : 0x85uz}, b, a, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateTestImmReg(const Width size, const std::size_t reg,
                                                                  std::int64_t imm)
{
     Bytes out{};
     if (!ImmFits(size, imm)) return out;
     imm = ImmNormalize(size, imm);
     if (reg == Rax)
     {
          if (size == Width::W16) Put(out, 0x66uz);
          PutRex(out, size == Width::W64, false, false, false, false);
          Put(out, IsByte(size) ? 0xA8u : 0xA9u);
     }
     else
          EmitReg(out, size, {IsByte(size) ? 0xF6u : 0xF7u}, 0, reg, false, IsByte(size));
     PutImm(out, imm, ImmSize(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateTestImmMem(const Width size, const Memory& mem, std::int64_t imm)
{
     Bytes out{};
     if (!ImmFits(size, imm)) return out;
     imm = ImmNormalize(size, imm);
     EmitMem(out, size, {IsByte(size) ? 0xF6u : 0xF7u}, 0, mem, false);
     PutImm(out, imm, ImmSize(size));
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovLoad(const Width size, const std::size_t destination,
                                                               const Memory& source)
{
     Bytes out{};
     EmitMem(out, size, {IsByte(size) ? 0x8Au : 0x8Bu}, destination, source, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovStore(const Width size, const Memory& destination,
                                                                const std::size_t source)
{
     Bytes out{};
     EmitMem(out, size, {IsByte(size) ? 0x88uz : 0x89uz}, source, destination, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovStoreImm(const Width size, const Memory& destination,
                                                                   std::int64_t imm)
{
     Bytes out{};
     if (!ImmFits(size, imm)) return out;
     imm = ImmNormalize(size, imm);
     EmitMem(out, size, {IsByte(size) ? 0xC6u : 0xC7u}, 0, destination, false);
     PutImm(out, imm, ImmSize(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovExtendRegToReg(const ExtendKind kind,
                                                                         const Width destinationSize,
                                                                         const Width sourceSize,
                                                                         const std::size_t destination,
                                                                         const std::size_t source)
{
     Bytes out{};
     const ExtendEncoding e = PickExtend(kind, destinationSize, sourceSize);
     if (!e.valid) return out;
     if (e.size == Width::W16) Put(out, 0x66uz);
     const bool force = sourceSize == Width::W8 && RequiresByteRex(source);
     PutRex(out, e.size == Width::W64, destination >= 8, false, source >= 8, force);
     PutExtendOps(out, e);
     PutModRM(out, 3uz, destination, source);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovExtendMemToReg(const ExtendKind kind,
                                                                         const Width destinationSize,
                                                                         const Width sourceSize,
                                                                         const std::size_t destination,
                                                                         const Memory& source)
{
     Bytes out{};
     const ExtendEncoding e = PickExtend(kind, destinationSize, sourceSize);
     if (!e.valid) return out;
     if (e.size == Width::W16) Put(out, 0x66uz);
     PutRex(out, e.size == Width::W64, destination >= 8, MemExtIndex(source), MemExtBase(source), false);
     PutExtendOps(out, e);
     PutMemOperand(out, destination, source);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateXchgRegReg(const Width size, const std::size_t a,
                                                                  const std::size_t b)
{
     Bytes out{};
     EmitReg(out, size, {IsByte(size) ? 0x86uz : 0x87uz}, b, a, IsByte(size), IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateXchgMemReg(const Width size, const Memory& mem,
                                                                  const std::size_t reg)
{
     Bytes out{};
     EmitMem(out, size, {IsByte(size) ? 0x86uz : 0x87uz}, reg, mem, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCmpxchgMemReg(const Width size, const Memory& destination,
                                                                     const std::size_t source)
{
     Bytes out{};
     EmitMem(out, size, {0x0Fu, IsByte(size) ? 0xB0u : 0xB1u}, source, destination, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateXaddMemReg(const Width size, const Memory& destination,
                                                                  const std::size_t source)
{
     Bytes out{};
     EmitMem(out, size, {0x0Fu, IsByte(size) ? 0xC0u : 0xC1u}, source, destination, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCmovRegReg(const Condition cc, const Width size,
                                                                  const std::size_t destination,
                                                                  const std::size_t source)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitReg(out, size, {0x0Fu, 0x40uz + static_cast<std::size_t>(cc)}, destination, source, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCmovMemReg(const Condition cc, const Width size,
                                                                  const std::size_t destination, const Memory& source)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitMem(out, size, {0x0Fu, 0x40uz + static_cast<std::size_t>(cc)}, destination, source, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateSetccReg(const Condition cc, const std::size_t reg)
{
     Bytes out{};
     EmitReg(out, Width::W8, {0x0Fu, 0x90uz + static_cast<std::size_t>(cc)}, 0, reg, false, true);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateSetccMem(const Condition cc, const Memory& mem)
{
     Bytes out{};
     EmitMem(out, Width::W8, {0x0Fu, 0x90uz + static_cast<std::size_t>(cc)}, 0, mem, false);
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateLea(const Width size, const std::size_t destination,
                                                           const Memory& source)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitMem(out, size, {0x8Du}, destination, source, false);
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnaryReg(const UnaryOp op, const Width size,
                                                                const std::size_t reg)
{
     Bytes out{};
     const std::size_t ext = static_cast<std::size_t>(op);
     const std::size_t opc = (op <= UnaryOp::Dec ? 0xFEu : 0xF6u) | (IsByte(size) ? 0uz : 1uz);
     EmitReg(out, size, {opc}, ext, reg, false, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnaryMem(const UnaryOp op, const Width size, const Memory& mem)
{
     Bytes out{};
     const std::size_t ext = static_cast<std::size_t>(op);
     const std::size_t opc = (op <= UnaryOp::Dec ? 0xFEu : 0xF6u) | (IsByte(size) ? 0uz : 1uz);
     EmitMem(out, size, {opc}, ext, mem, false);
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateImulRegReg(const Width size, const std::size_t destination,
                                                                  const std::size_t source)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitReg(out, size, {0x0Fu, 0xAFu}, destination, source, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateImulRegMem(const Width size, const std::size_t destination,
                                                                  const Memory& source)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitMem(out, size, {0x0Fu, 0xAFu}, destination, source, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateImulRegRegImm(const Width size, const std::size_t destination,
                                                                     const std::size_t source, std::int64_t imm)
{
     Bytes out{};
     if (IsByte(size) || !ImmFits(size, imm)) return out;
     imm = ImmNormalize(size, imm);
     const bool small = FitsI8(imm);
     EmitReg(out, size, {small ? 0x6Bu : 0x69uz}, destination, source, false, false);
     PutImm(out, imm, small ? 1uz : ImmSize(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateImulRegMemImm(const Width size, const std::size_t destination,
                                                                     const Memory& source, std::int64_t imm)
{
     Bytes out{};
     if (IsByte(size) || !ImmFits(size, imm)) return out;
     imm = ImmNormalize(size, imm);
     const bool small = FitsI8(imm);
     EmitMem(out, size, {small ? 0x6Bu : 0x69uz}, destination, source, false);
     PutImm(out, imm, small ? 1uz : ImmSize(size));
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateShiftRegImm(const ShiftOp op, const Width size,
                                                                   const std::size_t reg, const std::uint8_t count)
{
     Bytes out{};
     const std::size_t ext = static_cast<std::size_t>(op);
     if (count == 1)
     {
          EmitReg(out, size, {IsByte(size) ? 0xD0u : 0xD1u}, ext, reg, false, IsByte(size));
          return out;
     }
     EmitReg(out, size, {IsByte(size) ? 0xC0u : 0xC1u}, ext, reg, false, IsByte(size));
     Put(out, count);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateShiftRegCl(const ShiftOp op, const Width size,
                                                                  const std::size_t reg)
{
     Bytes out{};
     EmitReg(out, size, {IsByte(size) ? 0xD2u : 0xD3u}, static_cast<std::size_t>(op), reg, false, IsByte(size));
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateShiftMemImm(const ShiftOp op, const Width size,
                                                                   const Memory& mem, const std::uint8_t count)
{
     Bytes out{};
     const std::size_t ext = static_cast<std::size_t>(op);
     if (count == 1)
     {
          EmitMem(out, size, {IsByte(size) ? 0xD0u : 0xD1u}, ext, mem, false);
          return out;
     }
     EmitMem(out, size, {IsByte(size) ? 0xC0u : 0xC1u}, ext, mem, false);
     Put(out, count);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateShiftMemCl(const ShiftOp op, const Width size, const Memory& mem)
{
     Bytes out{};
     EmitMem(out, size, {IsByte(size) ? 0xD2u : 0xD3u}, static_cast<std::size_t>(op), mem, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateDoubleShiftRegRegImm(const bool right, const Width size,
                                                                            const std::size_t destination,
                                                                            const std::size_t source,
                                                                            const std::uint8_t count)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitReg(out, size, {0x0Fu, right ? 0xACu : 0xA4u}, source, destination, false, false);
     Put(out, count);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateDoubleShiftRegRegCl(const bool right, const Width size,
                                                                           const std::size_t destination,
                                                                           const std::size_t source)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitReg(out, size, {0x0Fu, right ? 0xADu : 0xA5u}, source, destination, false, false);
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateBitTestRegReg(const BitOp op, const Width size,
                                                                     const std::size_t base, const std::size_t bit)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitReg(out, size, {0x0Fu, 0xA3u + (8uz * static_cast<std::size_t>(op))}, bit, base, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateBitTestRegImm(const BitOp op, const Width size,
                                                                     const std::size_t base, const std::uint8_t bit)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitReg(out, size, {0x0Fu, 0xBAu}, 4uz + static_cast<std::size_t>(op), base, false, false);
     Put(out, bit);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateBitTestMemReg(const BitOp op, const Width size,
                                                                     const Memory& base, const std::size_t bit)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitMem(out, size, {0x0Fu, 0xA3u + (8uz * static_cast<std::size_t>(op))}, bit, base, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateBitTestMemImm(const BitOp op, const Width size,
                                                                     const Memory& base, const std::uint8_t bit)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitMem(out, size, {0x0Fu, 0xBAu}, 4uz + static_cast<std::size_t>(op), base, false);
     Put(out, bit);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateBitScanRegReg(const bool reverse, const Width size,
                                                                     const std::size_t destination,
                                                                     const std::size_t source)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitReg(out, size, {0x0Fu, reverse ? 0xBDu : 0xBCu}, destination, source, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateBitScanMemReg(const bool reverse, const Width size,
                                                                     const std::size_t destination,
                                                                     const Memory& source)
{
     Bytes out{};
     if (IsByte(size)) return out;
     EmitMem(out, size, {0x0Fu, reverse ? 0xBDu : 0xBCu}, destination, source, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateBswap(const Width size, const std::size_t reg)
{
     Bytes out{};
     if (size != Width::W32 && size != Width::W64) return out;
     PutRex(out, size == Width::W64, false, false, reg >= 8, false);
     Put(out, 0x0Fu);
     Put(out, 0xC8u + static_cast<std::size_t>(reg & 7uz));
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateJmpRel8(const std::int8_t rel)
{
     Bytes out{};
     Put(out, 0xEBu);
     PutImm(out, rel, 1);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateJmpRel32(const std::int32_t rel)
{
     Bytes out{};
     Put(out, 0xE9u);
     PutImm(out, rel, 4);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateJccRel8(const Condition cc, const std::int8_t rel)
{
     Bytes out{};
     Put(out, 0x70uz + static_cast<std::size_t>(cc));
     PutImm(out, rel, 1);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateJccRel32(const Condition cc, const std::int32_t rel)
{
     Bytes out{};
     PutSeq(out, {0x0Fu, 0x80uz + static_cast<std::size_t>(cc)});
     PutImm(out, rel, 4);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCallRel32(const std::int32_t rel)
{
     Bytes out{};
     Put(out, 0xE8u);
     PutImm(out, rel, 4);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateJmpReg(const std::size_t reg)
{
     Bytes out{};
     EmitReg(out, Width::W32, {0xFFu}, 4, reg, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateJmpMem(const Memory& target)
{
     Bytes out{};
     EmitMem(out, Width::W32, {0xFFu}, 4, target, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCallReg(const std::size_t reg)
{
     Bytes out{};
     EmitReg(out, Width::W32, {0xFFu}, 2, reg, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateCallMem(const Memory& target)
{
     Bytes out{};
     EmitMem(out, Width::W32, {0xFFu}, 2, target, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateJrcxz(const std::int8_t rel)
{
     Bytes out{};
     Put(out, 0xE3u);
     PutImm(out, rel, 1);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateLoop(const std::int8_t rel)
{
     Bytes out{};
     Put(out, 0xE2u);
     PutImm(out, rel, 1);
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GeneratePushImm(const std::int32_t imm)
{
     Bytes out{};
     if (FitsI8(imm))
     {
          Put(out, 0x6Au);
          PutImm(out, imm, 1);
     }
     else
     {
          Put(out, 0x68uz);
          PutImm(out, imm, 4);
     }
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GeneratePushMem(const Memory& mem)
{
     Bytes out{};
     EmitMem(out, Width::W32, {0xFFu}, 6, mem, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GeneratePopMem(const Memory& mem)
{
     Bytes out{};
     EmitMem(out, Width::W32, {0x8Fu}, 0, mem, false);
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateStringOp(const StringOp op, const Width size,
                                                                const RepPrefix rep)
{
     Bytes out{};
     if (rep == RepPrefix::Rep) Put(out, 0xF3u);
     else if (rep == RepPrefix::Repne)
          Put(out, 0xF2u);
     if (size == Width::W16) Put(out, 0x66uz);
     PutRex(out, size == Width::W64, false, false, false, false);
     Put(out, static_cast<std::size_t>(op) | (IsByte(size) ? 0uz : 1uz));
     return out;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovToControlReg(const std::size_t controlRegister,
                                                                       const std::size_t source)
{
     Bytes out{};
     EmitReg(out, Width::W32, {0x0Fu, 0x22uz}, controlRegister, source, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovFromControlReg(const std::size_t destination,
                                                                         const std::size_t controlRegister)
{
     Bytes out{};
     EmitReg(out, Width::W32, {0x0Fu, 0x20uz}, controlRegister, destination, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovToDebugReg(const std::size_t debugRegister,
                                                                     const std::size_t source)
{
     Bytes out{};
     EmitReg(out, Width::W32, {0x0Fu, 0x23uz}, debugRegister, source, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovFromDebugReg(const std::size_t destination,
                                                                       const std::size_t debugRegister)
{
     Bytes out{};
     EmitReg(out, Width::W32, {0x0Fu, 0x21uz}, debugRegister, destination, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateSystemTableMem(const SystemTableOp op, const Memory& mem)
{
     Bytes out{};
     EmitMem(out, Width::W32, {0x0Fu, 0x01uz}, static_cast<std::size_t>(op), mem, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateLtr(const std::size_t reg)
{
     Bytes out{};
     EmitReg(out, Width::W32, {0x0Fu, 0x00uz}, 3, reg, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateLldt(const std::size_t reg)
{
     Bytes out{};
     EmitReg(out, Width::W32, {0x0Fuz, 0x00uz}, 2, reg, false, false);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateInImm8(const Width size, const std::uint8_t port)
{
     Bytes out{};
     if (size == Width::W64) return out;
     if (size == Width::W16) Put(out, 0x66uz);
     Put(out, IsByte(size) ? 0xE4uz : 0xE5uz);
     Put(out, port);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateInDx(const Width size)
{
     Bytes out{};
     if (size == Width::W64) return out;
     if (size == Width::W16) Put(out, 0x66uz);
     Put(out, IsByte(size) ? 0xECuz : 0xEDuz);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateOutImm8(const Width size, const std::uint8_t port)
{
     Bytes out{};
     if (size == Width::W64) return out;
     if (size == Width::W16) Put(out, 0x66uz);
     Put(out, IsByte(size) ? 0xE6uz : 0xE7uz);
     Put(out, port);
     return out;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateOutDx(const Width size)
{
     Bytes out{};
     if (size == Width::W64) return out;
     if (size == Width::W16) Put(out, 0x66uz);
     Put(out, IsByte(size) ? 0xEEuz : 0xEFuz);
     return out;
}

// NOLINTEND(readability-identifier-length)
