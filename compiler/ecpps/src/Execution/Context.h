#pragma once
#include <Queue.h>
#include <functional>
#include <memory>
#include <unordered_set>
#include "../TypeSystem/TypeBase.h"

namespace ecpps::ir
{
     struct ContextBase
     {
          virtual ~ContextBase(void) = default;

     protected:
          explicit ContextBase(void) = default;

     private:
     };
     using ContextPointer = std::unique_ptr<ContextBase>;

     struct FunctionContext final : ContextBase
     {
          explicit FunctionContext(typeSystem::TypePointer returnType) : returnType(std::move(returnType)) {}
          typeSystem::TypePointer returnType;
     };
     struct ClassContext final : ContextBase
     {
     };
     struct NamespaceContext final : ContextBase
     {
     };

     struct Context
     {
          std::reference_wrapper<Diagnostics> diagnostics;
          std::unordered_set<typeSystem::TypePointer, typeSystem::TypePointerHash, typeSystem::TypePointerEqual>
              types{};
          SBOQueue<ContextPointer> contextSequence{};

          explicit Context(Diagnostics& diagnostics) : diagnostics(std::ref(diagnostics)) {}
     };
} // namespace ecpps::ir