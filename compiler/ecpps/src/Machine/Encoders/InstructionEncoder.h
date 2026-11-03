#pragma once

#include <memory>
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Context.h"

namespace ecpps::abi
{
     [[nodiscard]] std::unique_ptr<api::Target> CreateTarget(const encoding::CompilationContext& context);
} // namespace ecpps::abi
