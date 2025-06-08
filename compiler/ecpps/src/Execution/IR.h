#pragma once
#include <vector>
#include "../Parsing/AST.h"
#include "../Shared/Diagnostics.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "Expressions.h"
#include "NodeBase.h"
#include "context.h"

namespace ecpps::ir
{
     class IR
     {
     public:
          static std::vector<NodePointer> Parse(Diagnostics& diagnostics, const std::vector<ast::NodePointer>& ast);

     private:
          explicit IR(Diagnostics& diagnostics) : _context(diagnostics) {}
          std::vector<NodePointer> _built{};
          Context _context;

          void ParseNode(const ast::NodePointer& node);
          void ParseFunctionDefinition(const ast::FunctionDefinitionNode& node);
          void ParseReturn(const ast::ReturnNode& node);

          Expression ParseAdditiveExpression(Expression left, ast::Operator operator_, Expression right, const Location& source);
          Expression ParseMultiplicativeExpression(Expression left, ast::Operator operator_, Expression right, const Location& source);
          Expression ParseShiftExpression(Expression left, ast::Operator operator_, Expression right, const Location& source);

          Expression ParseBinaryExpression(const ast::BinaryOperatorNode& node);
          Expression ParseExpression(const ast::NodePointer& expression);

          typeSystem::TypePointer ParseType(const ast::NodePointer& type);
          Expression ConvertTo(Expression&& expression, const typeSystem::TypePointer& toType);

          /// <summary>
          /// Only for integral conversions that are known to be integral conversions. If the conversion is not
          /// integral, the behaviour of this function is undefined.
          /// </summary>
          /// <param name="expression"></param>
          /// <param name="type"></param>
          /// <returns></returns>
          Expression ConvertIntegral(Expression&& expression, const std::shared_ptr<typeSystem::IntegralType>& type);
     };
} // namespace ecpps::ir