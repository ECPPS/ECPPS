#pragma once

#include <Shared/BumpAllocator.h>
#include <cstddef>

namespace ecpps::ast
{
     struct ASTContext : BumpAllocator
     {
          using BumpAllocator::BumpAllocator;
     };
} // namespace ecpps::ast
