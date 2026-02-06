#pragma once
#include <functional>
#include <string>
#include <vector>
#include "../CodeGeneration/Nodes.h"
#include "../Shared/Config.h"
#include "../Shared/Diagnostics.h"

namespace ecpps
{

     struct alignas(std::uint64_t) StringIndex
     {
          std::uint32_t indexInTable{};
          std::uint32_t offset{};
     };

     enum struct InstructionPatchType : std::uint8_t
     {
          MovFrom,
          MovTo,
          LeaFrom,
          LeaTo
     };

     struct StringPatch
     {
          StringIndex string{};
          std::uint32_t instructionOffset{};
          InstructionPatchType patchType{};
     };

     struct SourceFile
     {
          std::string name{};
          std::string contents{};
          Diagnostics diagnostics{};
          std::vector<codegen::Routine> compiledRoutines{};
          std::vector<StringPatch> stringTranslation{};

          explicit SourceFile(void) = default;
     };
     struct SourceMap
     {
          explicit SourceMap(CompilerConfig& config);
          std::vector<SourceFile> files{};

     private:
          std::reference_wrapper<CompilerConfig> _config;
     };
} // namespace ecpps
