// NOLINTBEGIN

#ifdef __ecpps_version
#define DLLIMPORT [[dllimport]]
#else
#define DLLIMPORT __declspec(dllimport)
#endif

DLLIMPORT extern "C" void ExitProcess(unsigned);
DLLIMPORT extern "C" void DebugBreak();

int add(int a) { return a; }
int add(int a, int b) { return a + b; }
int add(int a, int b, int c) { return a + b + c; }
int add(int a, int b, int c, int d) { return a + b + c + d; }
int add(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
int add(int a, int b, int c, int d, int e, int x) { return a + b + c + d + e + x; }
int add(int a, int b, int c, int d, int e, int x, int y) { return a + b + c + d + e + x + y; }
int add(int a, int b, int c, int d, int e, int x, int y, int z) { return a + b + c + d + e + x + y + z; }

int main()
{
     return add(1) + add(1, 2) + add(1, 2, 3) + add(1, 2, 3, 4) + add(1, 2, 3, 4, 5) + add(1, 2, 3, 4, 5, 6) +
            add(1, 2, 3, 4, 5, 6, 7);
}

#ifdef __ecpps_version
extern "C" void _EntryPoint() { ExitProcess(main()); }
#endif

// NOLINTEND
