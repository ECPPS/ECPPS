// NOLINTBEGIN

#ifdef __ecpps_version
#define DLLIMPORT [[dllimport]]
#else
#define DLLIMPORT __declspec(dllimport)
#endif

DLLIMPORT extern "C" void DebugBreak();
DLLIMPORT extern "C" void ExitProcess(unsigned);
int main()
{
     const char p[] = "aaa";
     const char* ptr = p;
     return *p * *"2";
}

#ifdef __ecpps_version
extern "C" void _EntryPoint() { ExitProcess(main()); }
#endif

// NOLINTEND
