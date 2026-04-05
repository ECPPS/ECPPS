#include "Context.h"

ecpps::ir::TypeContext& ecpps::ir::GetTypeContext(void)
{
     static TypeContext typeContext{};
     return typeContext;
}
