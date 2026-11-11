#include <type_traits>
struct N
{
     N(const N&) = delete;
};
struct Test
{
     int m;
     N n;
};

Test t(Test a)
{
     return a;
}
