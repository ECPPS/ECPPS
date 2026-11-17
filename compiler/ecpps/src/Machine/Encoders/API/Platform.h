#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
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
     enum struct AggregatePassingKind : std::uint8_t
     {
          Indirect,
          Direct,
          Memory,
          Unsupported
     };
     enum struct AggregateRegisterClass : std::uint8_t
     {
          Integer,
          FloatingPoint,
          Vector
     };
     enum struct ClassArgumentCleanupOwner : std::uint8_t
     {
          Caller,
          Callee
     };
     enum struct AggregateResultAddressLocation : std::uint8_t
     {
          FirstIntegerArgument,
          DedicatedRegister
     };
     struct AggregatePiece
     {
          std::size_t offset{};
          std::size_t size{};
          AggregateRegisterClass registerClass{};
     };
     struct AggregatePassingPlan
     {
          AggregatePassingKind kind{AggregatePassingKind::Indirect};
          std::vector<AggregatePiece> pieces{};
     };
     // Location of an ABI's hidden destination pointer; this is not a source-level reference return.
     struct AggregateResultAddressPlan
     {
          AggregateResultAddressLocation location{AggregateResultAddressLocation::FirstIntegerArgument};
          std::size_t integerRegisterIndex{};
          bool addressEchoedInIntegerReturnRegister{};
     };
     struct AggregateValueInfo
     {
          std::size_t size{};
          bool triviallyCopyable{};
          bool integerOnly{};
          bool hasUserDeclaredSpecialMembers{};
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
          [[nodiscard]] virtual AggregatePassingPlan ClassArgumentPassing(
               [[maybe_unused]] AggregateValueInfo info) const
          {
               return {};
          }
          [[nodiscard]] virtual AggregatePassingPlan ClassReturnPassing([[maybe_unused]] AggregateValueInfo info) const
          {
               return {};
          }
          [[nodiscard]] virtual AggregateResultAddressPlan IndirectClassResultAddress(void) const noexcept = 0;
          [[nodiscard]] virtual ClassArgumentCleanupOwner ClassArgumentCleanup(void) const noexcept = 0;
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
          [[nodiscard]] virtual std::size_t InlineInitialisationLimit(void) const noexcept
          {
               return 128; // bytes
          }

     protected:
          SDKBase* _currentSdk; // TODO: non-null pointer
     };
} // namespace ecpps::abi::api
