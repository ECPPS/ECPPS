#include "MemoryAccess.h"

std::string ecpps::ir::ToString(MemoryOrdering order)
{
     switch (order)
     {
     case ecpps::ir::MemoryOrdering::Acquire: return "acquire";
     case ecpps::ir::MemoryOrdering::Release: return "release";
     case ecpps::ir::MemoryOrdering::AcquireRelease: return "acquire-release";
     case ecpps::ir::MemoryOrdering::Sequenced: return "sequence";
     case ecpps::ir::MemoryOrdering::Relaxed: return "relaxed";
     }
     return "__unknown_ordering";
}
