#pragma once

#include "../Parsing/AST.h"
#include "Expressions.h"
#include "NodeBase.h"

namespace ecpps::ir
{
     enum struct BinaryOperationLevel
     {
          None,         // +            => add tmp
          Atomic,       // (atomic) +   => lock add tmp
          Assign,       // +=           => add
          AtomicAssign, // (atomic) +=  => lock add
     };

     enum struct MemoryOrdering
     {
          Acquire,
          Release,
          Relaxed,
          Sequenced,
     };

     /// <summary>
     /// add 2 (hopefully) integral operands. No checking as I can't predict them really, the optimiser might clash 2
     /// classes with that btw, if they are layout compatible, have an overloaded operator+= that adds all members 1:1
     /// to each other and each of them is possible to add using that node Example: struct C
     /// {
     ///      C& operator+(const C& other)
     ///      {
     ///           this->a += other.a;
     ///           this->b += other.b;
     ///           return *this;
     ///      }
     ///
     ///      int a;
     ///      int b;
     /// };
     ///
     /// This function supports vectorisation
     /// </summary>
     class AdditionNode final : public NodeBase
     {
     public:
          explicit AdditionNode(Expression left, Expression right)
              : NodeBase(NodeKind::Addition), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') /* + this->_left->ToString() */ +
                      " + " /* + this->_right->ToString() */;
          }

     private:
          Expression _left;
          Expression _right;
     };

     class SubtractionNode final : public NodeBase
     {

     public:
          explicit SubtractionNode(Expression left, Expression right)
              : NodeBase(NodeKind::Subtraction), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') /* + this->_left->ToString() */ +
                      " + " /* + this->_right->ToString() */;
          }

     private:
          Expression _left;
          Expression _right;
     };

     class CompareExchangeNode final : public NodeBase
     {
     public:
          explicit CompareExchangeNode(Expression address, Expression expected, Expression replacement, bool isWeak,
                                       MemoryOrdering ordering)
              : NodeBase(NodeKind::CompareExchange), _address(std::move(address)), _expected(std::move(expected)),
                _replacement(std::move(replacement)), _isWeak(isWeak), _ordering(ordering)
          {
          }

          [[nodiscard]] const Expression& Address() const noexcept { return _address; }
          [[nodiscard]] const Expression& Expected() const noexcept { return _expected; }
          [[nodiscard]] const Expression& Replacement() const noexcept { return _replacement; }
          [[nodiscard]] bool IsWeak() const noexcept { return _isWeak; }
          [[nodiscard]] MemoryOrdering OrderingMode() const noexcept { return _ordering; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__cmpxchg(...)";
          }

     private:
          Expression _address;
          Expression _expected;
          Expression _replacement;
          bool _isWeak;
          MemoryOrdering _ordering;
     };
} // namespace ecpps::ir