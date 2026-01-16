#include <Parsing/Preprocessor.h>
#include <Parsing/Tokeniser.h>
#include <algorithm>
#include <execution>
#include <ranges>
#include <string>

static bool AssertTokens(const std::vector<ecpps::PreprocessingToken>& ppTokens,
                         const std::vector<ecpps::PreprocessingToken>& original)
{
     if (ppTokens.size() != original.size()) return false;
     auto indices = std::views::iota(size_t{0}, ppTokens.size());

     return std::all_of(std::execution::par_unseq, indices.begin(), indices.end(),
                        [&ppTokens, &original](std::size_t i)
                        {
                             const auto& rExpected = original[i];
                             const auto& rCurrent = ppTokens[i];
                             return rExpected.type == rCurrent.type && rExpected.value == rCurrent.value;
                        });
}

int main()
{
     static const std::string source = R"(
int main() { return 0; }
)";
     std::vector<ecpps::MacroReplacement> macros{};
     const auto ppTokens = ecpps::Preprocessor::Parse(source, macros);
     if (!AssertTokens(
             ppTokens,
             std::vector<ecpps::PreprocessingToken>{
                 ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::Identifier, "int", ecpps::Location{0, 0, 0}},
                 ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::Identifier, "main", ecpps::Location{0, 0, 0}},
                 ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::OperatorOrPunctuator, "(",
                                           ecpps::Location{0, 0, 0}},
                 ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::OperatorOrPunctuator, ")",
                                           ecpps::Location{0, 0, 0}},
                 ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::OperatorOrPunctuator, "{",
                                           ecpps::Location{0, 0, 0}},
                 ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::Identifier, "return",
                                           ecpps::Location{0, 0, 0}},
                 ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::Number, "0", ecpps::Location{0, 0, 0}},
                 ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::OperatorOrPunctuator, ";",
                                           ecpps::Location{0, 0, 0}},
                 ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::OperatorOrPunctuator, "}",
                                           ecpps::Location{0, 0, 0}},
             }))
          return -1;

     return 0;
}
