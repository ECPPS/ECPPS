#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecpps
{
     struct Location
     {
          std::size_t line;
          std::size_t position;
          std::size_t endPosition;

          explicit Location(const std::size_t line, const std::size_t position, const std::size_t endPosition)
              : line(line), position(position), endPosition(endPosition)
          {
          }
     };
} // namespace ecpps

namespace ecpps::diagnostics
{
     enum struct DiagnosticsLevel : std::uint_fast8_t
     {
          Error,
          Warning,
          Information
     };

     constexpr std::string ToString(const DiagnosticsLevel e)
     {
          switch (e)
          {
          case DiagnosticsLevel::Warning: return "Warning";
          case DiagnosticsLevel::Error: return "Error";
          case DiagnosticsLevel::Information: return "Information";
          default: return "unknown";
          }
     }

     class IDiagnosticsBase
     {
     public:
          virtual ~IDiagnosticsBase(void) = default;

          [[nodiscard]] virtual std::string Message(void) const noexcept = 0;
          [[nodiscard]] virtual DiagnosticsLevel Level(void) const noexcept = 0;
          [[nodiscard]] virtual std::string Name(void) const noexcept = 0;
          [[nodiscard]] virtual Location Source(void) const noexcept = 0;
          [[nodiscard]] virtual const std::vector<std::unique_ptr<IDiagnosticsBase>>& SubDiagnostics(
              void) const noexcept = 0;
     };

     using DiagnosticsMessage = std::unique_ptr<IDiagnosticsBase>;

     template <DiagnosticsLevel TLevel> class DiagnosticsBase : public IDiagnosticsBase
     {
     public:
          explicit DiagnosticsBase(std::string name, std::string message, Location source)
              : _name(std::move(name)), _message(std::move(message)), _source(source)
          {
          }
          [[nodiscard]] DiagnosticsLevel Level(void) const noexcept override { return TLevel; }
          [[nodiscard]] std::string Name(void) const noexcept override { return this->_name; }
          [[nodiscard]] std::string Message(void) const noexcept override { return this->_message; }
          [[nodiscard]] Location Source(void) const noexcept override { return this->_source; }
          [[nodiscard]] const std::vector<DiagnosticsMessage>& SubDiagnostics(void) const noexcept final
          {
               return this->_subDiagnostics;
          }
          [[nodiscard]] std::vector<DiagnosticsMessage>& SubDiagnostics(void) noexcept { return this->_subDiagnostics; }

     protected:
          std::string _name;
          std::string _message;
          std::vector<DiagnosticsMessage> _subDiagnostics{};
          Location _source;
     };

     using Error = DiagnosticsBase<DiagnosticsLevel::Error>;
     using Warning = DiagnosticsBase<DiagnosticsLevel::Warning>;
     using Information = DiagnosticsBase<DiagnosticsLevel::Information>;

     class InternalCompilerError final : public Error
     {
     public:
          explicit InternalCompilerError(std::string message, std::vector<std::string> trace, Location source)
              : DiagnosticsBase("internal-compiler-error", std::move(message), source), _trace(std::move(trace))
          {
          }

          [[nodiscard]] std::string Message(void) const noexcept override;

     private:
          std::vector<std::string> _trace;
     };

     class SyntaxError final : public Error
     {
     public:
          explicit SyntaxError(std::string message, Location source)
              : DiagnosticsBase("syntax-error", std::move(message), source)
          {
          }
          [[nodiscard]] std::string Message(void) const noexcept override;
     };

     class UnresolvedSymbolError final : public Error
     {
     public:
          explicit UnresolvedSymbolError(std::string symbol, std::string message, Location source)
              : DiagnosticsBase("unresolved-symbol-error", std::move(message), source), _symbol(std::move(symbol))
          {
          }
          [[nodiscard]] std::string Message(void) const noexcept override;

     private:
          std::string _symbol;
     };

     class TypeError final : public Error
     {
     public:
          explicit TypeError(std::string message, Location source)
              : DiagnosticsBase("type-error", std::move(message), source)
          {
          }
          [[nodiscard]] std::string Message(void) const noexcept override;
     };

     class ConstantEvaluationError final : public Error
     {
     public:
          explicit ConstantEvaluationError(std::string message, Location source)
              : DiagnosticsBase("constexpr-error", std::move(message), source)
          {
          }
          [[nodiscard]] std::string Message(void) const noexcept override;
     };

     class ConstantEvaluationWarning final : public Warning
     {
     public:
          explicit ConstantEvaluationWarning(std::string message, Location source)
              : DiagnosticsBase("constexpr-error", std::move(message), source)
          {
          }
          [[nodiscard]] std::string Message(void) const noexcept override;
     };

     template <typename TDiagnostics>
          requires std::is_base_of_v<IDiagnosticsBase, TDiagnostics>
     class DiagnosticsBuilder
     {
     public:
          template <typename... TArgs>
               requires std::is_constructible_v<TDiagnostics, TArgs...>
          [[nodiscard]] std::unique_ptr<TDiagnostics> Build(TArgs&&... args) const noexcept
          {
               return std::make_unique<TDiagnostics>(std::forward<decltype(args)>(args)...);
          }
     };

     void PrintDiagnostic(const std::string& fileName, const DiagnosticsMessage& diagnostic, int indent = 0);
} // namespace ecpps::diagnostics
