// NOLINTBEGIN

#ifdef __ecpps_version
[[dllimport]] extern "C" void ExitProcess(int);
[[dllimport]] extern "C" void DebugBreak();
#else
extern "C" void ExitProcess(int);
extern "C" void DebugBreak();
#endif

int main()
{
     int variable = 7;
     variable++;
     return variable;
}

#ifdef __ecpps_version
extern "C" void _EntryPoint() { ExitProcess(main()); }
#endif

// NOLINTEND
