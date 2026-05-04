#include "Entities.h"

ecpps::ir::Entity::~Entity(void) = default;

ecpps::ir::EntityStatistics& ecpps::ir::GetEntityStatistics(void)
{
     static EntityStatistics statistics{};
     return statistics;
}
