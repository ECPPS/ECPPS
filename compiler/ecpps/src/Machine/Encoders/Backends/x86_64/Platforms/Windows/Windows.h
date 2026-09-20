#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include "Machine/Encoders/API/Platform.h"
#include "Machine/Encoders/API/SDK.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"

namespace ecpps::abi::encoders::x8664
{
     struct WindowsPlatform : api::PlatformBase
     {
          explicit WindowsPlatform(api::SDKBase* currentSdk) : PlatformBase(currentSdk)
          {
          }
          [[nodiscard]] std::size_t StackAlignment(void) const noexcept final
          {
               return 16;
          }
          [[nodiscard]] std::size_t InitialStackReserve(void) const noexcept final
          {
               return 32;
          }
          [[nodiscard]] std::uint32_t CalleeSavedRegisterMask(void) const noexcept final
          {
               return MaskOf(RegisterIndex::Rbx) | MaskOf(RegisterIndex::Rbp) | MaskOf(RegisterIndex::Rsi) |
                      MaskOf(RegisterIndex::Rdi) | MaskOf(RegisterIndex::R12) | MaskOf(RegisterIndex::R13) |
                      MaskOf(RegisterIndex::R14) | MaskOf(RegisterIndex::R15);
          }

     private:
          [[nodiscard]] static constexpr std::uint32_t MaskOf(const RegisterIndex index) noexcept
          {
               return 1u << std::to_underlying(index);
          }
     };
} // namespace ecpps::abi::encoders::x8664
