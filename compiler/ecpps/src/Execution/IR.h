#pragma once
#include <utility>
#include <vector>
#include "../Parsing/AST.h"
#include "../Shared/Diagnostics.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "Context.h"
#include "Expressions.h"
#include "NodeBase.h"

namespace ecpps::ir
{
     enum struct MatchingScore : std::size_t
     {
          NotMatching = 0,
          MaxScore = static_cast<std::size_t>(-1),
     };

     enum struct StandardConversionKind
     {
          Identity,
          LvalueToRvalue,
          ArrayToPointer,
          FunctionToPointer,
          IntegralPromotion,
          FloatingPointPromotion,
          IntegralConversion,
          FloatingPointConversion,
          PointerConversion,
          PointerToMemberConversion,
          BooleanConversion,
          QualificationAdjustment,
          NoConversion, // or error
     };

     struct ImplicitConversion
     {
          enum struct RefBindingKind
          {
               None,            // No ref involved (e.g. T <- expr)
               LValueRef,       // T& <- lvalue
               ConstLValueRef,  // const T& <- lvalue
               RValueRef,       // T&& <- xvalue
               ConstRValueRef,  // const T&& <- prvalue
               BindToTemporary, // T& <- prvalue needs temp
               IllFormed
          };

          ecpps::typeSystem::ConversionSequence typeSequence;
          RefBindingKind refKind = RefBindingKind::None;
          bool isValid = false;

          [[nodiscard]] bool IsExactMatch(void) const noexcept
          {
               return this->isValid && this->typeSequence.SameAs() && this->refKind == RefBindingKind::None;
          }

          explicit ImplicitConversion(ecpps::typeSystem::ConversionSequence typeSequence, const RefBindingKind refKind,
                                      const bool isValid)
              : typeSequence(std::move(typeSequence)), refKind(refKind), isValid(isValid)
          {
          }
          [[nodiscard]] MatchingScore Rank(void) const noexcept;
     };

     /// <summary>
     /// Matches expression to type
     /// </summary>
     /// <returns></returns>
     [[nodiscard]] ImplicitConversion MatchImplicitConversion(const Expression& expression,
                                                              const typeSystem::TypePointer& type);

     constexpr bool operator!(const MatchingScore score) { return score == MatchingScore::NotMatching; }
     constexpr auto operator<=>(const MatchingScore left, const MatchingScore right)
     {
          return std::to_underlying(left) <=> std::to_underlying(right);
     }
     constexpr MatchingScore& operator++(MatchingScore& score)
     {
          return score = static_cast<MatchingScore>(std::to_underlying(score) + 1);
     }
     constexpr MatchingScore& operator--(MatchingScore& score)
     {
          return score = static_cast<MatchingScore>(std::to_underlying(score) - 1);
     }
     constexpr MatchingScore& operator+=(MatchingScore& score, const std::size_t value)
     {
          return score = static_cast<MatchingScore>(std::to_underlying(score) + value);
     }
     constexpr MatchingScore& operator-=(MatchingScore& score, const std::size_t value)
     {
          return score = static_cast<MatchingScore>(std::to_underlying(score) - value);
     }

     constexpr MatchingScore& operator+=(MatchingScore& destination, const MatchingScore other)
     {
          return destination += std::to_underlying(other);
     }

     class IR
     {
     public:
          static std::vector<NodePointer> Parse(Diagnostics& diagnostics, const std::vector<ast::NodePointer>& ast);

     private:
          explicit IR(Diagnostics& diagnostics) : _context(diagnostics) {}
          std::vector<NodePointer> _built{};
          Context _context;

          void ParseNode(const ast::NodePointer& node);
          void ParseFunctionDeclaration(const ast::FunctionDeclarationNode& node);
          void ParseFunctionDefinition(const ast::FunctionDefinitionNode& node);
          void ParseReturn(const ast::ReturnNode& node);
          void ParseVariableDeclaration(const ast::VariableDeclarationNode& node);

          Expression ParseAdditiveExpression(Expression left, ast::Operator operator_, Expression right,
                                             const Location& source);
          Expression ParseMultiplicativeExpression(Expression left, ast::Operator operator_, Expression right,
                                                   const Location& source);
          Expression ParseShiftExpression(Expression left, ast::Operator operator_, Expression right,
                                          const Location& source);

          Expression ParseBinaryExpression(const ast::BinaryOperatorNode& node);
          Expression ParseCallExpression(const ast::CallOperatorNode& node);
          Expression ParseIdExpression(const ast::IdentifierNode& expression);
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

          // matching
          MatchingScore MatchFunction(const std::shared_ptr<FunctionScope>& function,
                                      const std::vector<Expression>& arguments);
     };
} // namespace ecpps::ir