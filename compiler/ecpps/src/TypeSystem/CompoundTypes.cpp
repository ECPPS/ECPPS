#include "CompoundTypes.h"

std::size_t ecpps::typeSystem::ArrayType::Size(void) const noexcept
{
     const auto elementSize = this->_elementType->Size();
     const auto alignedElementSize = Align(elementSize, this->_alignmentRequirements);
     return this->_nElements * alignedElementSize;
}

ecpps::typeSystem::ConversionSequence ecpps::typeSystem::ArrayType::CompareTo(
     [[maybe_unused]] NonowningTypePointer other) const
{
     return ConversionSequence{std::nullopt};
}

ecpps::typeSystem::TypeTraits ecpps::typeSystem::ArrayType::Traits(void) const noexcept
{
     TypeTraits traits{TypeTraitEnum::Array};
     if (IsTriviallyCopyable(this->_elementType)) traits.Set(TypeTraitEnum::TriviallyCopyable);
     if (IsImplicitLifetime(this->_elementType)) traits.Set(TypeTraitEnum::ImplicitLifetime);
     return traits;
}

ecpps::typeSystem::NonowningTypePointer ecpps::typeSystem::ArrayType::CommonWith(
     [[maybe_unused]] NonowningTypePointer other) const
{
     return nullptr;
}
