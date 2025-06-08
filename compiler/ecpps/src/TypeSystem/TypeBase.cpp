#include "TypeBase.h"
#include <memory>
#include "ArithmeticTypes.h"

std::shared_ptr<ecpps::typeSystem::TypeBase> ecpps::typeSystem::VoidType::CommonWith(
    const std::shared_ptr<ecpps::typeSystem::TypeBase>& other)
{
     {
          return IsIncomplete(other) ? ecpps::typeSystem::g_void : nullptr;
     }
}
