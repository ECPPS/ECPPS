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

          [[nodiscard]] api::AggregatePassingPlan ClassArgumentPassing(api::AggregateValueInfo info) const final
          {
               return ClassPassing(info, false);
          }

          [[nodiscard]] api::AggregatePassingPlan ClassReturnPassing(api::AggregateValueInfo info) const final
          {
               return ClassPassing(info, true);
          }

          [[nodiscard]] api::AggregateResultAddressPlan IndirectClassResultAddress(void) const noexcept final
          {
               return {.location = api::AggregateResultAddressLocation::FirstIntegerArgument,
                       .addressEchoedInIntegerReturnRegister = true};
          }

          [[nodiscard]] api::ClassArgumentCleanupOwner ClassArgumentCleanup(void) const noexcept final
          {
               return api::ClassArgumentCleanupOwner::Caller;
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
               return api::StackParameterOrdering::Forward;
          }

     private:
          [[nodiscard]] static api::AggregatePassingPlan ClassPassing(api::AggregateValueInfo info, const bool isReturn)
          {
               if (!info.triviallyCopyable) return {};
               if (info.size > 16)
                    return isReturn ? api::AggregatePassingPlan{.kind = api::AggregatePassingKind::Memory}
                                    : api::AggregatePassingPlan{.kind = api::AggregatePassingKind::Unsupported};
               if (info.size > 8 || !info.integerOnly) return {.kind = api::AggregatePassingKind::Unsupported};
               switch (info.size)
               {
               case 1:
               case 2:
               case 4:
               case 8:
                    return {
                         .kind = api::AggregatePassingKind::Direct,
                         .pieces = {
                              {.offset = 0, .size = info.size, .registerClass = api::AggregateRegisterClass::Integer}}};
               default: return {.kind = api::AggregatePassingKind::Unsupported};
               }
          }

          [[nodiscard]] static constexpr std::uint32_t MaskOf(const RegisterIndex index) noexcept
          {
               return 1u << std::to_underlying(index);
          }
     };
} // namespace ecpps::abi::encoders::x8664
