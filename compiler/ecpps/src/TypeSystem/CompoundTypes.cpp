#include "CompoundTypes.h"

std::size_t ecpps::typeSystem::ArrayType::Size(void) const noexcept
{
     const auto elementSize = this->_elementType->Size();
     const auto alignedElementSize = Align(elementSize, this->_alignmentRequirements);
     return this->_nElements * alignedElementSize;
}

ecpps::typeSystem::ConversionSequence ecpps::typeSystem::ArrayType::CompareTo(const std::shared_ptr<TypeBase>& other)
{
     return ConversionSequence{std::nullopt};
}

ecpps::typeSystem::TypeTraits ecpps::typeSystem::ArrayType::Traits(void) const noexcept
{
     return TypeTraits{TypeTraitEnum::Array, TypeTraitEnum::TriviallyCopyable, TypeTraitEnum::ImplicitLifetime};
}

std::shared_ptr<ecpps::typeSystem::TypeBase> ecpps::typeSystem::ArrayType::CommonWith(const std::shared_ptr<TypeBase>& other)
{
     return nullptr;
}
