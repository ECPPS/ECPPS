#pragma once

#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Context.h"

namespace ecpps::abi
{
     class BackendRegistry
     {
     public:
          static void Register(encoding::CompilationContext name, std::unique_ptr<api::Target> backend)
          { _backends.emplace(name.MakeId(), std::move(backend)); }

          static api::Target& Get(const encoding::CompilationContext& name) { return *_backends.at(name.MakeId()); }
          static api::Target& Get(const encoding::CompilationId identifier) { return *_backends.at(identifier); }

          static void RegisterTargets(void);

     private:
          static std::unordered_map<encoding::CompilationId, std::unique_ptr<api::Target>> _backends;
     };
} // namespace ecpps::abi
