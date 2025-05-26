#include <Windows.h>

__declspec(dllexport) void _FastExit(void) { ExitProcess(-1); }