#pragma once
#include <RuntimeAssert.h>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include "../CodeGeneration/Nodes.h"
#include "../TypeSystem/TypeBase.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine.h"
#include "Machine/Encoders/API/VirtualInstructionEncoder.h"
#include "Machine/Encoders/InstructionEncoder.h"
#include "Storage.h"

/// <summary>
/// The term "width" is always measured in bits, while "size" in bytes. ECPPS ABI defines one byte to be exactly eight
/// bits, and the ABI is unsupported on any platform that states otherwise.
/// </summary>

namespace ecpps::abi
{
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
          void SetPointerSize(const std::size_t newSize) noexcept
          {
               _pointerSize = newSize;
          }

          template <std::size_t TTo, std::size_t TFrom>
          [[nodiscard]] std::size_t ConvertEndian(std::size_t value) const noexcept;
          template <NarrowCharArray TArray, // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                            // modernize-avoid-c-arrays)
                    std::size_t TFrom>
          [[nodiscard]] auto ConvertEndian(std::size_t value) const noexcept
          {
               using T = std::remove_reference_t<decltype(std::declval<TArray>()[0])>;
               std::array<T, TFrom> output{};

               for (std::size_t i = 0; i < TFrom; i++)
               {
                    T byte = static_cast<T>(static_cast<unsigned char>((value >> (i * 8)) & 0xFF));
                    if (this->_endianness == ecpps::abi::Endianness::Big) output[TFrom - 1 - i] = byte;
                    else
                         output[i] = byte;
               }

               return output;
          }
          template <std::size_t TTo, std::same_as<unsigned char[]> TArray> // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                           // modernize-avoid-c-arrays)
          [[nodiscard]] std::size_t ConvertEndian(auto&& range) const noexcept
          {
               std::size_t value = 0;
               using Range = std::remove_reference_t<decltype(range)>;
               using IndexType = std::conditional_t<requires { typename Range::difference_type; },
                                                    typename Range::difference_type, std::size_t>;

               if (this->_endianness == ecpps::abi::Endianness::Big)
               {
                    for (std::size_t i = 0; i < TTo; i++)
                    {
                         value <<= 8;
                         value |= static_cast<std::size_t>(range[static_cast<IndexType>(i)]);
                    }
               }
               else
               {
                    for (std::size_t i = 0; i < TTo; i++)
                         value |= static_cast<std::size_t>(range[static_cast<IndexType>(i)]) << (i * 8);
               }

               return value;
          }

          typeSystem::TypeKind sizeSize{};
          typeSystem::TypeKind ptrdiffSize{};
          typeSystem::TypeKind boolSize{};
          typeSystem::TypeKind intptrSize{};

     private:
          static ABI _current;

          Endianness _endianness;
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
