// NOLINTBEGIN

#ifdef __ecpps_version
#define DLLIMPORT [[dllimport]]
#else
#define DLLIMPORT __declspec(dllimport)
#endif

DLLIMPORT extern "C" void ExitProcess(unsigned);
DLLIMPORT extern "C" void DebugBreak();

int main()
{
     const char* meow1 = "test";
     const char* meow2 = "test";
     return meow1 - meow2;
}

#ifdef __ecpps_version
extern "C" void _EntryPoint() { ExitProcess(main()); }
#endif

// NOLINTEND
