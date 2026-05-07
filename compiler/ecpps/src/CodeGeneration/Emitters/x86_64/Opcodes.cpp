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

          if ((memReg & 7u) == 4u) // RSP/R12 base → need SIB
          {
               ModRM(push, mod, regField & 7u, 4u); // rm=100 → SIB follows
               Sib(push, 0u, 4u, memReg & 7u);      // index=100 (no index), base=RSP
               EmitDisp(push, mod, disp);
               return;
          }

          ModRM(push, mod, regField & 7u, memReg & 7u);
          EmitDisp(push, mod, disp);
     }

     static std::uint8_t ModFromOffset(std::integral auto offset)
     {
          return static_cast<std::uint8_t>(offset == 0 ? 0b00 : offset <= 0x7f ? 0b01 : 0xb10);
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
     if (offset == 0)
     {
          Emit(MakePusher(binary), reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
          return binary;
     }
     else if (offset <= 0x7f)
     {
          Emit(MakePusher(binary), 0x40 | reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Emit(MakePusher(binary), offset);
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
     }
     else
     {
          Emit(MakePusher(binary), 0x80 | reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(offset));
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(imm));
     }
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
     if (offset == 0)
     {
          Emit(MakePusher(binary), reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Imm32(MakePusher(binary), imm);
          return binary;
     }
     else if (offset <= 0x7f)
     {
          Emit(MakePusher(binary), 0x40 | reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Emit(MakePusher(binary), offset);
          Imm32(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x80 | reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(offset));
          Imm32(MakePusher(binary), imm);
     }
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovImmToMem16(std::size_t reg, const std::size_t offset,
                                                                     const std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     const bool isExtendedRegister = reg >= 8;
     reg &= 7;
     Emit(MakePusher(binary), 0x66); // size override
     Rex(MakePusher(binary), false, isExtendedRegister, false, false);
     Emit(MakePusher(binary), 0xc7);
     if (offset == 0)
     {
          Emit(MakePusher(binary), reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Imm16(MakePusher(binary), imm);
          return binary;
     }
     else if (offset <= 0x7f)
     {
          Emit(MakePusher(binary), 0x40 | reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Emit(MakePusher(binary), offset);
          Imm16(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x80 | reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(offset));
          Imm16(MakePusher(binary), imm);
     }
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
     if (offset == 0)
     {
          Emit(MakePusher(binary), reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Emit(MakePusher(binary), imm);
          return binary;
     }
     else if (offset <= 0x7f)
     {
          Emit(MakePusher(binary), 0x40 | reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Emit(MakePusher(binary), offset);
          Emit(MakePusher(binary), imm);
     }
     else
     {
          Emit(MakePusher(binary), 0x80 | reg);
          if (reg == 4) Emit(MakePusher(binary), 0x24);
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(offset));
          Emit(MakePusher(binary), imm);
     }
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
     Emit(MakePusher(binary), 0x66); // size override
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
     sourceRegister &= 7;
     destination &= 7;
     Rex(MakePusher(binary), true, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);
     ModRM(MakePusher(binary), ModFromOffset(destinationOffset), static_cast<std::uint8_t>(sourceRegister),
           static_cast<std::uint8_t>(destination));
     if (destination == Rsp) Emit(MakePusher(binary), 0x24);
     if (destinationOffset == 0) return binary;
     else if (destinationOffset <= 0x7f)
          Emit(MakePusher(binary), destinationOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(destinationOffset));
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
     sourceRegister &= 7;
     destination &= 7;
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);
     ModRM(MakePusher(binary), ModFromOffset(destinationOffset), static_cast<std::uint8_t>(sourceRegister),
           static_cast<std::uint8_t>(destination));
     if (destination == Rsp) Emit(MakePusher(binary), 0x24);
     if (destinationOffset == 0) return binary;
     else if (destinationOffset <= 0x7f)
          Emit(MakePusher(binary), destinationOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(destinationOffset));
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
     sourceRegister &= 7;
     destination &= 7;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x89);
     ModRM(MakePusher(binary), ModFromOffset(destinationOffset), static_cast<std::uint8_t>(sourceRegister),
           static_cast<std::uint8_t>(destination));
     if (destination == Rsp) Emit(MakePusher(binary), 0x24);
     if (destinationOffset == 0) return binary;
     else if (destinationOffset <= 0x7f)
          Emit(MakePusher(binary), destinationOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(destinationOffset));
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
     sourceRegister &= 7;
     destination &= 7;
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x88);
     ModRM(MakePusher(binary), ModFromOffset(destinationOffset), static_cast<std::uint8_t>(sourceRegister),
           static_cast<std::uint8_t>(destination));
     if (destination == Rsp) Emit(MakePusher(binary), 0x24);
     if (destinationOffset == 0) return binary;
     else if (destinationOffset <= 0x7f)
          Emit(MakePusher(binary), destinationOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(destinationOffset));
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
     sourceRegister &= 7;
     destinationRegister &= 7;
     Rex(MakePusher(binary), true, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x8b);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(sourceRegister),
           static_cast<std::uint8_t>(destinationRegister));
     if (destinationRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
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
     sourceRegister &= 7;
     destinationRegister &= 7;
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x8b);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(destinationRegister),
           static_cast<std::uint8_t>(sourceRegister));
     if (sourceRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
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
     sourceRegister &= 7;
     destinationRegister &= 7;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x8b);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(sourceRegister),
           static_cast<std::uint8_t>(destinationRegister));
     if (destinationRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
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
     sourceRegister &= 7;
     destinationRegister &= 7;
     Rex(MakePusher(binary), true, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x8a);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(sourceRegister),
           static_cast<std::uint8_t>(destinationRegister));
     if (destinationRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem8ToReg64(std::size_t destinationRegister,
                                                                                std::size_t sourceOffset,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     destinationRegister &= 7;
     sourceRegister &= 7;
     Rex(MakePusher(binary), true, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xbe);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(destinationRegister),
           static_cast<std::uint8_t>(sourceRegister));
     if (sourceRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem8ToReg32(std::size_t destinationRegister,
                                                                                std::size_t sourceOffset,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     destinationRegister &= 7;
     sourceRegister &= 7;
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xbe);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(destinationRegister),
           static_cast<std::uint8_t>(sourceRegister));
     if (sourceRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem8ToReg16(std::size_t destinationRegister,
                                                                                std::size_t sourceOffset,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     destinationRegister &= 7;
     sourceRegister &= 7;
     Emit(MakePusher(binary), 0x66);
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xbe);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(destinationRegister),
           static_cast<std::uint8_t>(sourceRegister));
     if (sourceRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem16ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceOffset,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     destinationRegister &= 7;
     sourceRegister &= 7;
     Rex(MakePusher(binary), true, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xbf);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(destinationRegister),
           static_cast<std::uint8_t>(sourceRegister));
     if (sourceRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem16ToReg32(std::size_t destinationRegister,
                                                                                 std::size_t sourceOffset,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     destinationRegister &= 7;
     sourceRegister &= 7;
     Rex(MakePusher(binary), false, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x0f);
     Emit(MakePusher(binary), 0xbf);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(destinationRegister),
           static_cast<std::uint8_t>(sourceRegister));
     if (sourceRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendMem32ToReg64(std::size_t destinationRegister,
                                                                                 std::size_t sourceOffset,
                                                                                 std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const bool isDestinationExtendedRegister = destinationRegister >= 8;
     const bool isSourceExtendedRegister = sourceRegister >= 8;
     destinationRegister &= 7;
     sourceRegister &= 7;
     Rex(MakePusher(binary), true, isSourceExtendedRegister, false, isDestinationExtendedRegister);
     Emit(MakePusher(binary), 0x63);
     ModRM(MakePusher(binary), ModFromOffset(sourceOffset), static_cast<std::uint8_t>(destinationRegister),
           static_cast<std::uint8_t>(sourceRegister));
     if (sourceRegister == Rsp) Emit(MakePusher(binary), 0x24);
     if (sourceOffset == 0) return binary;
     else if (sourceOffset <= 0x7f)
          Emit(MakePusher(binary), sourceOffset);
     else
          Imm32(MakePusher(binary), static_cast<std::uint32_t>(sourceOffset));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateMovZeroExtendReg8ToReg64(std::size_t destinationRegister,
                                                                                std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(4);
     // MOVZX r64, r/m8
     std::uint8_t rex = 0x48;                   // REX.W
     if (destinationRegister >= 8) rex |= 0x04; // REX.R
     if (sourceRegister >= 8) rex |= 0x01;      // REX.B
     binary.push_back(static_cast<std::byte>(rex));
     binary.push_back(std::byte{0x0F});
     binary.push_back(std::byte{0xB6}); // Correct opcode
     binary.push_back(static_cast<std::byte>(0xC0 | ((destinationRegister & 7) << 3) | (sourceRegister & 7)));
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
     binary.push_back(static_cast<std::byte>(0xc0 | sourceRegister | (destinationRegister << 3)));
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
     // 32-bit MOV to 32-bit register automatically zero-extends upper 32 bits
     return GenerateMovRegToReg32(destinationRegister, sourceRegister);
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
     std::uint32_t reg = destinationRegister & 7;
     std::size_t modrm = static_cast<std::uint8_t>((static_cast<std::size_t>(mod) << 6) | (reg << 3) | rm);
     binary.push_back(static_cast<std::byte>(modrm));

     // SIB byte: scale=00, index=100 (none), base=100 (RSP)
     std::uint8_t sib = static_cast<std::uint8_t>((0b00 << 6) | (0b100 << 3) | 0b100);
     binary.push_back(static_cast<std::byte>(sib));

     // 4-byte little-endian displacement
     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

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

     std::size_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | 0b100;
     binary.push_back(static_cast<std::byte>(modrm));
     binary.push_back(static_cast<std::byte>(0x24));

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

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

     std::size_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | 0b100;
     binary.push_back(static_cast<std::byte>(modrm));
     binary.push_back(static_cast<std::byte>(0x24));

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

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

     std::size_t modrm = 0b10000000 | ((destinationRegister & 7) << 3) | 0b100;
     binary.push_back(static_cast<std::byte>(modrm));
     binary.push_back(static_cast<std::byte>(0x24));

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

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

               for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
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

          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
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

               for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
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

          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
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

               for (std::size_t i = 0; i < 2; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
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

          for (std::size_t i = 0; i < 2; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
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

     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | rm));

     if (needsSib)
     {
          // scale=0, index=none(100), base=reg&7
          binary.push_back(static_cast<std::byte>((0 << 6) | (4 << 3) | (reg & 7)));
     }

     if (disp8) { binary.push_back(static_cast<std::byte>(offset)); }
     else
     {
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

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

     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | rm));

     if (needsSib) { binary.push_back(static_cast<std::byte>((0 << 6) | (4 << 3) | (reg & 7))); }

     if (disp8) { binary.push_back(static_cast<std::byte>(offset)); }
     else
     {
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

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

     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | rm));

     if (needsSib) { binary.push_back(static_cast<std::byte>((0 << 6) | (4 << 3) | (reg & 7))); }

     if (disp8) { binary.push_back(static_cast<std::byte>(offset)); }
     else
     {
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
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

     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | rm));

     if (needsSib) { binary.push_back(static_cast<std::byte>((0 << 6) | (4 << 3) | (reg & 7))); }

     if (disp8) { binary.push_back(static_cast<std::byte>(offset)); }
     else
     {
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     binary.push_back(static_cast<std::byte>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     bool rexR = source >= 8;
     bool rexB = destination >= 8;
     binary.push_back(
         static_cast<std::byte>(0x48 | (static_cast<std::uint32_t>(rexR) << 2) | static_cast<std::uint32_t>(rexB)));
     binary.push_back(static_cast<std::byte>(0x01));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     if (destination >= 8 || source >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(source >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x01));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));
     if (destination >= 8 || source >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(source >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x01));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     bool rex = destination >= 8 || source >= 8 || (destination & 7) >= 4 || (source & 7) >= 4;
     if (rex)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(source >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

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
     binary.push_back(
         static_cast<std::byte>(0x48 | (static_cast<std::uint32_t>(rexR) << 2) | static_cast<std::uint32_t>(rexB)));
     binary.push_back(static_cast<std::byte>(0x03)); // ADD r64, r/m64

     const bool needsSib = (sourceRegister & 7) == 4;  // RSP/R12
     const bool forceDisp = (sourceRegister & 7) == 5; // RBP/R13

     std::uint32_t mod{};
     if (sourceOffset == 0 && !forceDisp) mod = 0b00;
     else if (sourceOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (sourceRegister & 7);
     const auto modrm = static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((destination & 7) << 3) | rm);
     binary.push_back(modrm);

     if (needsSib) binary.push_back(std::byte{0x24}); // SIB: scale=0, index=none, base=rsp

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
     {
          const auto offset32 = static_cast<std::uint32_t>(sourceOffset);
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset32 >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg32(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(destination >= 8) << 2) |
                                                  static_cast<std::uint32_t>(sourceRegister >= 8)));

     binary.push_back(static_cast<std::byte>(0x03)); // ADD r32, r/m32

     const bool needsSib = (sourceRegister & 7) == 4;
     const bool forceDisp = (sourceRegister & 7) == 5;

     std::uint32_t mod{};
     if (sourceOffset == 0 && !forceDisp) mod = 0b00;
     else if (sourceOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (sourceRegister & 7);
     const auto modrm = static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((destination & 7) << 3) | rm);
     binary.push_back(modrm);

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
     {
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg16(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     binary.push_back(static_cast<std::byte>(0x66));
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(destination >= 8) << 2) |
                                                  static_cast<std::uint32_t>(sourceRegister >= 8)));

     binary.push_back(static_cast<std::byte>(0x03)); // ADD r16, r/m16

     const bool needsSib = (sourceRegister & 7) == 4;
     const bool forceDisp = (sourceRegister & 7) == 5;

     std::uint32_t mod{};
     if (sourceOffset == 0 && !forceDisp) mod = 0b00;
     else if (sourceOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (sourceRegister & 7);
     const auto modrm = static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((destination & 7) << 3) | rm);
     binary.push_back(modrm);

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
     {
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddMemToReg8(std::size_t destination, std::size_t sourceOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     const bool rex = destination >= 8 || sourceRegister >= 8 || (destination & 7) >= 4 || (sourceRegister & 7) >= 4;
     if (rex)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(destination >= 8) << 2) |
                                                  static_cast<std::uint32_t>(sourceRegister >= 8)));

     binary.push_back(static_cast<std::byte>(0x02)); // ADD r8, r/m8

     const bool needsSib = (sourceRegister & 7) == 4;
     const bool forceDisp = (sourceRegister & 7) == 5;

     std::uint32_t mod{};
     if (sourceOffset == 0 && !forceDisp) mod = 0b00;
     else if (sourceOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (sourceRegister & 7);
     const auto modrm = static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((destination & 7) << 3) | rm);
     binary.push_back(modrm);

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
     {
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                             static_cast<std::uint32_t>(destination >= 8)));
     binary.push_back(static_cast<std::byte>(0x01)); // ADD r/m64, r64

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x01)); // ADD r/m32, r32

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(
         static_cast<std::uint32_t>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm)));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66));
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x01)); // ADD r/m16, r16

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateAddRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(7);
     if (destination >= 8 || sourceRegister >= 8 || (sourceRegister & 7) >= 4)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x00)); // ADD r/m8, r8

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg64(std::size_t reg, std::uint64_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(13);

     const bool rexW = true;
     const bool rexB = (reg & 0b1000) != 0;

     auto rex = static_cast<std::byte>(0x40 | (rexW ? 0x08 : 0x00) | (rexB ? 0x01 : 0x00));
     binary.push_back(rex);

     if (imm <= 0x7FFFFFFF)
     {
          binary.push_back(static_cast<std::byte>(0x81));
          binary.push_back(static_cast<std::byte>(0xE8 | (reg & 0x07)));
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x48));
          binary.push_back(static_cast<std::byte>(0xB8 + (reg & 0x07)));
          for (std::size_t i = 0; i < 8; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
          binary.push_back(static_cast<std::byte>(0x2B)); // SUB reg, rax
          binary.push_back(static_cast<std::byte>(0xC0 | ((0 & 7) << 3) | (reg & 7)));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(7);

     if (reg >= 8)
     {
          binary.push_back(static_cast<std::byte>(0x41));
          reg -= 8;
     }

     binary.push_back(static_cast<std::byte>(0x81));
     binary.push_back(static_cast<std::byte>(0xE8 | reg));
     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(6);
     binary.push_back(static_cast<std::byte>(0x66)); // Operand size override

     if (reg >= 8)
     {
          binary.push_back(static_cast<std::byte>(0x41));
          reg -= 8;
     }

     binary.push_back(static_cast<std::byte>(0x81));
     binary.push_back(static_cast<std::byte>(0xE8 | reg));
     for (std::size_t i = 0; i < 2; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToReg8(std::size_t reg, std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(4);

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
     binary.reserve(11);

     const bool rexB = reg >= 8;
     if (rexB) binary.push_back(static_cast<std::byte>(0x48 | (rexB ? 0x01 : 0x00)));

     binary.push_back(static_cast<std::byte>(0x81)); // /5 for SUB

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 0x07) << 0))); // mod = 01
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 0x07) << 0))); // mod = 10
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem32(std::size_t reg, std::size_t offset,
                                                                     std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(16);

     const auto needsRexB = reg >= 8;
     const auto baseLow = static_cast<std::uint8_t>(reg & 0x7);

     if (needsRexB)
     {
          const std::uint8_t rex = 0x40 | 0x01; // REX.B
          binary.push_back(static_cast<std::byte>(rex));
     }

     binary.push_back(std::byte{0x81}); // /5

     bool needsSib = (baseLow == 4); // rsp/r12

     std::uint32_t mod{};
     if (offset == 0 && baseLow != 5) // rbp/r13 cannot use mod=00
          mod = 0b00;

     if (offset <= 0x7F) mod = 0b01;
     else
          mod = 0b10;

     if (baseLow == 5 && mod == 0b00)
     {
          mod = 0b01;
          offset = 0;
     }

     const std::size_t modrm =
         static_cast<std::uint8_t>(static_cast<std::uint32_t>(mod << 6) | (5 << 3) | (needsSib ? 4 : baseLow));

     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSib)
     {
          const std::uint8_t sib = static_cast<std::uint8_t>((0 << 6) | (4 << 3) | baseLow);
          binary.push_back(static_cast<std::byte>(sib));
     }

     if (mod == 0b01) { binary.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(offset))); }
     else if (mod == 0b10)
     {
          for (std::uint32_t i : std::views::iota(0u, 4uz))
          {
               binary.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(offset >> (i * 8))));
          }
     }

     for (std::uint32_t i : std::views::iota(0u, 4uz))
     {
          binary.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(imm >> (i * 8))));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem16(std::size_t reg, std::size_t offset,
                                                                     std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(16);

     binary.push_back(std::byte{0x66}); // operand-size override

     const auto needsRexB = reg >= 8;
     const auto baseLow = static_cast<std::uint8_t>(reg & 0x7);

     if (needsRexB)
     {
          binary.push_back(std::byte{0x41}); // REX.B
     }

     binary.push_back(std::byte{0x81}); // /5

     const bool needsSib = (baseLow == 4);

     std::uint32_t mod{};
     if (offset == 0 && baseLow != 5) mod = 0b00;
     if (offset <= 0x7F) mod = 0b01;
     else
          mod = 0b10;

     if (baseLow == 5 && mod == 0b00)
     {
          mod = 0b01;
          offset = 0;
     }

     const std::size_t modrm =
         static_cast<std::uint8_t>(static_cast<std::uint32_t>(mod << 6) | (5 << 3) | (needsSib ? 4 : baseLow));

     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSib)
     {
          const std::uint8_t sib = static_cast<std::uint8_t>((0 << 6) | (4 << 3) | baseLow);
          binary.push_back(static_cast<std::byte>(sib));
     }

     if (mod == 0b01) { binary.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(offset))); }
     else if (mod == 0b10)
     {
          for (std::uint32_t i : std::views::iota(0u, 4uz))
          {
               binary.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(offset >> (i * 8))));
          }
     }

     for (std::uint32_t i : std::views::iota(0u, 2uz))
     {
          binary.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(imm >> (i * 8))));
     }

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubImmToMem8(std::size_t reg, std::size_t offset,
                                                                    std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(16);

     const auto needsRexB = reg >= 8;
     const auto baseLow = static_cast<std::uint8_t>(reg & 0x7);

     if (needsRexB)
     {
          binary.push_back(std::byte{0x41}); // REX.B
     }

     binary.push_back(std::byte{0x80}); // /5 (imm8)

     const bool needsSib = (baseLow == 4);

     std::uint32_t mod{};
     if (offset == 0 && baseLow != 5) mod = 0b00;
     if (offset <= 0x7F) mod = 0b01;
     else
          mod = 0b10;

     if (baseLow == 5 && mod == 0b00)
     {
          mod = 0b01;
          offset = 0;
     }

     const std::size_t modrm =
         static_cast<std::uint8_t>(static_cast<std::uint32_t>(mod << 6) | (5 << 3) | (needsSib ? 4 : baseLow));

     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSib)
     {
          const std::uint8_t sib = static_cast<std::uint8_t>((0 << 6) | (4 << 3) | baseLow);
          binary.push_back(static_cast<std::byte>(sib));
     }

     if (mod == 0b01) { binary.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(offset))); }
     else if (mod == 0b10)
     {
          for (std::uint32_t i : std::views::iota(0u, 4u))
          {
               binary.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(offset >> (i * 8))));
          }
     }

     binary.push_back(static_cast<std::byte>(imm));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);
     const bool rexR = source >= 8;
     const bool rexB = destination >= 8;

     binary.push_back(
         static_cast<std::byte>(0x48 | (static_cast<std::uint32_t>(rexR) << 2) | static_cast<std::uint32_t>(rexB)));
     binary.push_back(static_cast<std::byte>(0x29));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);

     if (source >= 8 || destination >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(source >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.reserve(4);
     binary.push_back(static_cast<std::byte>(0x66));

     if (source >= 8 || destination >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(source >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);

     const bool rex = (destination >= 8) || (source >= 8) || (destination & 7) >= 4 || (source & 7) >= 4;
     if (rex)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(source >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x28));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem64(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(8);
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                             static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29)); // SUB r/m64, r64

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem32(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(8);
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29)); // SUB r/m32, r32

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem16(std::size_t destination,
                                                                     std::size_t destinationOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(9);
     binary.push_back(static_cast<std::byte>(0x66));
     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x29)); // SUB r/m16, r16

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubRegToMem8(std::size_t destination,
                                                                    std::size_t destinationOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(8);
     if (destination >= 8 || sourceRegister >= 8 || (sourceRegister & 7) >= 4)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x28)); // SUB r/m8, r8

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg64(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(8);

     auto rex = static_cast<std::byte>(0x48);                        // REX.W = 1
     if (destination >= 8) rex |= static_cast<std::byte>(0x01 << 2); // REX.R
     if (sourceRegister >= 8) rex |= static_cast<std::byte>(0x01);   // REX.B
     binary.push_back(rex);

     binary.push_back(static_cast<std::byte>(0x2B)); // opcode SUB reg, r/m64

     // Mod calculation
     std::uint8_t mod = 0;
     if (sourceOffset == 0 && sourceRegister != Rbp && sourceRegister != R13) mod = 0x00;
     else if (sourceOffset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<std::uint8_t>(sourceRegister % 8);
     bool needsSIB = (rm == 4); // rsp/r12 needs SIB
     std::size_t modrm = static_cast<std::uint32_t>(mod << 6) | ((destination % 8) << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSIB) binary.push_back(static_cast<std::byte>(0x24)); // SIB byte: scale=0, index=none, base=rsp/r12

     // displacement
     if (mod == 0x01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg32(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(7);

     auto rex = static_cast<std::byte>(0x40); // REX prefix without W
     if (destination >= 8) rex |= static_cast<std::byte>(0x01 << 2);
     if (sourceRegister >= 8) rex |= static_cast<std::byte>(0x01);
     if (rex != static_cast<std::byte>(0x40)) binary.push_back(rex);

     binary.push_back(static_cast<std::byte>(0x2B)); // SUB reg, r/m32

     std::uint8_t mod = 0;
     if (sourceOffset == 0 && sourceRegister != Rbp && sourceRegister != R13) mod = 0x00;
     else if (sourceOffset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<std::uint8_t>(sourceRegister % 8);
     bool needsSIB = (rm == 4);
     std::size_t modrm = static_cast<std::uint32_t>(mod << 6) | ((destination % 8) << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSIB) binary.push_back(static_cast<std::byte>(0x24));

     if (mod == 0x01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg16(std::size_t destination, std::size_t sourceOffset,
                                                                     std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(10);
     binary.push_back(static_cast<std::byte>(0x66)); // operand size prefix

     auto rex = static_cast<std::byte>(0x40); // REX prefix without W
     if (destination >= 8) rex |= static_cast<std::byte>(0x01 << 2);
     if (sourceRegister >= 8) rex |= static_cast<std::byte>(0x01);
     if (rex != static_cast<std::byte>(0x40)) binary.push_back(rex);

     binary.push_back(static_cast<std::byte>(0x2B)); // SUB reg, r/m16

     std::uint8_t mod = 0;
     if (sourceOffset == 0 && sourceRegister != Rbp && sourceRegister != R13) mod = 0x00;
     else if (sourceOffset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<std::uint8_t>(sourceRegister % 8);
     bool needsSIB = (rm == 4);
     std::size_t modrm = static_cast<std::uint32_t>(mod << 6) | ((destination % 8) << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSIB) binary.push_back(static_cast<std::byte>(0x24));

     if (mod == 0x01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSubMemToReg8(std::size_t destination, std::size_t sourceOffset,
                                                                    std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(8);

     auto rex = static_cast<std::byte>(0x40); // REX prefix
     if (destination >= 8) rex |= static_cast<std::byte>(0x01 << 2);
     if (sourceRegister >= 8) rex |= static_cast<std::byte>(0x01);
     if (rex != static_cast<std::byte>(0x40)) binary.push_back(rex);

     binary.push_back(static_cast<std::byte>(0x2A)); // SUB reg, r/m8

     std::uint8_t mod = 0;
     if (sourceOffset == 0 && sourceRegister != Rbp && sourceRegister != R13) mod = 0x00;
     else if (sourceOffset < 0x80)
          mod = 0x01;
     else
          mod = 0x02;

     auto rm = static_cast<std::uint8_t>(sourceRegister % 8);
     bool needsSIB = (rm == 4);
     std::size_t modrm = static_cast<std::uint32_t>(mod << 6) | ((destination % 8) << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSIB) binary.push_back(static_cast<std::byte>(0x24));

     if (mod == 0x01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0x02 || (mod == 0x00 && (rm == 5 || rm == 13)))
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg64([[maybe_unused]] std::size_t destination,
                                                                             std::size_t source)
{
     // MUL r/m64: result in RDX:RAX, operand in source register
     std::vector<std::byte> binary{};
     std::uint8_t rex = 0x48;      // REX.W
     if (source >= 8) rex |= 0x01; // REX.B
     binary.push_back(static_cast<std::byte>(rex));
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xE0 | (source & 7))); // ModRM: mod=11, reg=4, rm=source
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg32([[maybe_unused]] std::size_t destination,
                                                                             std::size_t source)
{
     // MUL r/m32: result in EDX:EAX
     std::vector<std::byte> binary{};
     if (source >= 8)
     {
          binary.push_back(static_cast<std::byte>(0x41)); // REX.B
     }
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xE0 | (source & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg16([[maybe_unused]] std::size_t destination,
                                                                             std::size_t source)
{
     // MUL r/m16: result in DX:AX
     std::vector<std::byte> binary{};
     binary.push_back(static_cast<std::byte>(0x66)); // Operand size prefix
     if (source >= 8) { binary.push_back(static_cast<std::byte>(0x41)); }
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xE0 | (source & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToReg8([[maybe_unused]] std::size_t destination,
                                                                            std::size_t source)
{
     // MUL r/m8: result in AX
     std::vector<std::byte> binary{};
     if (source >= 8 || (source & 7) >= 4)
     {
          std::uint8_t rex = 0x40;
          if (source >= 8) rex |= 0x01;
          binary.push_back(static_cast<std::byte>(rex));
     }
     binary.push_back(static_cast<std::byte>(0xF6));
     binary.push_back(static_cast<std::byte>(0xE0 | (source & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem64(
    std::size_t destination, std::size_t destinationOffset, [[maybe_unused]] std::size_t sourceRegister)
{
     // MUL [destination + offset]: multiplies RAX by memory, result in RDX:RAX
     std::vector<std::byte> binary{};

     std::uint8_t rex = 0x48;           // REX.W
     if (destination >= 8) rex |= 0x01; // REX.B
     binary.push_back(static_cast<std::byte>(rex));

     binary.push_back(static_cast<std::byte>(0xF7)); // MUL opcode

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | (4 << 3) | rm)); // reg=4 for MUL

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem32(
    std::size_t destination, std::size_t destinationOffset, [[maybe_unused]] std::size_t sourceRegister)
{
     // MUL [destination + offset]: multiplies EAX by memory, result in EDX:EAX
     std::vector<std::byte> binary{};

     if (destination >= 8) binary.push_back(static_cast<std::byte>(0x41)); // REX.B

     binary.push_back(static_cast<std::byte>(0xF7)); // MUL opcode

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | (4 << 3) | rm)); // reg=4 for MUL

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem16(
    std::size_t destination, std::size_t destinationOffset, [[maybe_unused]] std::size_t sourceRegister)
{
     // MUL [destination + offset]: multiplies AX by memory, result in DX:AX
     std::vector<std::byte> binary{};

     binary.push_back(static_cast<std::byte>(0x66)); // Operand size override

     if (destination >= 8) binary.push_back(static_cast<std::byte>(0x41)); // REX.B

     binary.push_back(static_cast<std::byte>(0xF7)); // MUL opcode

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | (4 << 3) | rm)); // reg=4 for MUL

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedMulRegToMem8(std::size_t destination,
                                                                            std::size_t destinationOffset,
                                                                            [[maybe_unused]] std::size_t sourceRegister)
{
     // MUL [destination + offset]: multiplies AL by memory, result in AX
     std::vector<std::byte> binary{};

     if (destination >= 8) binary.push_back(static_cast<std::byte>(0x41)); // REX.B

     binary.push_back(static_cast<std::byte>(0xF6)); // MUL opcode (8-bit)

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | (4 << 3) | rm)); // reg=4 for MUL

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg64(std::size_t reg, std::uint64_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(7);
     bool rexR = reg >= 8;
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<std::uint32_t>(rexR) << 2)));
     binary.push_back(static_cast<std::byte>(0x69));
     binary.push_back(static_cast<std::byte>(0xC0 | ((reg & 7) << 3) | (reg & 7)));

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg32(std::size_t reg, std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(7);
     if (reg >= 8) binary.push_back(static_cast<std::byte>(0x41));
     binary.push_back(static_cast<std::byte>(0x69));
     binary.push_back(static_cast<std::byte>(0xC0 | ((reg & 7) << 3) | (reg & 7)));

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToReg16(std::size_t reg, std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(7);
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
     binary.reserve(4);
     bool rex = reg >= 8 || (reg & 7) >= 4;
     if (rex) binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(reg >= 8) << 0)));
     binary.push_back(static_cast<std::byte>(0x6B));
     binary.push_back(static_cast<std::byte>(0xC0 | ((reg & 7) << 3) | (reg & 7)));
     binary.push_back(static_cast<std::byte>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem64(std::size_t reg, std::size_t offset,
                                                                           std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(11);
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
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem32(std::size_t reg, std::size_t offset,
                                                                           std::uint32_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(12);
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
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((imm >> (i * 8)) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem16(std::size_t reg, std::size_t offset,
                                                                           std::uint16_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(10);
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
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     binary.push_back(static_cast<std::byte>(imm & 0xFF));
     binary.push_back(static_cast<std::byte>((imm >> 8) & 0xFF));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulImmToMem8(std::size_t reg, std::size_t offset,
                                                                          std::uint8_t imm)
{
     std::vector<std::byte> binary{};
     binary.reserve(8);
     bool rex = reg >= 8 || (reg & 7) >= 4;
     if (rex) binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(reg >= 8) << 0)));
     binary.push_back(static_cast<std::byte>(0x6B));

     if (offset <= 0x7F)
     {
          binary.push_back(static_cast<std::byte>(0x45 | ((reg & 7) << 3)));
          binary.push_back(static_cast<std::byte>(offset));
     }
     else
     {
          binary.push_back(static_cast<std::byte>(0x85 | ((reg & 7) << 3)));
          for (std::size_t i = 0; i < 4; i++) binary.push_back(static_cast<std::byte>((offset >> (i * 8)) & 0xFF));
     }

     binary.push_back(static_cast<std::byte>(imm));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg64(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.reserve(4);
     bool rexR = source >= 8;
     bool rexB = destination >= 8;
     binary.push_back(
         static_cast<std::byte>(0x48 | (static_cast<std::uint32_t>(rexR) << 2) | static_cast<std::uint32_t>(rexB)));
     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg32(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     if (destination >= 8 || source >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(destination >= 8) << 2) |
                                                  static_cast<std::uint32_t>(source >= 8)));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));
     binary.push_back(static_cast<std::byte>(0xC0 | ((destination & 7) << 3) | (source & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg16(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.reserve(4);
     binary.push_back(static_cast<std::byte>(0x66));
     if (source >= 8 || destination >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(source >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));
     binary.push_back(static_cast<std::byte>(0xC0 | ((source & 7) << 3) | (destination & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToReg8(std::size_t destination, std::size_t source)
{
     std::vector<std::byte> binary{};
     binary.reserve(4);
     bool rex = destination >= 8 || source >= 8 || (destination & 7) >= 4 || (source & 7) >= 4;
     if (rex)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(source >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

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
     binary.reserve(9);
     binary.push_back(static_cast<std::byte>(0x48 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                             static_cast<std::uint32_t>(destination >= 8)));
     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF)); // IMUL r64, r/m64

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem32(std::size_t destination,
                                                                           std::size_t destinationOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(8);

     if (destination >= 8 || sourceRegister >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF)); // IMUL r32, r/m32

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulRegToMem16(std::size_t destination,
                                                                           std::size_t destinationOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};
     binary.reserve(10);

     binary.push_back(static_cast<std::byte>(0x66));
     if (sourceRegister >= 8 || destination >= 8)
          binary.push_back(static_cast<std::byte>(0x40 | (static_cast<std::uint32_t>(sourceRegister >= 8) << 2) |
                                                  static_cast<std::uint32_t>(destination >= 8)));

     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF)); // IMUL r16, r/m16

     const bool needsSib = (destination & 7) == 4;
     const bool forceDisp = (destination & 7) == 5;

     std::uint32_t mod{};
     if (destinationOffset == 0 && !forceDisp) mod = 0b00;
     else if (destinationOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (destination & 7);
     binary.push_back(static_cast<std::byte>(static_cast<std::uint32_t>(mod << 6) | ((sourceRegister & 7) << 3) | rm));

     if (needsSib) binary.push_back(std::byte{0x24});

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(destinationOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((destinationOffset >> (i * 8)) & 0xFF));

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

     // REX prefix
     std::uint8_t rex = 0x48;              // REX.W
     if (destination >= 8) rex |= 0x04;    // REX.R
     if (sourceRegister >= 8) rex |= 0x01; // REX.B
     binary.push_back(static_cast<std::byte>(rex));

     // IMUL r64, r/m64: opcode 0F AF /r
     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));

     // ModR/M and SIB
     const bool needsSib = (sourceRegister & 7) == 4;  // RSP/R12
     const bool forceDisp = (sourceRegister & 7) == 5; // RBP/R13

     std::uint32_t mod{};
     if (sourceOffset == 0 && !forceDisp) mod = 0b00;
     else if (sourceOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (sourceRegister & 7);
     const std::size_t modrm = static_cast<std::uint32_t>(mod << 6) | ((destination & 7) << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSib) binary.push_back(static_cast<std::byte>(0x24)); // SIB: scale=0, index=none, base=rsp

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulMemToReg32(std::size_t destination,
                                                                           std::size_t sourceOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> result{};

     const auto base = static_cast<std::uint8_t>(sourceRegister);

     const std::uint8_t rex = 0x40 | ((destination & 0x8) ? 0x04 : 0x00) | // REX.R
                              ((base & 0x8) ? 0x01 : 0x00);                // REX.B

     if (rex != 0x40) result.push_back(static_cast<std::byte>(rex));

     // opcode
     result.push_back(std::byte{0x0F});
     result.push_back(std::byte{0xAF});

     const bool needsSib = (base & 0x7) == 4;
     const bool noDisp = sourceOffset == 0 && (base & 0x7) != 5;
     const bool disp8 = !noDisp && sourceOffset <= 0x7F;

     std::uint32_t mod{};
     if (noDisp) mod = 0b00;
     else if (disp8)
          mod = 0b01;
     else
          mod = 0b10;

     const std::size_t modrm = static_cast<std::uint8_t>(
         static_cast<std::uint32_t>(mod << 6) | ((destination & 0x7) << 3) | (needsSib ? 0b100 : (base & 0x7)));

     result.push_back(static_cast<std::byte>(modrm));

     if (needsSib)
     {
          // scale=0, index=none(100), base=base
          const std::uint8_t sib = static_cast<std::uint8_t>((0b00 << 6) | (0b100 << 3) | (base & 0x7));

          result.push_back(static_cast<std::byte>(sib));
     }

     if (!noDisp)
     {
          if (disp8) { result.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(sourceOffset))); }
          else
          {
               result.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
               result.push_back(static_cast<std::byte>((sourceOffset >> 8) & 0xFF));
               result.push_back(static_cast<std::byte>((sourceOffset >> 16) & 0xFF));
               result.push_back(static_cast<std::byte>((sourceOffset >> 24) & 0xFF));
          }
     }

     return result;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulMemToReg16(std::size_t destination,
                                                                           std::size_t sourceOffset,
                                                                           std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     // Operand-size prefix
     binary.push_back(static_cast<std::byte>(0x66));

     // REX if needed
     if (destination >= 8 || sourceRegister >= 8)
     {
          std::uint8_t rex = 0x40;
          if (destination >= 8) rex |= 0x04;
          if (sourceRegister >= 8) rex |= 0x01;
          binary.push_back(static_cast<std::byte>(rex));
     }

     // IMUL r16, r/m16: opcode 0F AF /r
     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));

     const bool needsSib = (sourceRegister & 7) == 4;
     const bool forceDisp = (sourceRegister & 7) == 5;

     std::uint32_t mod{};
     if (sourceOffset == 0 && !forceDisp) mod = 0b00;
     else if (sourceOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (sourceRegister & 7);
     const std::size_t modrm = static_cast<std::uint32_t>(mod << 6) | ((destination & 7) << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSib) binary.push_back(static_cast<std::byte>(0x24));

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}
std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedMulMemToReg8(std::size_t destination,
                                                                          std::size_t sourceOffset,
                                                                          std::size_t sourceRegister)
{
     std::vector<std::byte> binary{};

     // REX if needed
     if (destination >= 8 || sourceRegister >= 8 || (destination & 7) >= 4 || (sourceRegister & 7) >= 4)
     {
          std::uint8_t rex = 0x40;
          if (destination >= 8) rex |= 0x04;
          if (sourceRegister >= 8) rex |= 0x01;
          binary.push_back(static_cast<std::byte>(rex));
     }

     // IMUL r8, r/m8: opcode 0F AF /r
     binary.push_back(static_cast<std::byte>(0x0F));
     binary.push_back(static_cast<std::byte>(0xAF));

     const bool needsSib = (sourceRegister & 7) == 4;
     const bool forceDisp = (sourceRegister & 7) == 5;

     std::uint32_t mod{};
     if (sourceOffset == 0 && !forceDisp) mod = 0b00;
     else if (sourceOffset <= 0x7F)
          mod = 0b01;
     else
          mod = 0b10;

     const std::uint8_t rm = needsSib ? 4 : (sourceRegister & 7);
     const std::size_t modrm = static_cast<std::uint32_t>(mod << 6) | ((destination & 7) << 3) | rm;
     binary.push_back(static_cast<std::byte>(modrm));

     if (needsSib) binary.push_back(static_cast<std::byte>(0x24));

     if (mod == 0b01) binary.push_back(static_cast<std::byte>(sourceOffset & 0xFF));
     else if (mod == 0b10)
          for (std::size_t i = 0; i < 4; i++)
               binary.push_back(static_cast<std::byte>((sourceOffset >> (i * 8)) & 0xFF));

     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv64(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);
     std::uint8_t rex = 0x48;   // REX.W
     if (reg >= 8) rex |= 0x01; // REX.B
     binary.push_back(static_cast<std::byte>(rex));
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF0 | (reg & 7))); // ModRM: mod=11, reg=6 (DIV), rm=reg
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv32(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);
     if (reg >= 8)
     {
          binary.push_back(static_cast<std::byte>(0x41)); // REX.B
     }
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF0 | (reg & 7))); // ModRM: mod=11, reg=6, rm=reg
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv16(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.reserve(4);
     binary.push_back(static_cast<std::byte>(0x66)); // operand-size prefix for 16-bit
     if (reg >= 8)
     {
          binary.push_back(static_cast<std::byte>(0x41)); // REX.B
     }
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF0 | (reg & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDiv8(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);
     if (reg >= 8 || (reg & 7) >= 4)
     {
          std::uint8_t rex = 0x40;
          if (reg >= 8) rex |= 0x01;
          binary.push_back(static_cast<std::byte>(rex));
     }
     binary.push_back(static_cast<std::byte>(0xF6));
     binary.push_back(static_cast<std::byte>(0xF0 | (reg & 7))); // ModRM: mod=11, reg=6 (DIV), rm=reg
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem64(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> code;
     code.reserve(8);

     // REX.W for 64-bit
     std::uint8_t rex = 0x48;
     if (baseReg >= 8) rex |= 0x01; // REX.B
     code.push_back(std::byte{rex});

     // opcode F7 /6 for DIV r/m64
     code.push_back(std::byte{0xF7});

     const bool needsSib = (baseReg & 7) == 4;  // RSP/R12
     const bool forceDisp = (baseReg & 7) == 5; // RBP/R13

     std::byte mod{};
     if (displacement == 0 && !forceDisp) mod = std::byte{0x00};
     else if (displacement >= -128 && displacement <= 127)
          mod = std::byte{0x40};
     else
          mod = std::byte{0x80};

     std::size_t modrm = (static_cast<std::uint8_t>(mod) & 0xC0) | (6 << 3) | (needsSib ? 4 : (baseReg & 7));
     code.push_back(static_cast<std::byte>(modrm));

     if (needsSib) code.push_back(std::byte{0x24}); // SIB: scale=0, index=none, base=rsp

     if (mod == std::byte{0x40}) code.push_back(std::byte{static_cast<std::uint8_t>(displacement & 0xFF)});
     else if (mod == std::byte{0x80})
     {
          for (std::size_t i = 0; i < 4; i++)
               code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});
     }

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem32(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> code;
     code.reserve(7);

     // REX if needed
     if (baseReg >= 8) code.push_back(std::byte{0x41}); // REX.B

     // opcode F7 /6 for DIV r/m32
     code.push_back(std::byte{0xF7});

     const bool needsSib = (baseReg & 7) == 4;
     const bool forceDisp = (baseReg & 7) == 5;

     std::byte mod{};
     if (displacement == 0 && !forceDisp) mod = std::byte{0x00};
     else if (displacement >= -128 && displacement <= 127)
          mod = std::byte{0x40};
     else
          mod = std::byte{0x80};

     std::size_t modrm = (static_cast<std::uint8_t>(mod) & 0xC0) | (6 << 3) | (needsSib ? 4 : (baseReg & 7));
     code.push_back(static_cast<std::byte>(modrm));

     if (needsSib) code.push_back(std::byte{0x24});

     if (mod == std::byte{0x40}) code.push_back(std::byte{static_cast<std::uint8_t>(displacement & 0xFF)});
     else if (mod == std::byte{0x80})
     {
          for (std::size_t i = 0; i < 4; i++)
               code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});
     }

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem16(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> code;
     code.reserve(7);

     // operand-size prefix for 16-bit
     code.push_back(std::byte{0x66});

     // REX if needed
     if (baseReg >= 8) code.push_back(std::byte{0x41}); // REX.B

     // opcode F7 /6
     code.push_back(std::byte{0xF7});

     const bool needsSib = (baseReg & 7) == 4;
     const bool forceDisp = (baseReg & 7) == 5;

     std::byte mod{};
     if (displacement == 0 && !forceDisp) mod = std::byte{0x00};
     else if (displacement >= -128 && displacement <= 127)
          mod = std::byte{0x40};
     else
          mod = std::byte{0x80};

     std::size_t modrm = (static_cast<std::uint8_t>(mod) & 0xC0) | (6 << 3) | (needsSib ? 4 : (baseReg & 7));
     code.push_back(static_cast<std::byte>(modrm));

     if (needsSib) code.push_back(std::byte{0x24});

     if (mod == std::byte{0x40}) code.push_back(std::byte{static_cast<std::uint8_t>(displacement & 0xFF)});
     else if (mod == std::byte{0x80})
     {
          for (std::size_t i = 0; i < 4; i++)
               code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});
     }

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateUnsignedDivMem8(std::size_t baseReg, std::int32_t displacement)
{
     std::vector<std::byte> code;
     code.reserve(7);

     // REX if needed
     if (baseReg >= 8)
     {
          std::uint8_t rex = 0x40;
          rex |= 0x01; // REX.B
          code.push_back(std::byte{rex});
     }

     // opcode F6 /6 for DIV r/m8
     code.push_back(std::byte{0xF6});

     const bool needsSib = (baseReg & 7) == 4;
     const bool forceDisp = (baseReg & 7) == 5;

     std::byte mod{};
     if (displacement == 0 && !forceDisp) mod = std::byte{0x00};
     else if (displacement >= -128 && displacement <= 127)
          mod = std::byte{0x40};
     else
          mod = std::byte{0x80};

     std::size_t modrm = (static_cast<std::uint8_t>(mod) & 0xC0) | (6 << 3) | (needsSib ? 4 : (baseReg & 7));
     code.push_back(static_cast<std::byte>(modrm));

     if (needsSib) code.push_back(std::byte{0x24});

     if (mod == std::byte{0x40}) code.push_back(std::byte{static_cast<std::uint8_t>(displacement & 0xFF)});
     else if (mod == std::byte{0x80})
     {
          for (std::size_t i = 0; i < 4; i++)
               code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});
     }

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv64(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);
     std::uint8_t rex = 0x48;   // REX.W
     if (reg >= 8) rex |= 0x01; // REX.B
     binary.push_back(static_cast<std::byte>(rex));
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF8 | (reg & 7))); // ModRM: mod=11, reg=7 (IDIV), rm=reg
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv32(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);
     if (reg >= 8)
     {
          binary.push_back(static_cast<std::byte>(0x41)); // REX.B
     }
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF8 | (reg & 7))); // ModRM: mod=11, reg=7, rm=reg
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv16(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.reserve(4);
     binary.push_back(static_cast<std::byte>(0x66)); // operand-size prefix for 16-bit
     if (reg >= 8)
     {
          binary.push_back(static_cast<std::byte>(0x41)); // REX.B
     }
     binary.push_back(static_cast<std::byte>(0xF7));
     binary.push_back(static_cast<std::byte>(0xF8 | (reg & 7)));
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateSignedDiv8(std::size_t reg)
{
     std::vector<std::byte> binary{};
     binary.reserve(3);
     if (reg >= 8 || (reg & 7) >= 4)
     {
          std::uint8_t rex = 0x40;
          if (reg >= 8) rex |= 0x01;
          binary.push_back(static_cast<std::byte>(rex));
     }
     binary.push_back(static_cast<std::byte>(0xF6));
     binary.push_back(static_cast<std::byte>(0xF8 | (reg & 7))); // ModRM: mod=11, reg=7 (IDIV), rm=reg
     return binary;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg8(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> code;
     code.reserve(3);

     // REX prefix if needed
     bool needsRex = (destReg >= 8) || (srcReg >= 8) || ((destReg & 7) >= 4) || ((srcReg & 7) >= 4);
     if (needsRex)
     {
          std::uint8_t rex = 0x40;
          if (srcReg >= 8) rex |= 0x04;  // REX.R
          if (destReg >= 8) rex |= 0x01; // REX.B
          code.push_back(std::byte{rex});
     }

     // opcode: 30 /r (XOR r/m8, r8)
     code.push_back(std::byte{0x30});

     // ModRM byte: mod=11 (register), reg=srcReg, rm=destReg
     std::size_t modrm = 0xC0 | ((srcReg & 0x7) << 3) | (destReg & 0x7);
     code.push_back(static_cast<std::byte>(modrm));

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg16(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> code;
     code.reserve(4);

     // operand size prefix for 16-bit
     code.push_back(std::byte{0x66});

     // REX if needed for extended registers
     if (destReg >= 8 || srcReg >= 8)
     {
          std::uint8_t rex = 0x40;
          if (srcReg >= 8) rex |= 0x04;  // REX.R
          if (destReg >= 8) rex |= 0x01; // REX.B
          code.push_back(std::byte{rex});
     }

     // opcode: 31 /r (XOR r/m16, r16)
     code.push_back(std::byte{0x31});

     // ModRM: mod=11 reg=srcReg rm=destReg
     std::size_t modrm = 0xC0 | ((srcReg & 0x7) << 3) | (destReg & 0x7);
     code.push_back(static_cast<std::byte>(modrm));

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg32(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> code;
     code.reserve(3);

     // REX if needed for extended registers
     if (destReg >= 8 || srcReg >= 8)
     {
          std::uint8_t rex = 0x40;
          if (srcReg >= 8) rex |= 0x04;  // REX.R
          if (destReg >= 8) rex |= 0x01; // REX.B
          code.push_back(std::byte{rex});
     }

     // opcode: 31 /r (XOR r/m32, r32)
     code.push_back(std::byte{0x31});

     // ModRM: mod=11 reg=srcReg rm=destReg
     std::size_t modrm = 0xC0 | ((srcReg & 0x7) << 3) | (destReg & 0x7);
     code.push_back(static_cast<std::byte>(modrm));

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateXorReg64(std::size_t destReg, std::size_t srcReg)
{
     std::vector<std::byte> code;
     code.reserve(3);

     // REX prefix for 64-bit with REX.W=1
     std::uint8_t rex = 0x48;       // REX.W
     if (srcReg >= 8) rex |= 0x04;  // REX.R
     if (destReg >= 8) rex |= 0x01; // REX.B
     code.push_back(std::byte{rex});

     // opcode: 31 /r (XOR r/m64, r64)
     code.push_back(std::byte{0x31});

     // ModRM: mod=11 reg=srcReg rm=destReg
     std::size_t modrm = 0xC0 | ((srcReg & 0x7) << 3) | (destReg & 0x7);
     code.push_back(static_cast<std::byte>(modrm));

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GeneratePushReg64(std::size_t reg)
{
     std::vector<std::byte> code;
     code.reserve(2);

     if (reg >= 8) code.push_back(std::byte{0x41}); // REX.B for r8..r15
     code.push_back(std::byte{static_cast<std::uint8_t>(0x50 + (reg & 0x7))});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GeneratePopReg64(std::size_t reg)
{
     std::vector<std::byte> code;
     code.reserve(2);

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
     for (std::size_t i = 0; i < 4; i++)
          code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateIndirectCall2(std::int32_t displacement)
{
     std::vector<std::byte> code;
     code.reserve(6); // 2 bytes opcode + 4 bytes displacement

     code.push_back(std::byte{0xFF}); // opcode
     code.push_back(std::byte{0x15}); // ModRM for RIP-relative memory

     // RIP-relative displacement (32-bit)
     for (std::size_t i = 0; i < 4; i++)
          code.push_back(std::byte{static_cast<std::uint8_t>((displacement >> (i * 8)) & 0xFF)});

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateRegisterCall(std::size_t reg)
{
     std::vector<std::byte> code;
     code.reserve(2);

     // FF /2 -> ModR/M: 11 010 reg
     std::size_t modrm = 0b11010000 | static_cast<std::uint8_t>(reg & 0x07);
     code.push_back(std::byte{0xFF});
     code.push_back(static_cast<std::byte>(modrm));

     return code;
}

std::vector<std::byte> ecpps::codegen::x86_64::GenerateLeaToReg(std::size_t sourceRegister,
                                                                std::size_t sourceDisplacement,
                                                                std::size_t destinationRegister)
{
     std::vector<std::byte> code;
     code.reserve(9);

     constexpr bool rexW = true;
     const bool rexR = destinationRegister >= 8;
     const bool rexB = sourceRegister >= 8;

     std::uint8_t rex = 0x40;
     if (rexW) rex |= 0x08;
     if (rexR) rex |= 0x04;

     if (sourceRegister == Rip)
     {
          code.push_back(std::byte{rex});

          code.push_back(std::byte{0x8D});

          const std::uint8_t destination = static_cast<std::uint8_t>(destinationRegister & 7);

          const std::size_t modrm = static_cast<std::uint8_t>((0b00 << 6) | (destination << 3) | 0b101);
          code.push_back(static_cast<std::byte>(modrm));

          const std::int32_t d32 = static_cast<std::int32_t>(sourceDisplacement);
          for (std::size_t i = 0; i < 4; i++)
               code.push_back(static_cast<std::byte>(static_cast<std::uint8_t>((d32 >> (i * 8)) & 0xFF)));

          return code;
     }

     if (rexB) rex |= 0x01;
     code.push_back(static_cast<std::byte>(rex));

     // opcode: LEA r64, m
     code.push_back(std::byte{0x8D});

     const std::uint32_t destination = static_cast<std::uint8_t>(destinationRegister & 7);
     const std::uint32_t base = static_cast<std::uint8_t>(sourceRegister & 7);

     const bool needsSib = base == 4;   // RSP / R12
     const bool forceDisp8 = base == 5; // RBP / R13 with mod=00 is illegal

     std::int64_t displacement = static_cast<std::int64_t>(sourceDisplacement);

     std::uint32_t mod{};
     if (displacement == 0 && !forceDisp8) mod = 0b00;
     else if (displacement >= -128 && displacement <= 127)
          mod = 0b01;
     else
          mod = 0b10;

     // ModRM
     std::size_t modrm =
         static_cast<std::uint8_t>(static_cast<std::uint32_t>(mod << 6) | (destination << 3) | (needsSib ? 4 : base));
     code.push_back(static_cast<std::byte>(modrm));

     // SIB
     if (needsSib)
     {
          std::uint8_t sib = static_cast<std::uint8_t>((0 << 6) | (4 << 3) | base);
          code.push_back(static_cast<std::byte>(sib));
     }

     if (mod == 0b01) { code.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(displacement))); }
     else if (mod == 0b10)
     {
          std::int32_t d32 = static_cast<std::int32_t>(displacement);
          for (std::size_t i = 0; i < 4; i++)
               code.push_back(static_cast<std::byte>(static_cast<std::uint8_t>((d32 >> (i * 8)) & 0xFF)));
     }

     return code;
}

// NOLINTEND(readability-identifier-length)
