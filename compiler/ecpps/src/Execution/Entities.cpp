#include "Entities.h"
#include <limits>

ecpps::ir::Entity::~Entity(void)
{
     // thumbstone
     this->_id = std::numeric_limits<std::size_t>::max();
     this->_kind = static_cast<EntityKind>(0xcd);
}

ecpps::ir::EntityStatistics& ecpps::ir::GetEntityStatistics(void)
{
     static EntityStatistics statistics{};
     return statistics;
}
