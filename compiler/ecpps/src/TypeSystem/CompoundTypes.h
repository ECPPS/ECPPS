#pragma once

#include <algorithm>
#include <format>
#include "TypeBase.h"

namespace ecpps::typeSystem
{
     class ArrayType final : public TypeBase
     {
     public:
          explicit ArrayType(const std::size_t nElements, typeSystem::NonowningTypePointer elementType,
                             std::size_t alignmentRequirements = 0)
              : TypeBase(std::format("{}[{}]", elementType->Name(), nElements)), _nElements(nElements),
                _elementType(elementType),
                _alignmentRequirements(std::max(alignmentRequirements, _elementType->Alignment()))
          {
          }
          [[nodiscard]] std::size_t ElementCount(void) const noexcept { return this->_nElements; }
          [[nodiscard]] NonowningTypePointer ElementType(void) const noexcept { return this->_elementType; }

          [[nodiscard]] std::size_t Size(void) const noexcept override;

          [[nodiscard]] std::size_t Alignment(void) const noexcept override { return this->_alignmentRequirements; }

          [[nodiscard]] std::string RawName(void) const override
          {
               return std::format("{}[{}]", this->_elementType->RawName(), this->_nElements);
          }

          [[nodiscard]] ConversionSequence CompareTo(NonowningTypePointer other) const override;

          [[nodiscard]] TypeTraits Traits(void) const noexcept final;
          [[nodiscard]] NonowningTypePointer CommonWith(NonowningTypePointer other) const final;

     private:
          std::size_t _nElements;
          typeSystem::NonowningTypePointer _elementType;
          std::size_t _alignmentRequirements;
     };
} // namespace ecpps::typeSystem
