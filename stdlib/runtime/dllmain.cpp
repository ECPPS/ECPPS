
#ifdef __ecpps_version
#define _DLLIMPORT [[dllimport]]
#define _DLLEXPORT [[dllexport]]
#elifdef _WIN32
#define _DLLIMPORT __declspec(dllimport)
#define _DLLEXPORT __declspec(dllexport)
#else
#define _DLLIMPORT
#define _DLLEXPORT
#endif

#ifdef _WIN32
extern "C" _DLLIMPORT void ExitProcess(unsigned int);
#define _EXIT_ARG unsigned int
#define _EXIT ExitProcess
#else
_DLLIMPORT extern "C" void _exit(int);
#define _EXIT_ARG int
#define _EXIT _exit
#endif
_DLLEXPORT void _FastExit(void) // NOLINT(readability-identifier-naming, bugprone-reserved-identifier)
{
     _EXIT(static_cast<_EXIT_ARG>(-1));
}
