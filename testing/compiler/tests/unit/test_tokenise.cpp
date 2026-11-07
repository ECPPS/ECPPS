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
     ecpps::StringPool stringPool{};
     const auto ppTokens = ecpps::Preprocessor{stringPool}.Parse(source, macros, "");
     const auto expectedToken = [&stringPool](const ecpps::PreprocessingTokenType type, const std::string_view value)
     {
          return ecpps::PreprocessingToken{type, stringPool.Intern(value), ecpps::Location{0, 0, 0}};
     };
     if (!AssertTokens(ppTokens, std::vector<ecpps::PreprocessingToken>{
                                      expectedToken(ecpps::PreprocessingTokenType::Identifier, "int"),
                                      expectedToken(ecpps::PreprocessingTokenType::Identifier, "main"),
                                      expectedToken(ecpps::PreprocessingTokenType::OperatorOrPunctuator, "("),
                                      expectedToken(ecpps::PreprocessingTokenType::OperatorOrPunctuator, ")"),
                                      expectedToken(ecpps::PreprocessingTokenType::OperatorOrPunctuator, "{"),
                                      expectedToken(ecpps::PreprocessingTokenType::Identifier, "return"),
                                      expectedToken(ecpps::PreprocessingTokenType::Number, "0"),
                                      expectedToken(ecpps::PreprocessingTokenType::OperatorOrPunctuator, ";"),
                                      expectedToken(ecpps::PreprocessingTokenType::OperatorOrPunctuator, "}"),
                                 }))
          return -1;

     return 0;
}
