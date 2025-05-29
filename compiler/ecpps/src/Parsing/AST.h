#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include "SourceMap.h"
#include "Tokeniser.h"

namespace ecpps::ast
{
     class Node
     {
     public:
          explicit Node(Location location) : _location(std::move(location)) {}

     private:
          Location _location;
     };
     using NodePointer = std::unique_ptr<Node>;

     enum struct ConstantExpressionSpecifier
     {
          None,
          Constexpr,
          Consteval,
          Constinit
     };
     enum struct Operator
     {
          Function,              // ()
          Ellipsis,              // ...
          QuestionMark,          // ?
          FullStop,              // .
          PointerToMemberObject, // .*
          Arrow,                 // ->
          PointerToMember,       // ->*
          Tilde,                 // ~
          Exclamation,           // !

          Plus,              // +
          Minus,             // -
          Asterisk,          // *
          Solidus,           // /
          Percent,           // %
          CircrumflexAccent, // ^
          Ampersand,         // &
          VerticalLine,      // |
          Assignment,        // =
          PlusAssign,        // +=
          MinusAssign,       // -=
          MultiplyAssign,    // *=
          DivideAssign,      // /=
          ModuloAssign,      // %=
          XorAssign,         // ^=
          AndAssign,         // &=
          OrAssign,          // |=

          EqualsSign,       // =
          NotEqual,         // !=
          Less,             // <
          Greater,          // >
          LessEqual,        // <=
          GreaterEqual,     // >=
          Spaceship,        // <=>
          LogicalAnd,       // &&
          LogicalOr,        // ||
          LeftShift,        // <<
          RightShift,       // >>
          LeftShiftAssign,  // >>=
          RightShiftAssign, // <<=
          Increment,        // ++
          Decrement,        // --
          Comma             // ,
     };

     class AttributeNode : public Node
     {
     };

     class TypeNode : public Node
     {
     };

     class IdentifierNode final : public Node
     {
     public:
          explicit IdentifierNode(std::string value, Location where) : Node(std::move(where)), _value(std::move(value))
          {
          }

     private:
          std::string _value;
     };

     class OperatorFunctionId final : Node
     {
     public:
          explicit OperatorFunctionId(Operator op, Location where) : Node(std::move(where)), _operator(std::move(op)) {}

          [[nodiscard]] Operator operator_(void) const noexcept { return this->_operator; }

     private:
          Operator _operator;
     };

     class QualifiedIdNode final : public Node
     {
     public:
     private:
          NodePointer _unqualified;
          std::vector<NodePointer> _path;
     };

     struct FunctionSignature
     {
          std::unique_ptr<TypeNode> type;
          bool isFriend;
          bool isInline;
          bool isExtern;
          std::optional<std::string> externOptional;
          ConstantExpressionSpecifier constexprSpecifier;
          std::vector<AttributeNode> attributes;
          NodePointer name;
          // TODO: Trailing return types

          explicit FunctionSignature(std::unique_ptr<TypeNode> type, bool isFriend, bool isInline,
                                     ConstantExpressionSpecifier constexprSpecifier,
                                     std::vector<AttributeNode> attributes, NodePointer name)
              : type(std::move(type)), isFriend(isFriend), isInline(isInline), constexprSpecifier(constexprSpecifier),
                attributes(std::move(attributes)), name(std::move(name))
          {
          }
     };

     class FunctionDefinitionNode
     {
     public:
     private:
          FunctionSignature _signature;
          std::vector<NodePointer> _body;
     };

     class AST
     {
     public:
          explicit AST(std::vector<Token> tokens)
               : _tokens(std::move(tokens))
          {}
          [[nodiscard]] std::vector<NodePointer> Parse(void);

     private:
          std::vector<Token> _tokens;
          std::size_t _position{};

          [[nodiscard]] bool AtEnd(void) const { return this->_position >= this->_tokens.size(); }
          [[nodiscard]] const Token& Peek(const std::ptrdiff_t offset = 0) const noexcept
          {
               return this->_tokens[this->_position + offset];
          }
          [[nodiscard]] Token& Peek(const std::ptrdiff_t offset = 0) { return this->_tokens[this->_position + offset]; }
          [[nodiscard]] void Advance(void) noexcept { this->_position++; }
          [[nodiscard]] void Retreat(void) noexcept { this->_position--; }
          [[nodiscard]] bool Match(const TokenType type) noexcept
          {
               if (Peek().type != type) return false;
               Advance();
               return true;
          }
     };
} // namespace ecpps::ast