[[dllimport]] extern "C" void ExitProcess(int code);

int main()
{
     long long x = 2;
     int* meow;
     ExitProcess(x);
}