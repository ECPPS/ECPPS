#pragma once

#include <Shared/BumpAllocator.h>

namespace ecpps::ast
{
     struct ASTContext : BumpAllocator
     {
          using BumpAllocator::BumpAllocator;
     };
} // namespace ecpps::ast
