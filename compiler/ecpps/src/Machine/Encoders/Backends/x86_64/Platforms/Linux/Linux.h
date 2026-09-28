#pragma once

#include <utility>
#include "Machine/Encoders/API/Platform.h"
#include "Machine/Encoders/API/SDK.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "Shared/Diagnostics.h"

namespace ecpps::abi::encoders::x8664
{
     struct LinuxPlatform : api::PlatformBase
     {
          explicit LinuxPlatform(api::SDKBase* currentSdk) : PlatformBase(currentSdk)
          {
          }
          [[nodiscard]] std::size_t IntegerParameterRegisterCount(void) const noexcept final
          {
               return 4;
          }
          [[nodiscard]] std::size_t IntegerParameterRegisterIndex(std::size_t reg) const final
          {
               switch (reg)
               {
               case 0: return std::to_underlying(RegisterIndex::Rdi);
               case 1: return std::to_underlying(RegisterIndex::Rsi);
               case 2: return std::to_underlying(RegisterIndex::Rdx);
               case 3: return std::to_underlying(RegisterIndex::Rcx);
               }
               throw TracedException("Invalid");
          }
          [[nodiscard]] std::uint32_t CalleeSavedRegisterMask(void) const noexcept final
          {
               return MaskOf(RegisterIndex::Rbx) | MaskOf(RegisterIndex::Rbp) | MaskOf(RegisterIndex::Rsp) |
                      MaskOf(RegisterIndex::R12) | MaskOf(RegisterIndex::R13) | MaskOf(RegisterIndex::R14) |
                      MaskOf(RegisterIndex::R15);
          }

          [[nodiscard]] std::size_t StackAlignment(void) const noexcept final
          {
               return 16;
          }

     private:
          [[nodiscard]] static constexpr std::uint32_t MaskOf(const RegisterIndex index) noexcept
          {
               return 1u << std::to_underlying(index);
          }
     };
} // namespace ecpps::abi::encoders::x8664
