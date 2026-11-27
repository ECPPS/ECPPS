
#include "Optimiser/selector.h"
#include "Optimiser/passes/deadCode.h"
#include "Optimiser/passes/inline.h"
#include "Shared/Config.h"

std::vector<ecpps::optimisations::Pass> ecpps::optimisations::SelectPasses(ecpps::OptimisationFeatureSets features,
                                                                           ecpps::BumpAllocator& allocator)
{
     std::vector<ecpps::optimisations::Pass> passes{};
     passes.reserve(features.features.count());
     if (features.IsEnabled(Optimisation::InlineRegularFunctions)) passes.emplace_back(InlinePass{allocator});
     if (features.IsEnabled(Optimisation::DeadCodeElimination)) passes.emplace_back(DeadCodePass{});
     return passes;
}
