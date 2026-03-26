#include <cstdio>

#define EXPAND(x) #x

#define STRINGIFY(x) EXPAND(x)

int main()
{
     std::puts(STRINGIFY(__LINE__));
     return 0;
}
