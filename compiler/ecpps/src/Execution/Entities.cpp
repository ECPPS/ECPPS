#include "Entities.h"

ecpps::ir::Entity::~Entity(void) = default;

namespace ecpps::ir
{
     EntityStatistics& GetEntityStatistics(void)
     {
          static EntityStatistics statistics{};
          return statistics;
     }
} // namespace ecpps::ir
