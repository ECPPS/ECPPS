#include <cstdio>
int main()
{
     int y;
     int* x = &y;
     int* z(x);
     int* a(&y);
     int** b(&a);
     int* c(b);
}
