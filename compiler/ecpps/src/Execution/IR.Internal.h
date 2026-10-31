#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
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
     [[nodiscard]] std::size_t ConversionCost(typeSystem::ConversionSequence::ConversionKind kind) noexcept;
     [[nodiscard]] std::string FormatFunctionSignature(const std::shared_ptr<FunctionScope>& func);
     [[nodiscard]] bool IsIndirectLValue(const Expression& expression);
     inline constexpr std::string_view IndirectStoreUnsupported =
          "Modifying an object through a pointer is not supported yet (the IR has no indirect store)";
     [[nodiscard]] std::size_t LevenshteinDistance(const std::string& string1, const std::string& string2);
     [[nodiscard]] std::uint64_t ConvertConstant(std::uint64_t value, const typeSystem::IntegralType* from,
                                                 const typeSystem::IntegralType* to);
     [[nodiscard]] std::vector<std::shared_ptr<FunctionScope>> CollectExactMatches(const std::string&,
                                                                                   std::deque<ContextPointer>&);
     [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> CollectSimilarNames(const std::string&, std::size_t,
                                                                                        std::deque<ContextPointer>&);
     [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> CollectSimilarIdentifierNames(
          const std::string&, std::deque<ContextPointer>&);
     struct CandidateFailureInfo
     {
          std::shared_ptr<FunctionScope> function;
          std::vector<std::pair<std::size_t, std::string>> parameterFailures;
     };
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
     [[nodiscard]] CandidateFailureInfo AnalyseCandidateFailure(const std::shared_ptr<FunctionScope>&,
                                                                const std::vector<Expression>&);
     [[nodiscard]] bool DeclaresNonType(const Scope&, const std::string&);
     [[nodiscard]] typeSystem::ClassKind ToClassKind(ast::ClassKey);
     [[nodiscard]] typeSystem::Access ToAccess(ast::AccessSpecifier);
     [[nodiscard]] typeSystem::Qualifiers QualifiersFrom(bool, bool);
     [[nodiscard]] TypeRequest ErrorRequest(std::vector<diagnostics::DiagnosticsMessage> diagnostics = {});
     [[nodiscard]] bool IsValidIntegerSpecifierCombination(const std::vector<std::string_view>&);
     enum struct Derivation : std::uint_fast8_t
     {
          None,
          Pointer,
          Reference,
          Array,
          Function
     };
     [[nodiscard]] Derivation LastDerivation(const ast::Declarator&);
     [[nodiscard]] const ast::IdentifierNode* DeclaratorIdentifier(const ast::Declarator&);
} // namespace ecpps::ir::detail
