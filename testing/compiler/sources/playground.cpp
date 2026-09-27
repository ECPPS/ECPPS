using LL = long long;

int main()
{
     int x = 123;
     x = x ^ (x >> 13);
     x = x ^ (x << 7);
     x = x ^ (x >> 17);
     return x;
}
