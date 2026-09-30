#pragma once

#include <cstddef>
#include <cstdint>
#include "Machine/Encoders/API/SDK.h"
namespace ecpps::abi::api
{
     enum struct ExtensionRuling : std::uint8_t
     {
          CallerExtendsSigned,
          CallerExtendsZero,
          CalleeExtends
     };
     enum struct StackParameterOrdering : std::uint8_t
     {
          Forward,
          Reverse
     };

     struct PlatformBase
     {
          explicit PlatformBase(SDKBase* currentSdk) : _currentSdk(currentSdk)
          {
          }
          virtual ~PlatformBase(void) = default;

          [[nodiscard]] virtual std::uint8_t IntegerReturnRegisterIndex(void) const noexcept
          {
               return 0;
          }
          [[nodiscard]] virtual std::uint8_t ThisPointerRegisterIndex(void) const noexcept
          {
               return 0;
          }
          [[nodiscard]] virtual bool HasSharedParameterRegisterAllocation(void) const noexcept = 0;
          [[nodiscard]] virtual bool CanSplitStructAcrossRegisters(void) const noexcept = 0;

          [[nodiscard]] virtual ExtensionRuling SubWordExtensionPolicy(void) const noexcept = 0;
          [[nodiscard]] virtual StackParameterOrdering StackParameterOrder(void) const noexcept = 0;

          [[nodiscard]] virtual std::size_t IntegerParameterRegisterCount(void) const noexcept = 0;
          [[nodiscard]] virtual std::size_t ParameterStackSlotWidth(void) const noexcept = 0;
          [[nodiscard]] virtual std::size_t MaxStructRegisterPassingSize(void) const noexcept = 0;
          [[nodiscard]] virtual std::size_t IntegerParameterRegisterIndex(std::size_t reg) const = 0;
          [[nodiscard]] virtual std::size_t StackAlignment(void) const noexcept
          {
               return 0;
          }
          [[nodiscard]] virtual std::size_t InitialStackReserve(void) const noexcept
          {
               return 0;
          }
          [[nodiscard]] virtual std::uint32_t CalleeSavedRegisterMask(void) const noexcept
          {
               return 0;
          }
          [[nodiscard]] bool IsCalleeSaved(const std::uint8_t registerIndex) const noexcept
          {
               if (registerIndex >= 32) return false;
               return ((this->CalleeSavedRegisterMask() >> registerIndex) & 1U) != 0;
          }
          virtual void PrepareABI(void)
          {
          }

     protected:
          SDKBase* _currentSdk; // TODO: non-null pointer
     };
} // namespace ecpps::abi::api
