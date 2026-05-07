#include "Opcodes.h"
#include <RuntimeAssert.h>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <vector>

// NOLINTBEGIN(readability-identifier-length)

inline namespace detail
{
     constexpr auto MakePusher(auto&& vector)
          requires std::ranges::output_range<decltype(vector), std::byte>
     {
          return [&vector](std::byte b) { vector.push_back(b); };
     }
     template <typename T>
     concept IsPushByteFunctor = requires(T t, std::byte b) {
          { t(b) } -> std::same_as<void>;
     };
     static void Emit(IsPushByteFunctor auto&& push, std::integral auto b) { push(static_cast<std::byte>(b)); }
     static void Rex(IsPushByteFunctor auto&& push, bool w, bool r, bool x, bool b)
     {
          std::uint8_t rex = 0x40u;
          if (w) rex |= 0x08u;
          if (r) rex |= 0x04u;
          if (x) rex |= 0x02u;
          if (b) rex |= 0x01u;
          if (rex != 0x40u || w) push(static_cast<std::byte>(rex));
     }
     [[maybe_unused]] static void RexOpt(IsPushByteFunctor auto&& push, bool r, bool x, bool b)
     {
          std::uint8_t rex = 0x40u;
          if (r) rex |= 0x04u;
          if (x) rex |= 0x02u;
          if (b) rex |= 0x01u;
          if (rex != 0x40u) push(static_cast<std::byte>(rex));
     }
     [[maybe_unused]] static void ModRM(IsPushByteFunctor auto&& push, std::uint8_t mod, std::uint8_t reg,
                                        std::uint8_t rm)
     {
          push(static_cast<std::byte>(((mod & 3u) << 6u) | ((reg & 7u) << 3u) | (rm & 7u)));
     }
     static void Sib(IsPushByteFunctor auto&& push, std::uint8_t scale, std::uint8_t index, std::uint8_t base)
     {
          push(static_cast<std::byte>(((scale & 3u) << 6u) | ((index & 7u) << 3u) | (base & 7u)));
     }
     static void Imm16(IsPushByteFunctor auto&& push, std::uint16_t v)
     {
          Emit(push, static_cast<std::uint8_t>(v));
          Emit(push, static_cast<std::uint8_t>(v >> 8u));
     }
     static void Imm32(IsPushByteFunctor auto&& push, std::uint32_t v)
     {
          Emit(push, static_cast<std::uint8_t>(v));
          Emit(push, static_cast<std::uint8_t>(v >> 8u));
          Emit(push, static_cast<std::uint8_t>(v >> 16u));
          Emit(push, static_cast<std::uint8_t>(v >> 24u));
     }
     static void Imm64(IsPushByteFunctor auto&& push, std::uint64_t v)
     {
          Imm32(push, static_cast<std::uint32_t>(v));
          Imm32(push, static_cast<std::uint32_t>(v >> 32u));
     }
     static std::uint8_t DispMod(std::uint8_t base, std::int32_t disp)
     {
          if (disp == 0 && (base & 7u) != 5u) return 0x00u; // [base]
          if (disp >= -128 && disp <= 127) return 0x01u;    // [base+disp8]
          return 0x02u;                                     // [base+disp32]
     }
     static void EmitDisp(IsPushByteFunctor auto&& push, std::uint8_t mod, std::int32_t disp)
     {
          if (mod == 0x01u) Emit(push, static_cast<std::uint8_t>(static_cast<std::int8_t>(disp)));
          else if (mod == 0x02u)
               Imm32(push, static_cast<std::uint32_t>(disp));
     }
     static void ModRMMemory(IsPushByteFunctor auto&& push, std::uint8_t regField, std::uint8_t memReg,
                             std::int32_t disp)
     {
          std::uint8_t mod = DispMod(memReg, disp);

          if ((memReg & 7u) == 4u)
          {
               ModRM(push, mod, regField & 7u, 4u);
               Sib(push, 0u, 4u, memReg & 7u);
               EmitDisp(push, mod, disp);
               return;
          }

          ModRM(push, mod, regField & 7u, memReg & 7u);
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

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg64(std::size_t reg, const std::uint64_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= R8;
     reg &= 7;
     Rex(MakePusher(binary), true, isExtendedRegister, false, false);
     if (imm <= std::numeric_limits<std::uint32_t>::max())
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
     Emit(MakePusher(binary), 0xB8 | reg);
     Imm16(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToReg8(std::size_t reg, const std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(5);
     const bool isExtendedRegister = reg >= R8;
     reg &= 7;
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
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
              reg, offset, static_cast<std::uint32_t>(imm & (std::numeric_limits<std::uint32_t>::max() - 1))));
          binary.append_range(GenerateMovImmToMem32(reg, offset + 4, static_cast<std::uint32_t>(imm >> 32)));
          return binary;
     }
     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), true, isExtendedRegister, false, false);
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
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
     source &= 7;
     destination &= 7;
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x88);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source), static_cast<std::uint8_t>(destination));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     runtime_assert(destinationOffset <= std::numeric_limits<std::uint32_t>::max(),
                    "Displacement out of the 32-bit integer range");
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;
     Rex(MakePusher(binary), true, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     runtime_assert(destinationOffset <= std::numeric_limits<std::uint32_t>::max(),
                    "Displacement out of the 32-bit integer range");
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     runtime_assert(destinationOffset <= std::numeric_limits<std::uint32_t>::max(),
                    "Displacement out of the 32-bit integer range");
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     runtime_assert(destinationOffset <= std::numeric_limits<std::uint32_t>::max(),
                    "Displacement out of the 32-bit integer range");
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destination >= 8;
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x88);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(sourceRegister & 7),
                 static_cast<std::uint8_t>(destination & 7), static_cast<std::int32_t>(destinationOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg64(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     runtime_assert(sourceOffset <= std::numeric_limits<std::uint32_t>::max(),
                    "Displacement out of the 32-bit integer range");
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     Rex(MakePusher(binary), true, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x8b);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg32(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     runtime_assert(sourceOffset <= std::numeric_limits<std::uint32_t>::max(),
                    "Displacement out of the 32-bit integer range");
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x8b);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg16(std::size_t destinationRegister,
                                                                     std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     runtime_assert(sourceOffset <= std::numeric_limits<std::uint32_t>::max(),
                    "Displacement out of the 32-bit integer range");
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x8b);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovMemToReg8(std::size_t destinationRegister,
                                                                    std::size_t sourceOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     runtime_assert(sourceOffset <= std::numeric_limits<std::uint32_t>::max(),
                    "Displacement out of the 32-bit integer range");
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x8a);
     ModRMMemory(MakePusher(binary), static_cast<std::uint8_t>(destinationRegister & 7),
                 static_cast<std::uint8_t>(sourceRegister & 7), static_cast<std::int32_t>(sourceOffset));
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
     Emit(MakePusher(binary), 0xbe);
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
     Emit(MakePusher(binary), 0xbe);
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
     Emit(MakePusher(binary), 0xbe);
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
     Emit(MakePusher(binary), 0xbf);
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
     Emit(MakePusher(binary), 0xbf);
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
     Rex(MakePusher(binary), true, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x63);
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
     Emit(MakePusher(binary), 0xbe);
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
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
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
     Rex(MakePusher(binary), false, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xbe);
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
     Emit(MakePusher(binary), 0xbf);
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
     Emit(MakePusher(binary), 0xbf);
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
     Rex(MakePusher(binary), true, isDestinationExtendedRegister, false, isSourceExtendedRegister);
     Emit(MakePusher(binary), 0x63);
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
     Rex(MakePusher(binary), true, isExtendedRegister, false, false);

     if (reg == 0 && imm >= 0x7f)
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);

     if (reg == 0 && imm >= 0x7f)
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);

     if (reg == 0 && imm >= 0x7f)
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

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     if (imm == 0) return binary;

     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);

     if (reg == 0)
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
     Rex(MakePusher(binary), true, isExtendedRegister, false, false);
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
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
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
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
     Rex(MakePusher(binary), false, source >= 8, false, destination >= 8);
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
     Rex(MakePusher(binary), false, destination >= 8, false, sourceRegister >= 8);
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
     Rex(MakePusher(binary), false, sourceRegister >= 8, false, destination >= 8);
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
     Rex(MakePusher(binary), false, false, false, isExtendedRegister);
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
     Rex(MakePusher(binary), false, source >= 8, false, destination >= 8);
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
     Rex(MakePusher(binary), false, sourceRegister >= 8, false, destination >= 8);
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
     Rex(MakePusher(binary), false, destination >= 8, false, sourceRegister >= 8);
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
     Rex(MakePusher(binary), false, false, false, source >= 8);
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
     Rex(MakePusher(binary), true, reg >= 8, false, false);
     Emit(MakePusher(binary), 0x69);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7));
     Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, reg >= 8, false, false);
     Emit(MakePusher(binary), 0x69);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7));
     Imm32(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, reg >= 8, false, false);
     Emit(MakePusher(binary), 0x69);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(reg & 7), static_cast<std::uint8_t>(reg & 7));
     Imm16(MakePusher(binary), imm);
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, reg >= 8, false, false);
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
     Rex(MakePusher(binary), true, source >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
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
     Rex(MakePusher(binary), false, source >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, source >= 8, false, destination >= 8);
     Emit(MakePusher(binary), 0x0F);
     Emit(MakePusher(binary), 0xAF);
     ModRM(MakePusher(binary), 0b11, static_cast<std::uint8_t>(source & 7), static_cast<std::uint8_t>(destination & 7));
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
     Rex(MakePusher(binary), false, false, false, reg >= 8);
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
     Rex(MakePusher(binary), false, false, false, reg >= 8);
     Emit(MakePusher(binary), 0xF6);
     ModRM(MakePusher(binary), 0b11, 7, static_cast<std::uint8_t>(reg & 7));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg8(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> binary{};
     Rex(MakePusher(binary), false, srcReg >= 8, false, destReg >= 8);
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

// NOLINTEND(readability-identifier-length)
