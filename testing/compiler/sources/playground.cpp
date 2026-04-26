#include <cstdio>

#define EXPAND(x) #x

#define STRINGIFY(x) EXPAND(x)

int main()
{
     bool x = true;
     return x - x;
}
