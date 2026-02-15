// NOLINTBEGIN

#ifdef __ecpps_version
#define DLLIMPORT [[dllimport]]
#else
#define DLLIMPORT __declspec(dllimport)
#endif

DLLIMPORT extern "C" void ExitProcess(unsigned);
DLLIMPORT extern "C" void DebugBreak();

int one() { return 2; }

int main() { const char meow[*"2"] = "a"; }

#ifdef __ecpps_version
extern "C" void _EntryPoint() { ExitProcess(main()); }
#endif

// NOLINTEND
