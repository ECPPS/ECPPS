#pragma once

#include "TypeBase.h"

namespace ecpps::typeSystem
{
     class ArrayType final : public TypeBase
     {
     public:
          explicit ArrayType(const std::size_t nElements, TypePointer elementType, std::size_t alignmentRequirements = 0)
              : TypeBase(std::format("{}[{}]", elementType->Name(), nElements)), _nElements(nElements),
                _elementType(std::move(elementType)),
                _alignmentRequirements(std::max(alignmentRequirements, _elementType->Alignment()))
          {
          }
          [[nodiscard]] std::size_t ElementCount(void) const noexcept { return this->_nElements; }
          [[nodiscard]] const std::shared_ptr<TypeBase>& ElementType(void) const noexcept { return this->_elementType; }

          [[nodiscard]] std::size_t Size(void) const noexcept override;

          [[nodiscard]] std::size_t Alignment(void) const noexcept override { return this->_alignmentRequirements; }

          [[nodiscard]] std::string RawName(void) const override
          {
               return std::format("{}[{}]", this->_elementType->RawName(), this->_nElements);
          }

          [[nodiscard]] ConversionSequence CompareTo(const std::shared_ptr<TypeBase>& other) override;

          [[nodiscard]] TypeTraits Traits(void) const noexcept final;
          [[nodiscard]] std::shared_ptr<TypeBase> CommonWith(const std::shared_ptr<TypeBase>& other) final;

     private:
          std::size_t _nElements;
          TypePointer _elementType;
          std::size_t _alignmentRequirements;
     };
} // namespace ecpps::typeSystem
