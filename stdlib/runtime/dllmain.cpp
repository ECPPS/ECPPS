#include <Windows.h>

__declspec(dllexport) void _FastExit(void) // NOLINT(readability-identifier-naming, bugprone-reserved-identifier)
{
     ExitProcess(-1);
}