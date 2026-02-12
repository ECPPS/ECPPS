#include "TypeBase.h"
#include <memory>
#include "ArithmeticTypes.h"

typeSystem::NonowningTypePointer ecpps::typeSystem::VoidType::CommonWith(typeSystem::NonowningTypePointer other)
{
     {
          return IsIncomplete(other) ? ecpps::typeSystem::g_void : nullptr;
     }
}

bool ecpps::typeSystem::VoidType::operator==(const typeSystem::NonowningTypePointer other) const noexcept
{
     return other->RawName() == "void";
}

bool ecpps::typeSystem::TypeBase::operator==(const typeSystem::NonowningTypePointer other) const noexcept
{
     return this->RawName() == other->RawName();
}
