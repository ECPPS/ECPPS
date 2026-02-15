// NOLINTBEGIN

#ifdef __ecpps_version
[[dllimport]] extern "C" void ExitProcess(unsigned);
[[dllimport]] extern "C" void DebugBreak();
#else
extern "C" void ExitProcess(unsigned);
extern "C" void DebugBreak();
#endif

int main() { return 50 * *"2"; }

#ifdef __ecpps_version
extern "C" void _EntryPoint() { ExitProcess(main()); }
#endif

// NOLINTEND
