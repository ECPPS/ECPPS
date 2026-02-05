// NOLINTBEGIN

#ifdef __ecpps_version
[[dllimport]] extern "C" void ExitProcess(int);
[[dllimport]] extern "C" void DebugBreak();
#else
extern "C" void ExitProcess(unsigned int);
extern "C" void DebugBreak();
#endif

int main() { const char p[] = "aaa"; }

#ifdef __ecpps_version
extern "C" void _EntryPoint() { ExitProcess(main()); }
#endif

// NOLINTEND
