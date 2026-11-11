#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Shared/Error.h"

namespace ecpps::ir::overloadDiagnostics
{
     struct CandidateDetail
     {
          std::string message{};
          Location source{};
          std::optional<std::string> followup{};
     };

     struct Candidate
     {
          std::string signature{};
          Location source{};
          std::vector<CandidateDetail> details{};
          std::optional<std::string> access{};
          bool isDeleted{};
     };

     struct SimilarName
     {
          std::string name{};
          Location source{};
          std::size_t distance{};
          std::size_t argumentFailureCount{};
          bool acceptsArguments{};
          bool isDataMember{};
          std::vector<Candidate> candidates{};
     };

     enum struct SimilarNamesKind : std::uint8_t
     {
          Functions,
          Members
     };

     [[nodiscard]] std::unique_ptr<diagnostics::TypeError> NoMatchingOverload(
          const std::string& name, Location source, const std::vector<Candidate>& candidates,
          const std::vector<SimilarName>& similarNames, SimilarNamesKind similarNamesKind);
     [[nodiscard]] std::unique_ptr<diagnostics::TypeError> AmbiguousOverload(
          const std::string& name, Location source, const std::vector<Candidate>& candidates = {});
     [[nodiscard]] std::unique_ptr<diagnostics::TypeError> SelectedOverloadFailure(
          std::string signature, Location source, bool inaccessible, bool isDeleted,
          const std::optional<Candidate>& candidate = std::nullopt);
     [[nodiscard]] diagnostics::DiagnosticsMessage UnresolvedFunction(const std::string& name, Location source,
                                                                      const std::vector<SimilarName>& similarNames);
     [[nodiscard]] diagnostics::DiagnosticsMessage MissingMember(std::string className, std::string memberName,
                                                                 Location source,
                                                                 std::optional<Location> dataMemberSource,
                                                                 const std::vector<SimilarName>& similarNames);
     [[nodiscard]] std::unique_ptr<diagnostics::TypeError> NoMatchingConstructor(
          std::string className, Location source, const std::vector<Candidate>& candidates = {});
     [[nodiscard]] std::unique_ptr<diagnostics::TypeError> AmbiguousConstructor(
          std::string className, Location source, const std::vector<Candidate>& candidates = {});
     [[nodiscard]] std::unique_ptr<diagnostics::TypeError> SelectedConstructorFailure(std::string message,
                                                                                      Location source,
                                                                                      const Candidate& candidate);
     [[nodiscard]] std::unique_ptr<diagnostics::TypeError> NoViableCopyOrMoveConstructor(
          std::string className, Location source, const std::vector<Candidate>& candidates);
     [[nodiscard]] std::unique_ptr<diagnostics::TypeError> SelectedDestructorFailure(std::string className,
                                                                                     Location source, bool inaccessible,
                                                                                     bool isDeleted,
                                                                                     const Candidate& candidate);
} // namespace ecpps::ir::overloadDiagnostics
