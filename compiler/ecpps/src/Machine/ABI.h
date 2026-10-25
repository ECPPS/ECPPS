#pragma once
#include <RuntimeAssert.h>
#include <cstddef>
#include <string>
#include <vector>
#include "../TypeSystem/TypeBase.h"
#include "Machine.h"

/// <summary>
/// The term "width" is always measured in bits, while "size" in bytes. ECPPS ABI defines one byte to be exactly eight
/// bits, and the ABI is unsupported on any platform that states otherwise.
/// </summary>

namespace ecpps::abi
{
     struct MemberLayoutInput
     {
          std::size_t size{};
          std::size_t alignment{};
     };
     struct ClassLayoutResult
     {
          std::size_t size{};
          std::size_t alignment{};
          std::vector<std::size_t> offsets{};
     };
     enum struct Linkage : std::uint_fast8_t
     {
          NoLinkage,
          Internal,
          External,
          Module,
          CLinkage
     };

     enum struct CallingConventionName : std::uint_fast16_t // NOLINT(performance-enum-size)
     {
          Microsoftx64
     };

     template <typename T>
     concept NarrowCharArray = std::same_as<unsigned char[], T> || std::same_as<char8_t[], T>; // NOLINT

     class ABI
     {
     public:
          explicit ABI(ISA isa);

          static ABI& Current(void);

          [[nodiscard]] ISA Isa(void) const noexcept
          {
               return this->_isa;
          }

          [[nodiscard]] static std::string MangleName(Linkage linkage, const std::string& name,
                                                      CallingConventionName callingConvention,
                                                      typeSystem::NonowningTypePointer returnType,
                                                      const std::vector<typeSystem::NonowningTypePointer>& parameters,
                                                      const std::vector<std::string>& namespacePath);

          [[nodiscard]] CallingConventionName DefaultCallingConventionName(void) const;

          [[nodiscard]] std::size_t PointerSize(void) const noexcept
          {
               return this->_pointerSize;
          }
          void SetPointerSize(std::size_t newSize) noexcept
          {
               this->_pointerSize = newSize;
          }
          [[nodiscard]] typeSystem::TypeKind SizeSize(void) const noexcept
          {
               return this->sizeSize;
          }
          [[nodiscard]] typeSystem::TypeKind PtrDiffSize(void) const noexcept
          {
               return this->ptrdiffSize;
          }
          [[nodiscard]] typeSystem::TypeKind IntPtrSize(void) const noexcept
          {
               return this->intptrSize;
          }
          [[nodiscard]] typeSystem::TypeKind BoolSize(void) const noexcept
          {
               return this->boolSize;
          }

          typeSystem::TypeKind sizeSize{};
          typeSystem::TypeKind ptrdiffSize{};
          typeSystem::TypeKind boolSize{};
          typeSystem::TypeKind intptrSize{};

          std::string importPrefix{};

          [[nodiscard]] ClassLayoutResult LayoutClass(bool isUnion, std::span<const MemberLayoutInput> members) const;

     private:
          static ABI _current;

          ISA _isa;

          std::size_t _pointerSize{};
     };
} // namespace ecpps::abi

namespace
{
     [[maybe_unused]] std::string ToString(const ecpps::abi::CallingConventionName callingConvention) // NOLINT
     {
          switch (callingConvention)
          {
          case ecpps::abi::CallingConventionName::Microsoftx64: return "__mscall";
          default: return "__undefined";
          }
     }
} // namespace
