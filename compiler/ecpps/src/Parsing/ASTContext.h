#pragma once

#include "StringPool.h"

namespace ecpps::ast
{
     struct ASTContext : StringPool
     {
          using StringPool::StringPool;
     };
} // namespace ecpps::ast
