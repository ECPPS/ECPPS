#pragma once

#include <catch_amalgamated.hpp>
#include <string_view>

namespace TestHelpers
{

     /// Pre-defined source code snippets for testing
     namespace SourceFixtures
     {

          constexpr std::string_view SimpleFunction = R"(
int main() { 
    return 0; 
}
)";

          constexpr std::string_view ArithmeticExpression = R"(
int main() {
    int x = 1 + 2 * 3;
    return x;
}
)";

          constexpr std::string_view ControlFlow = R"(
int main() {
    int sum = 0;
    for (int i = 0; i < 10; ++i) {
        if (i % 2 == 0) {
            sum += i;
        } else {
            sum -= 1;
        }
    }
    return sum;
}
)";

          constexpr std::string_view FunctionCall = R"(
int add(int a, int b) {
    return a + b;
}

int main() {
    return add(5, 7);
}
)";

          constexpr std::string_view PointerOperations = R"(
int main() {
    int x = 42;
    int* p = &x;
    int y = *p;
    return y;
}
)";

          constexpr std::string_view ArrayDeclaration = R"(
int main() {
    int arr[10];
    arr[0] = 1;
    arr[9] = 9;
    return arr[0] + arr[9];
}
)";

          constexpr std::string_view PointerArithmetic = R"(
int main() {
    int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int* p = arr;
    p = p + 5;
    int value = *p;
    return value; // returns 5
}
)";

          constexpr std::string_view VariableDeclarations = R"(
int main() {
    int a = 1;
    long b = 2L;
    short c = 3;
    char d = 'x';
    unsigned int e = 4u;
    return 0;
}
)";

          constexpr std::string_view ConstExpressions = R"(
int main() {
    constexpr int x = 10 + 20;
    constexpr int y = x * 2;
    return y;
}
)";

     } // namespace SourceFixtures

} // namespace TestHelpers
