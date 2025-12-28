// NOLINTBEGIN

#ifdef __ecpps_version
[[dllimport]] extern "C" void ExitProcess(int);
[[dllimport]] extern "C" void DebugBreak();
#else
extern "C" void ExitProcess(int);
extern "C" void DebugBreak();
#endif

short One()
{
     char x = 97;
     return x;
}

int Two()
{
     char x = 97;
     return x;
}

unsigned int Three()
{
     unsigned char x = 97;
     return x;
}

int main() { return One() + Two() + Three(); }

#ifdef __ecpps_version
extern "C" void _EntryPoint() { ExitProcess(main()); }
#endif

// NOLINTEND
