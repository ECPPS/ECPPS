#pragma once

#include <format>
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
               return 6;
          }
          [[nodiscard]] std::size_t IntegerParameterRegisterIndex(std::size_t reg) const final
          {
               switch (reg)
               {
               case 0: return std::to_underlying(RegisterIndex::Rdi);
               case 1: return std::to_underlying(RegisterIndex::Rsi);
               case 2: return std::to_underlying(RegisterIndex::Rdx);
               case 3: return std::to_underlying(RegisterIndex::Rcx);
               case 4: return std::to_underlying(RegisterIndex::R8);
               case 5: return std::to_underlying(RegisterIndex::R9);
               }
               throw TracedException(std::format("Invalid parameter register: passed {}", reg));
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
          [[nodiscard]] std::uint8_t ThisPointerRegisterIndex(void) const noexcept final
          {
               return std::to_underlying(RegisterIndex::Rdi);
          }

          [[nodiscard]] bool HasSharedParameterRegisterAllocation(void) const noexcept final
          {
               return false;
          }

          [[nodiscard]] std::size_t MaxStructRegisterPassingSize(void) const noexcept final
          {
               return 16;
          }

          [[nodiscard]] bool CanSplitStructAcrossRegisters(void) const noexcept final
          {
               return true;
          }

          [[nodiscard]] std::size_t ParameterStackSlotWidth(void) const noexcept final
          {
               return 64;
          }

          [[nodiscard]] api::ExtensionRuling SubWordExtensionPolicy(void) const noexcept final
          {
               return api::ExtensionRuling::CalleeExtends;
          }
          [[nodiscard]] api::StackParameterOrdering StackParameterOrder(void) const noexcept final
          {
               return api::StackParameterOrdering::Reverse;
          }

     private:
          [[nodiscard]] static constexpr std::uint32_t MaskOf(const RegisterIndex index) noexcept
          {
               return 1u << std::to_underlying(index);
          }
     };
} // namespace ecpps::abi::encoders::x8664
