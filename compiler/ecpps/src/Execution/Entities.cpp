#include "Entities.h"

namespace ecpps::ir
{
     EntityStatistics& GetEntityStatistics(void)
     {
          static EntityStatistics statistics{};
          return statistics;
     }
} // namespace ecpps::ir
