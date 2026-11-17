#include "OverloadDiagnostics.h"
#include <format>
#include <utility>

namespace
{
     [[nodiscard]] std::unique_ptr<ecpps::diagnostics::Information> MakeCandidateNote(
          const ecpps::ir::overloadDiagnostics::Candidate& candidate)
     {
          auto note = std::make_unique<ecpps::diagnostics::Information>(
               "candidate", "Candidate: " + candidate.signature, candidate.source);
          for (const auto& detail : candidate.details)
          {
               auto detailNote =
                    std::make_unique<ecpps::diagnostics::Information>("note", detail.message, detail.source);
               if (detail.followup.has_value())
                    detailNote->SubDiagnostics().push_back(
                         std::make_unique<ecpps::diagnostics::Information>("note", *detail.followup, detail.source));
               note->SubDiagnostics().push_back(std::move(detailNote));
          }
          if (candidate.access.has_value())
               note->SubDiagnostics().push_back(std::make_unique<ecpps::diagnostics::Information>(
                    "note", std::format("This candidate is {} and is inaccessible here", *candidate.access),
                    candidate.source));
          if (candidate.isDeleted)
               note->SubDiagnostics().push_back(std::make_unique<ecpps::diagnostics::Information>(
                    "note", "This candidate is deleted", candidate.source));
          return note;
     }

     template <typename TDiagnostic>
     void AppendCandidates(TDiagnostic& diagnostic,
                           const std::vector<ecpps::ir::overloadDiagnostics::Candidate>& candidates)
     {
          for (const auto& candidate : candidates) diagnostic.SubDiagnostics().push_back(MakeCandidateNote(candidate));
     }

     void AppendSimilarNames(ecpps::diagnostics::Error& diagnostic,
                             const std::vector<ecpps::ir::overloadDiagnostics::SimilarName>& similarNames,
                             const ecpps::ir::overloadDiagnostics::SimilarNamesKind kind, const ecpps::Location& source)
     {
          if (similarNames.empty()) return;

          const bool members = kind == ecpps::ir::overloadDiagnostics::SimilarNamesKind::Members;
          auto group = std::make_unique<ecpps::diagnostics::Information>(
               members ? "suggestions" : "note", members ? "Similar members" : "Did you mean:", source);
          for (std::size_t index = 0; index < similarNames.size(); index++)
          {
               const auto& similar = similarNames[index];
               const bool marked = index < 5;
               std::string description{};
               if (members)
               {
                    description = std::format("{} Similar member '{}'", marked ? "*" : "", similar.name);
                    if (!similar.candidates.empty())
                         description += similar.acceptsArguments ? " has a parameter list accepting these arguments"
                                                                 : " has no overload accepting these arguments";
                    if (similar.isDataMember)
                         description += similar.candidates.empty() ? " is a data member and is not callable"
                                                                   : "; it is also a data member and is not callable";
               }
               else
                    description = std::format(
                         "{}{} [distance={}; {}]", marked ? "* " : "", similar.name, similar.distance,
                         similar.acceptsArguments ? "accepts these arguments" : "no overload accepts these arguments");

               auto suggestion = std::make_unique<ecpps::diagnostics::Information>(members ? "suggestion" : "",
                                                                                   description, similar.source);
               if (members) AppendCandidates(*suggestion, similar.candidates);
               group->SubDiagnostics().push_back(std::move(suggestion));
          }
          diagnostic.SubDiagnostics().push_back(std::move(group));
     }
} // namespace

std::unique_ptr<ecpps::diagnostics::TypeError> ecpps::ir::overloadDiagnostics::NoMatchingOverload(
     const std::string& name, const Location source, const std::vector<Candidate>& candidates,
     const std::vector<SimilarName>& similarNames, const SimilarNamesKind similarNamesKind)
{
     auto diagnostic =
          std::make_unique<diagnostics::TypeError>(std::format("No matching overload for call to '{}'", name), source);
     AppendCandidates(*diagnostic, candidates);
     AppendSimilarNames(*diagnostic, similarNames, similarNamesKind, source);
     return diagnostic;
}

std::unique_ptr<ecpps::diagnostics::TypeError> ecpps::ir::overloadDiagnostics::AmbiguousOverload(
     const std::string& name, const Location source, const std::vector<Candidate>& candidates)
{
     auto diagnostic = std::make_unique<diagnostics::TypeError>(std::format("Call to '{}' is ambiguous", name), source);
     AppendCandidates(*diagnostic, candidates);
     return diagnostic;
}

std::unique_ptr<ecpps::diagnostics::TypeError> ecpps::ir::overloadDiagnostics::SelectedOverloadFailure(
     std::string signature, const Location source, const bool inaccessible, const bool isDeleted,
     const std::optional<Candidate>& candidate)
{
     std::string message = std::format("Selected overload '{}' is ", signature);
     if (inaccessible && isDeleted) message += "inaccessible and deleted";
     else if (inaccessible)
          message += "inaccessible";
     else
          message += "deleted";

     auto diagnostic = std::make_unique<diagnostics::TypeError>(std::move(message), source);
     if (candidate.has_value()) diagnostic->SubDiagnostics().push_back(MakeCandidateNote(*candidate));
     return diagnostic;
}

ecpps::diagnostics::DiagnosticsMessage ecpps::ir::overloadDiagnostics::UnresolvedFunction(
     const std::string& name, const Location source, const std::vector<SimilarName>& similarNames)
{
     auto diagnostic =
          std::make_unique<diagnostics::UnresolvedSymbolError>(name, "Unresolved function " + name, source);
     AppendSimilarNames(*diagnostic, similarNames, SimilarNamesKind::Functions, source);
     return diagnostic;
}

ecpps::diagnostics::DiagnosticsMessage ecpps::ir::overloadDiagnostics::MissingMember(
     std::string className, std::string memberName, const Location source,
     const std::optional<Location> dataMemberSource, const std::vector<SimilarName>& similarNames)
{
     const auto qualifiedName = className + "::" + memberName;
     if (dataMemberSource.has_value())
     {
          auto diagnostic = std::make_unique<diagnostics::TypeError>(
               std::format("Member '{}' of '{}' is a data member and cannot be called", memberName, className), source);
          diagnostic->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
               "declaration", "Data member declared here; data members are not callable", *dataMemberSource));
          AppendSimilarNames(*diagnostic, similarNames, SimilarNamesKind::Members, source);
          return diagnostic;
     }

     auto diagnostic = std::make_unique<diagnostics::UnresolvedSymbolError>(
          qualifiedName, "Unresolved function " + qualifiedName, source);
     AppendSimilarNames(*diagnostic, similarNames, SimilarNamesKind::Members, source);
     return diagnostic;
}

std::unique_ptr<ecpps::diagnostics::TypeError> ecpps::ir::overloadDiagnostics::NoMatchingConstructor(
     std::string className, const Location source, const std::vector<Candidate>& candidates)
{
     auto diagnostic = std::make_unique<diagnostics::TypeError>(
          std::format("No constructor of '{}' matches these arguments", className), source);
     AppendCandidates(*diagnostic, candidates);
     return diagnostic;
}

std::unique_ptr<ecpps::diagnostics::TypeError> ecpps::ir::overloadDiagnostics::AmbiguousConstructor(
     std::string className, const Location source, const std::vector<Candidate>& candidates)
{
     auto diagnostic = std::make_unique<diagnostics::TypeError>(
          std::format("Constructor call for '{}' is ambiguous", className), source);
     AppendCandidates(*diagnostic, candidates);
     return diagnostic;
}

std::unique_ptr<ecpps::diagnostics::TypeError> ecpps::ir::overloadDiagnostics::SelectedConstructorFailure(
     std::string message, const Location source, const Candidate& candidate)
{
     auto diagnostic = std::make_unique<diagnostics::TypeError>(std::move(message), source);
     diagnostic->SubDiagnostics().push_back(MakeCandidateNote(candidate));
     return diagnostic;
}

std::unique_ptr<ecpps::diagnostics::TypeError> ecpps::ir::overloadDiagnostics::NoViableCopyOrMoveConstructor(
     std::string className, const Location source, const std::vector<Candidate>& candidates)
{
     auto diagnostic = std::make_unique<diagnostics::TypeError>(
          std::format("No viable copy or move constructor for '{}'", className), source);
     AppendCandidates(*diagnostic, candidates);
     return diagnostic;
}

std::unique_ptr<ecpps::diagnostics::TypeError> ecpps::ir::overloadDiagnostics::SelectedDestructorFailure(
     std::string className, const Location source, const bool inaccessible, const bool isDeleted,
     const Candidate& candidate)
{
     std::string message = std::format("Destructor for '{}' is ", className);
     if (inaccessible && isDeleted) message += "inaccessible and deleted";
     else if (inaccessible)
          message += "inaccessible";
     else
          message += "deleted";

     auto diagnostic = std::make_unique<diagnostics::TypeError>(std::move(message), source);
     diagnostic->SubDiagnostics().push_back(MakeCandidateNote(candidate));
     return diagnostic;
}
