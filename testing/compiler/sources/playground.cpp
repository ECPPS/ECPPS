#include <cstdio>

#define EXPAND(x) #x

#define STRINGIFY(x) EXPAND(x)

int main()
{
     std::puts(STRINGIFY(__FILE__));
     puts(STRINGIFY(__LINE__));
}
