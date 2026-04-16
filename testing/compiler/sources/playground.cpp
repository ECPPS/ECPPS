#include <cstdio>

#define EXPAND(x) #x

#define STRINGIFY(x) EXPAND(x)

int main()
{
     long long x = 1;
     return alignof(char);
}
