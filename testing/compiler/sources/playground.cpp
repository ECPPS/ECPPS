#include <cstdio>
int main()
{
     char meow[] = "gbyrgfreytfg7yrefyregfyurefyuryufreyufrvyufvyrfvyrfvyru";
     std::puts("meow");
     std::puts(&"meow"[0] + 1);
     std::puts(&"meow"[2]);
     std::puts(&"meow"[2] - 2 + 2 - 2);
     std::puts(meow);
}
