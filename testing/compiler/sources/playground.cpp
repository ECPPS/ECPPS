// NOLINTBEGIN

#ifdef __ecpps_version
#define DLLIMPORT [[dllimport]]
#else
#define DLLIMPORT __declspec(dllimport)
#endif

DLLIMPORT extern "C" void ExitProcess(unsigned);
DLLIMPORT extern "C" void DebugBreak();

int one(char param) { return param++; }

int main() { return one(2); }

#ifdef __ecpps_version
extern "C" void _EntryPoint() { ExitProcess(main()); }
#endif

// NOLINTEND
