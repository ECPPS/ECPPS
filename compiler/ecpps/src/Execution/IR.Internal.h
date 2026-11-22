#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Context.h"
#include "Execution/MemoryAccess.h"
#include "IR.h"
#include "Parsing/AST.h"
#include "Parsing/ASTs/Type.h"
#include "Shared/Error.h"
#include "TypeSystem/ClassInfo.h"
namespace ecpps::ir::detail
{
     void InitialiseBuiltinTypes(Context& context);
     [[nodiscard]] std::string FormatTypeName(typeSystem::NonowningTypePointer type);
     [[nodiscard, maybe_unused]] MemoryAccess AccessFor(typeSystem::NonowningTypePointer type);
     [[nodiscard]] MemoryAccess AccessForStorage(typeSystem::NonowningTypePointer type);
     [[nodiscard]] std::size_t ConversionCost(typeSystem::ConversionSequence::ConversionKind kind) noexcept;
     [[nodiscard]] std::string FormatFunctionSignature(const std::shared_ptr<FunctionScope>& func);
     [[nodiscard]] bool IsIndirectLValue(const Expression& expression);
     // Scores above the caller's cutoff return cutoff + 1.
     [[nodiscard]] std::size_t LevenshteinDistance(std::string_view string1, std::string_view string2,
                                                   std::size_t maximumDistance);
     [[nodiscard]] std::uint64_t ConvertConstant(std::uint64_t value, const typeSystem::IntegralType* from,
                                                 const typeSystem::IntegralType* to);
     [[nodiscard]] std::vector<std::shared_ptr<FunctionScope>> CollectExactMatches(
          const std::string& /*name*/, std::deque<ContextPointer>& /*contextSequence*/);
     [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> CollectSimilarIdentifierNames(
          const std::string& /*name*/, std::deque<ContextPointer>& /*contextSequence*/);
     struct CandidateFailureInfo
     {
          std::shared_ptr<FunctionScope> function;
          std::vector<std::pair<std::size_t, std::string>> parameterFailures;
     };
     struct ConversionFunctionSelection
     {
          std::shared_ptr<FunctionScope> function;
          std::size_t cost{};
          bool ambiguous{};
     };
     [[nodiscard]] std::optional<ConversionFunctionSelection> FindConversionFunction(
          const Context& /*context*/, const Expression& /*expression*/, typeSystem::NonowningTypePointer /*targetType*/,
          bool allowExplicit = false);
     struct BuiltinArithmeticSelection
     {
          const typeSystem::IntegralType* type{};
          std::size_t cost{};
          bool ambiguous{};
     };
     [[nodiscard]] std::optional<BuiltinArithmeticSelection> SelectBuiltinArithmeticType(const Context& /*context*/,
                                                                                         const Expression& /*left*/,
                                                                                         const Expression& /*right*/);
     struct CompareByPriority
     {
          struct Candidate
          {
               MatchingScore argumentScore;
               std::size_t objectQualificationPenalty{};
               std::shared_ptr<FunctionScope> function;
          };

          bool operator()(const Candidate& a, const Candidate& b) const
          {
               if (a.argumentScore != b.argumentScore) return a.argumentScore < b.argumentScore;
               return a.objectQualificationPenalty > b.objectQualificationPenalty;
          }
     };
     [[nodiscard]] CandidateFailureInfo AnalyseCandidateFailure(const std::shared_ptr<FunctionScope>& /*function*/,
                                                                const std::vector<Expression>& /*arguments*/);
     [[nodiscard]] bool DeclaresNonType(const Scope& /*scope*/, const std::string& /*name*/);
     [[nodiscard]] typeSystem::ClassKind ToClassKind(ast::ClassKey /*key*/);
     [[nodiscard]] typeSystem::Access ToAccess(ast::AccessSpecifier /*spec*/);
     [[nodiscard]] typeSystem::Qualifiers QualifiersFrom(bool /*isConst*/, bool /*isVolatile*/);
     [[nodiscard]] TypeRequest ErrorRequest(std::vector<diagnostics::DiagnosticsMessage> diagnostics = {});
     [[nodiscard]] bool IsValidIntegerSpecifierCombination(const std::vector<std::string_view>& /*tokens*/);
     enum struct Derivation : std::uint_fast8_t
     {
          None,
          Pointer,
          Reference,
          Array,
          Function,
     };
     [[nodiscard]] Derivation LastDerivation(const ast::Declarator& /*declarator*/);
     [[nodiscard]] const ast::IdentifierNode* DeclaratorIdentifier(const ast::Declarator& /*declarator*/);
} // namespace ecpps::ir::detail
