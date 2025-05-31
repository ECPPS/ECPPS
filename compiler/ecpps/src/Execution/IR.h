#pragma once
#include <vector>
#include "../Parsing/AST.h"
#include "Expressions.h"
#include "NodeBase.h"

namespace ecpps::ir
{
     class IR
     {
     public:
          static std::vector<NodePointer> Parse(const std::vector<ast::NodePointer>& ast);

     private:
          explicit IR(void) = default;
          std::vector<NodePointer> _built{};

          void ParseNode(const ast::NodePointer& node);
          void ParseFunctionDefinition(const ast::FunctionDefinitionNode& node);
          void ParseReturn(const ast::ReturnNode& node);
          Expression ParseExpression(const ast::NodePointer& expression);
     };
} // namespace ecpps::ir