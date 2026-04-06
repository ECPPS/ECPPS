#include "Context.h"

ecpps::ir::Scope::~Scope(void) = default;
ecpps::ir::ContextBase::~ContextBase(void) = default;

ecpps::ir::TypeContext& ecpps::ir::GetTypeContext(void)
{
     static TypeContext typeContext{};
     return typeContext;
}
