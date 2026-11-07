#include "Diagnostics.h"
#include "Context.h"

void ecpps::ir::DiagnosticSink::Report(Context& context, diagnostics::DiagnosticsMessage diagnostic)
{
     context.diagnostics.get().diagnosticsList.push_back(std::move(diagnostic));
}

std::vector<ecpps::diagnostics::DiagnosticsMessage>& ecpps::ir::DiagnosticSink::List(Context& context)
{
     return context.diagnostics.get().diagnosticsList;
}

std::size_t ecpps::ir::DiagnosticSink::Count(const Context& context)
{
     return context.diagnostics.get().diagnosticsList.size();
}
