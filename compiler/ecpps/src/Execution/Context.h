#pragma once
#include <Queue.h>
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
          std::unordered_set<typeSystem::TypePointer, typeSystem::TypePointerHash, typeSystem::TypePointerEqual>
              types{};
          SBOQueue<ContextPointer> contextSequence{};
     };
} // namespace ecpps::ir